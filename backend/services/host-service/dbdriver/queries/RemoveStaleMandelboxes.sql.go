// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgconn"
	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"
)

const removeStaleMandelboxesSQL = `DELETE FROM whist.mandelboxes
  WHERE
    instance_id = $1
    AND (
      (status = $2
        AND created_at < $3)
      OR (
      (status = $4
        AND created_at < $5)
      )
    );`

type RemoveStaleMandelboxesParams struct {
	InstanceID                      string
	AllocatedStatus                 MandelboxState
	AllocatedCreationTimeThreshold  pgtype.Timestamptz
	ConnectingStatus                MandelboxState
	ConnectingCreationTimeThreshold pgtype.Timestamptz
}

// RemoveStaleMandelboxes implements Querier.RemoveStaleMandelboxes.
func (q *DBQuerier) RemoveStaleMandelboxes(ctx context.Context, params RemoveStaleMandelboxesParams) (pgconn.CommandTag, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "RemoveStaleMandelboxes")
	cmdTag, err := q.conn.Exec(ctx, removeStaleMandelboxesSQL, params.InstanceID, params.AllocatedStatus, params.AllocatedCreationTimeThreshold, params.ConnectingStatus, params.ConnectingCreationTimeThreshold)
	if err != nil {
		return cmdTag, fmt.Errorf("exec query RemoveStaleMandelboxes: %w", err)
	}
	return cmdTag, err
}

// RemoveStaleMandelboxesBatch implements Querier.RemoveStaleMandelboxesBatch.
func (q *DBQuerier) RemoveStaleMandelboxesBatch(batch genericBatch, params RemoveStaleMandelboxesParams) {
	batch.Queue(removeStaleMandelboxesSQL, params.InstanceID, params.AllocatedStatus, params.AllocatedCreationTimeThreshold, params.ConnectingStatus, params.ConnectingCreationTimeThreshold)
}

// RemoveStaleMandelboxesScan implements Querier.RemoveStaleMandelboxesScan.
func (q *DBQuerier) RemoveStaleMandelboxesScan(results pgx.BatchResults) (pgconn.CommandTag, error) {
	cmdTag, err := results.Exec()
	if err != nil {
		return cmdTag, fmt.Errorf("exec RemoveStaleMandelboxesBatch: %w", err)
	}
	return cmdTag, err
}
