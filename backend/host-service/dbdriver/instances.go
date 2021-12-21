package dbdriver // import "github.com/whisthq/whist/backend/host-service/dbdriver"

import (
	"context"
	"strings"
	"time"

	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"

	"github.com/whisthq/whist/backend/core-go/metadata"
	"github.com/whisthq/whist/backend/core-go/metadata/aws"
	"github.com/whisthq/whist/backend/core-go/utils"
	logger "github.com/whisthq/whist/backend/core-go/whistlogger"
	"github.com/whisthq/whist/backend/host-service/metrics"

	"github.com/whisthq/whist/backend/host-service/dbdriver/queries"
)

// This file is concerned with database interactions at the instance-level
// (except heartbeats).

// A InstanceStatus represents a possible status that this instance can have in the database.
type InstanceStatus string

// These represent the currently-defined statuses for instances.
const (
	InstanceStatusPreConnection InstanceStatus = "PRE_CONNECTION"
	InstanceStatusActive        InstanceStatus = "ACTIVE"
	InstanceStatusDraining      InstanceStatus = "DRAINING"
	InstanceStatusUnresponsive  InstanceStatus = "HOST_SERVICE_UNRESPONSIVE"
)

// RegisterInstance registers the instance in the database. If the expected row
// is not found, then it returns an error. This function also starts the
// heartbeat goroutine.
func RegisterInstance() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("registerInstance() called but dbdriver is not initialized!")
	}

	instanceName, err := aws.GetInstanceName()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get instance name: %s", err)
	}
	publicIP4, err := aws.GetPublicIpv4()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get public IPv4: %s", err)
	}
	amiID, err := aws.GetAmiID()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get AMI ID: %s", err)
	}
	region, err := aws.GetPlacementRegion()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get AWS Placement Region: %s", err)
	}
	instanceType, err := aws.GetInstanceType()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get AWS Instance type: %s", err)
	}
	instanceID, err := aws.GetInstanceID()
	if err != nil {
		return utils.MakeError("Couldn't register instance: couldn't get AWS Instance id: %s", err)
	}

	latestMetrics, errs := metrics.GetLatest()
	if len(errs) != 0 {
		return utils.MakeError("Couldn't register instance: errors getting metrics: %+v", errs)
	}

	// Create a transaction to register the instance, since we are querying and
	// writing separately.
	tx, err := dbpool.BeginTx(context.Background(), pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return utils.MakeError("Couldn't register instance: unable to begin transaction: %s", err)
	}
	// Safe to do even if committed -- see tx.Rollback() docs.
	defer tx.Rollback(context.Background())

	// Check if there's a row for us in the database already
	q := queries.NewQuerier(tx)
	rows, err := q.FindInstanceByName(context.Background(), string(instanceName))
	if err != nil {
		return utils.MakeError("RegisterInstance(): Error running query: %s", err)
	}

	// Since the `instance_name` is the primary key of `cloud.instance_info`,
	// we know that `rows` ought to contain either 0 or 1 results.
	if len(rows) == 0 {
		return utils.MakeError("RegisterInstance(): Existing row for this instance not found in the database.")
	}

	// Verify that the properties of the existing row are actually as we expect
	if rows[0].AwsAmiID.String != string(amiID) {
		return utils.MakeError(`RegisterInstance(): Existing database row found, but AMI differs. Expected "%s", Got "%s"`, amiID, rows[0].AwsAmiID.String)
	}
	if rows[0].Location.String != string(region) {
		return utils.MakeError(`RegisterInstance(): Existing database row found, but location differs. Expected "%s", Got "%s"`, region, rows[0].Location.String)
	}
	if !(rows[0].CommitHash.Status == pgtype.Present && strings.HasPrefix(metadata.GetGitCommit(), rows[0].CommitHash.String)) {
		// This is the only string where we have to check status, since an empty string is a prefix for anything.
		return utils.MakeError(`RegisterInstance(): Existing database row found, but commit hash differs. Expected "%s", Got "%s"`, metadata.GetGitCommit(), rows[0].CommitHash.String)
	}
	if rows[0].AwsInstanceType.String != string(instanceType) {
		return utils.MakeError(`RegisterInstance(): Existing database row found, but AWS instance type differs. Expected "%s", Got "%s"`, instanceType, rows[0].AwsInstanceType.String)
	}
	if rows[0].Status.String != string(InstanceStatusPreConnection) {
		return utils.MakeError(`RegisterInstance(): Existing database row found, but status differs. Expected "%s", Got "%s"`, InstanceStatusPreConnection, rows[0].Status.String)
	}

	// There is an existing row in the database for this instance --- we now "take over" and update it with the correct information.
	result, err := q.RegisterInstance(context.Background(), queries.RegisterInstanceParams{
		CloudProviderID: pgtype.Varchar{
			String: "aws-" + string(instanceID),
			Status: pgtype.Present,
		},
		MemoryRemainingKB:    int(latestMetrics.AvailableMemoryKB),
		NanoCPUsRemainingKB:  int(latestMetrics.NanoCPUsRemaining),
		GpuVramRemainingKb:   int(latestMetrics.FreeVideoMemoryKB),
		MandelboxCapacity:    latestMetrics.NumberOfGPUs,
		LastUpdatedUtcUnixMs: int(time.Now().UnixNano() / 1_000_000),
		Ip: pgtype.Varchar{
			String: publicIP4.String(),
			Status: pgtype.Present,
		},
		Status: pgtype.Varchar{
			String: string(InstanceStatusActive),
			Status: pgtype.Present,
		},
		InstanceName: string(instanceName),
	})
	if err != nil {
		return utils.MakeError("Couldn't register instance: error updating existing row in table `cloud.instance_info`: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("Couldn't register instance in database: row went missing!")
	}
	tx.Commit(context.Background())

	logger.Infof("Successfully registered %s instance in database.", instanceName)

	// Initialize the heartbeat goroutine. Note that this goroutine does not
	// listen to the global context, and is not tracked by the global
	// goroutineTracker. This is because we want the heartbeat goroutine to
	// outlive the global context. Instead, we have the explicit Close(), which
	// ends up finishing off the heartbeat goroutine as well.
	go heartbeatGoroutine()

	return nil
}

// MarkDraining marks this instance as draining, causing the webserver
// to stop assigning new mandelboxes here.
func markDraining() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("markDraining() called but dbdriver is not initialized!")
	}

	q := queries.NewQuerier(dbpool)

	instanceName, err := aws.GetInstanceName()
	if err != nil {
		return utils.MakeError("Couldn't mark instance as draining: couldn't get instance name: %s", err)
	}

	result, err := q.WriteInstanceStatus(context.Background(), pgtype.Varchar{
		String: string(InstanceStatusDraining),
		Status: pgtype.Present,
	},
		string(instanceName))
	if err != nil {
		return utils.MakeError("Couldn't mark instance as draining: error updating existing row in table `cloud.instance_info`: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("Couldn't mark instance as draining: row in database went missing!")
	}
	logger.Infof("Successfully marked instance %s as draining in database.", instanceName)
	return nil
}

// unregisterInstance removes the row for the instance from the
// `cloud.instance_info` table. Note that due to the `delete cascade`
// constraint on `cloud.mandelbox_info` this automatically removes all the
// mandelboxes for the instance as well.
func unregisterInstance() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("UnregisterInstance() called but dbdriver is not initialized!")
	}

	instanceName, err := aws.GetInstanceName()
	if err != nil {
		return utils.MakeError("Couldn't unregister instance: couldn't get instance name: %s", err)
	}

	q := queries.NewQuerier(dbpool)
	result, err := q.DeleteInstance(context.Background(), string(instanceName))
	if err != nil {
		return utils.MakeError("UnregisterInstance(): Error running delete command: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("UnregisterInstance(): row went missing before we could delete it!")
	}
	logger.Infof("UnregisterInstance(): delete command successful")

	return nil
}
