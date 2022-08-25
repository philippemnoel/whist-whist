package dbdriver // import "github.com/whisthq/whist/backend/services/host-service/dbdriver"

import (
	"context"
	"net"
	"strings"
	"time"

	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"

	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"

	"github.com/whisthq/whist/backend/services/host-service/dbdriver/queries"
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
)

// RegisterInstance registers the instance in the database. If the expected row
// is not found, then it returns an error. This function also starts the
// heartbeat goroutine. We use host-service heartbeats to keep track of whether
// the instance is alive or not.
func RegisterInstance() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("registerInstance() called but dbdriver is not initialized!")
	}

	publicIP4 := metadata.CloudMetadata.GetPublicIpv4()
	imageID := metadata.CloudMetadata.GetImageID()
	region := metadata.CloudMetadata.GetPlacementRegion()
	instanceType := metadata.CloudMetadata.GetInstanceType()
	instanceID := metadata.CloudMetadata.GetInstanceID()

	// Create a transaction to register the instance, since we are querying and
	// writing separately.
	tx, err := dbpool.BeginTx(context.Background(), pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return utils.MakeError("couldn't register instance: unable to begin transaction: %s", err)
	}
	// Safe to do even if committed -- see tx.Rollback() docs.
	defer tx.Rollback(context.Background())

	// Check if there's a row for us in the database already
	q := queries.NewQuerier(tx)

	rows, err := q.FindInstanceByID(context.Background(), string(instanceID))
	if err != nil {
		return utils.MakeError("error running query: %s", err)
	}

	// Since the `instance_name` is the primary key of `whist.instances`,
	// we know that `rows` ought to contain either 0 or 1 results.
	if len(rows) == 0 {
		return utils.MakeError("existing row for this instance not found in the database.")
	}

	// Verify that the properties of the existing row are actually as we expect
	if rows[0].ImageID.String != string(imageID) {
		return utils.MakeError(`existing database row found, but AMI differs: expected "%s", got "%s"`, imageID, rows[0].ImageID.String)
	}
	if rows[0].Region.String != string(region) {
		return utils.MakeError(`existing database row found, but location differs: expected "%s", got "%s"`, region, rows[0].Region.String)
	}
	if !(rows[0].ClientSha.Status == pgtype.Present && strings.HasPrefix(metadata.GetGitCommit(), rows[0].ClientSha.String)) {
		// This is the only string where we have to check status, since an empty string is a prefix for anything.
		return utils.MakeError(`existing database row found, but commit hash differs: expected "%s", got "%s"`, metadata.GetGitCommit(), rows[0].ClientSha.String)
	}
	if rows[0].InstanceType.String != string(instanceType) {
		return utils.MakeError(`existing database row found, but AWS instance type differs: expected "%s", got "%s"`, instanceType, rows[0].InstanceType.String)
	}
	if rows[0].Status.String != string(InstanceStatusPreConnection) {
		return utils.MakeError(`existing database row found, but status differs: expected "%s", got "%s"`, InstanceStatusPreConnection, rows[0].Status.String)
	}

	// There is an existing row in the database for this instance --- we now "take over" and update it with the correct information.
	result, err := q.RegisterInstance(context.Background(), queries.RegisterInstanceParams{
		Provider: pgtype.Varchar{
			String: string("AWS"),
			Status: pgtype.Present,
		},
		Region: pgtype.Varchar{
			String: string(region),
			Status: pgtype.Present,
		},
		ImageID: pgtype.Varchar{
			String: string(imageID),
			Status: pgtype.Present,
		},
		ClientSha: pgtype.Varchar{
			String: string(metadata.GetGitCommit()),
			Status: pgtype.Present,
		},
		IpAddr: pgtype.Inet{
			IPNet: &net.IPNet{
				IP: publicIP4,
			},
			Status: pgtype.Present,
		},
		InstanceType: pgtype.Varchar{
			String: string(instanceType),
			Status: pgtype.Present,
		},
		RemainingCapacity: rows[0].RemainingCapacity,
		Status: pgtype.Varchar{
			String: string(InstanceStatusActive),
			Status: pgtype.Present,
		},
		CreatedAt: pgtype.Timestamptz{
			Time:   time.Now(),
			Status: pgtype.Present,
		},
		UpdatedAt: pgtype.Timestamptz{
			Time:   time.Now(),
			Status: pgtype.Present,
		},
		InstanceID: string(instanceID),
	})
	if err != nil {
		return utils.MakeError("couldn't register instance: error updating existing row in table `whist.instances`: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("couldn't register instance in database: row went missing!")
	}
	tx.Commit(context.Background())

	logger.Infof("Successfully registered %s instance in database.", instanceID)

	// Initialize the heartbeat goroutine. Note that this goroutine does not
	// listen to the global context, and is not tracked by the global
	// goroutineTracker. This is because we want the heartbeat goroutine to
	// outlive the global context. Instead, we have the explicit Close(), which
	// ends up finishing off the heartbeat goroutine as well.
	go heartbeatGoroutine()

	return nil
}

// GetInstanceCapacity will get the capacity of this instance as established
// by the scaling service. This value will be used for deciding how many
// mandelboxes to create when starting the host service.
func GetInstanceCapacity(instanceID string) (int32, error) {
	if !enabled {
		return 0, nil
	}

	q := queries.NewQuerier(dbpool)

	rows, err := q.FindInstanceByID(context.Background(), string(instanceID))
	if err != nil {
		return -1, utils.MakeError("error running query for instance: %s", err)
	}

	if len(rows) == 0 {
		return -1, utils.MakeError("existing row for this instance not found in the database")
	}

	return rows[0].RemainingCapacity, nil
}

// MarkDraining marks this instance as draining, causing the backend (i.e. scaling-service)
// to stop assigning new mandelboxes to this instance.
func markDraining() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("markDraining() called but dbdriver is not initialized!")
	}

	q := queries.NewQuerier(dbpool)

	instanceID := metadata.CloudMetadata.GetInstanceID()

	result, err := q.WriteInstanceStatus(context.Background(), pgtype.Varchar{
		String: string(InstanceStatusDraining),
		Status: pgtype.Present,
	}, string(instanceID))

	if err != nil {
		return utils.MakeError("couldn't mark instance as draining: error updating existing row in table `whist.instances`: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("couldn't mark instance as draining: row in database went missing!")
	}
	logger.Infof("Successfully marked instance %s as draining in database.", instanceID)
	return nil
}

// unregisterInstance removes the row for the instance from the
// `whist.instances` table. Note that due to the `delete cascade`
// constraint on `whist.mandelboxes` this automatically removes all the
// mandelboxes for the instance as well.
func unregisterInstance() error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("unregisterInstance() called but dbdriver is not initialized!")
	}

	instanceID := metadata.CloudMetadata.GetInstanceID()

	q := queries.NewQuerier(dbpool)
	result, err := q.DeleteInstance(context.Background(), string(instanceID))
	if err != nil {
		return utils.MakeError("unregisterInstance(): error running delete command: %s", err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("unregisterInstance(): row went missing before we could delete it!")
	}
	logger.Infof("UnregisterInstance(): delete command successful")

	return nil
}
