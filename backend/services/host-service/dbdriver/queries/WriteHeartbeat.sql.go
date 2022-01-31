// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgconn"
	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"
)

const writeHeartbeatSQL = `UPDATE whist.instances SET (updated_at) = row($1) WHERE id = $2;`

// WriteHeartbeat implements Querier.WriteHeartbeat.
func (q *DBQuerier) WriteHeartbeat(ctx context.Context, updatedAt pgtype.Timestamptz, instanceID string) (pgconn.CommandTag, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "WriteHeartbeat")
	cmdTag, err := q.conn.Exec(ctx, writeHeartbeatSQL, updatedAt, instanceID)
	if err != nil {
		return cmdTag, fmt.Errorf("exec query WriteHeartbeat: %w", err)
	}
	return cmdTag, err
}

// WriteHeartbeatBatch implements Querier.WriteHeartbeatBatch.
func (q *DBQuerier) WriteHeartbeatBatch(batch genericBatch, updatedAt pgtype.Timestamptz, instanceID string) {
	batch.Queue(writeHeartbeatSQL, updatedAt, instanceID)
}

// WriteHeartbeatScan implements Querier.WriteHeartbeatScan.
func (q *DBQuerier) WriteHeartbeatScan(results pgx.BatchResults) (pgconn.CommandTag, error) {
	cmdTag, err := results.Exec()
	if err != nil {
		return cmdTag, fmt.Errorf("exec WriteHeartbeatBatch: %w", err)
	}
	return cmdTag, err
}
