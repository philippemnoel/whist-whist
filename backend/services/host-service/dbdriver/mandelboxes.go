package dbdriver // import "github.com/whisthq/whist/backend/services/host-service/dbdriver"

import (
	"context"
	"time"

	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"

	"github.com/whisthq/whist/backend/services/host-service/dbdriver/queries"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// This file is concerned with database interactions at the mandelbox-level.
// Note that we don't explicitly use transactions for any database interactions
// in this file, since the instance's singular host-service should be the only
// agent interacting with the database rows for the mandelboxes once the
// scaling service creates them in the "ALLOCATED" state.

// A MandelboxStatus represents a possible status that a mandelbox can have in the database.
type MandelboxStatus string

// These represent the currently-defined statuses for mandelboxes.
const (
	MandelboxStatusWaiting    MandelboxStatus = "WAITING"
	MandelboxStatusAllocated  MandelboxStatus = "ALLOCATED"
	MandelboxStatusConnecting MandelboxStatus = "CONNECTING"
	MandelboxStatusRunning    MandelboxStatus = "RUNNING"
	MandelboxStatusDying      MandelboxStatus = "DYING"
)

// CreateMandelbox inserts a new row to the database with the necessary fields. It sets the
// status of the mandelbox to WAITING, which means that the mandelbox is waiting for a user
// to get assigned to the instance.
func CreateMandelbox(id types.MandelboxID, app string, instanceID string, sessionID string) error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("VerifyAllocatedMandelbox() called but dbdriver is not initialized!")
	}

	insertParams := queries.CreateMandelboxParams{
		ID: pgtype.Varchar{
			String: id.String(),
			Status: pgtype.Present,
		},
		App: pgtype.Varchar{
			String: app,
			Status: pgtype.Present,
		},
		InstanceID: pgtype.Varchar{
			String: instanceID,
			Status: pgtype.Present,
		},
		UserID: pgtype.Varchar{
			// The user ID will be filled in by the scaling service
			// once a user gets assigned to this instance.
			Status: pgtype.Null,
		},
		SessionID: pgtype.Varchar{
			String: sessionID,
			Status: pgtype.Present,
		},
		Status: pgtype.Varchar{
			String: string(MandelboxStatusWaiting),
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
	}
	q := queries.NewQuerier(dbpool)
	mandelboxResult, err := q.CreateMandelbox(context.Background(), insertParams)
	if err != nil {
		return utils.MakeError("failed to insert mandelbox %s to database: %s", id, err)
	} else if mandelboxResult.RowsAffected() == 0 {
		return utils.MakeError("couldn't insert mandelbox %s: inserted 0 rows to database", id)
	}

	return nil
}

func VerifyAllocatedMandelbox(userID types.UserID, mandelboxID types.MandelboxID) error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("VerifyAllocatedMandelbox() called but dbdriver is not initialized!")
	}

	// Create a transaction to register the instance, since we are querying and
	// writing separately.
	tx, err := dbpool.BeginTx(context.Background(), pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return utils.MakeError("couldn't register instance: unable to begin transaction: %s", err)
	}
	// Safe to do even if committed -- see tx.Rollback() docs.
	defer tx.Rollback(context.Background())

	instanceID := metadata.CloudMetadata.GetInstanceID()

	q := queries.NewQuerier(tx)
	rows, err := q.FindMandelboxByID(context.Background(), mandelboxID.String())
	if err != nil {
		return utils.MakeError("couldn't verify mandelbox %s for user %s: error running query: %s", mandelboxID, userID, err)
	}

	// We expect that `rows` should contain exactly one, allocated (but not
	// running) mandelbox with a matching mandelbox ID. Any other state is an
	// error.
	if len(rows) == 0 {
		return utils.MakeError("couldn't verify mandelbox %s for user %s: didn't find matching row in the database.", mandelboxID, userID)
	} else if rows[0].Status.String != string(MandelboxStatusAllocated) {
		return utils.MakeError(`couldn't verify mandelbox %s for user %s: found a mandelbox row in the database for this instance, but it's in the wrong state: expected "%s", got "%s".`, mandelboxID, userID, MandelboxStatusAllocated, rows[0].Status.String)
	} else if rows[0].InstanceID.String != string(instanceID) {
		return utils.MakeError(`couldn't verify mandelbox %s for user %s: found an allocated mandelbox row in the database, but it has the wrong instanceName: expected "%s", got "%s".`, mandelboxID, userID, rows[0].InstanceID.String, instanceID)
	}

	// Mark the mandelbox as connecting. We can't just use WriteMandelboxStatus
	// since we want to do it in a single transaction.
	params := queries.WriteMandelboxStatusParams{
		Status: pgtype.Varchar{
			String: string(MandelboxStatusConnecting),
			Status: pgtype.Present,
		},
		UpdatedAt: pgtype.Timestamptz{
			Time:   time.Now(),
			Status: pgtype.Present,
		},
		MandelboxID: mandelboxID.String(),
	}
	result, err := q.WriteMandelboxStatus(context.Background(), params)
	if err != nil {
		return utils.MakeError("couldn't write status %s for mandelbox %s: error updating existing row in table `whist.mandelboxes`: %s", MandelboxStatusConnecting, mandelboxID, err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("couldn't write status %s for mandelbox %s: row in database missing!", MandelboxStatusConnecting, mandelboxID)
	}
	logger.Infof("Updated status in database for mandelbox %s to %s: %s", mandelboxID, MandelboxStatusConnecting, result)

	// Finish the transaction
	tx.Commit(context.Background())

	return nil
}

// WriteMandelboxStatus updates a mandelbox's status in the database.
func WriteMandelboxStatus(mandelboxID types.MandelboxID, status MandelboxStatus) error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("WriteMandelboxStatus() called but dbdriver is not initialized!")
	}

	q := queries.NewQuerier(dbpool)
	params := queries.WriteMandelboxStatusParams{
		Status: pgtype.Varchar{
			String: string(status),
			Status: pgtype.Present,
		},
		UpdatedAt: pgtype.Timestamptz{
			Time:   time.Now(),
			Status: pgtype.Present,
		},
		MandelboxID: mandelboxID.String(),
	}

	result, err := q.WriteMandelboxStatus(context.Background(), params)
	if err != nil {
		return utils.MakeError("couldn't write status %s for mandelbox %s: error updating existing row in table `whist.mandelboxes`: %s", status, mandelboxID, err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("couldn't write status %s for mandelbox %s: row in database missing!", status, mandelboxID)
	}
	logger.Infof("Updated status in database for mandelbox %s to %s: %s", mandelboxID, status, result)

	return nil
}

// RemoveMandelbox removes a mandelbox's row in the database.
func RemoveMandelbox(mandelboxID types.MandelboxID) error {
	if !enabled {
		return nil
	}
	if dbpool == nil {
		return utils.MakeError("RemoveMandelbox() called but dbdriver is not initialized!")
	}

	q := queries.NewQuerier(dbpool)
	// Get mandelbox row from database
	mandelboxResult, err := q.FindMandelboxByID(context.Background(), mandelboxID.String())
	if err != nil {
		return utils.MakeError("failed to query mandelbox %s from database: %s", mandelboxID, err)
	}

	if len(mandelboxResult) == 0 {
		return utils.MakeError("mandelbox %s not found in database.", mandelboxID)
	}

	// Remove mandelbox row from database
	result, err := q.RemoveMandelbox(context.Background(), mandelboxID.String())
	if err != nil {
		return utils.MakeError("couldn't remove mandelbox %s from database: %s", mandelboxID, err)
	} else if result.RowsAffected() == 0 {
		return utils.MakeError("tried to remove mandelbox %s from database, but it was already gone!", mandelboxID)
	}
	logger.Infof("Removed row in database for mandelbox %s: %s", mandelboxID, result)

	// Update the remaining capacity to account for the removed mandelbox.
	instanceResult, err := q.UpdateInstanceCapacity(context.Background(), int32(result.RowsAffected()), mandelboxResult[0].InstanceID.String)
	if err != nil {
		return utils.MakeError("couldn't increment instance capacity after cleaning mandelbox: %s", err)
	}

	if instanceResult.RowsAffected() != 0 {
		logger.Infof("Updated capacity of %v instances.", instanceResult.RowsAffected())
	}

	return nil
}
