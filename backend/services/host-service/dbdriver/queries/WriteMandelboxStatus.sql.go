// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgconn"
	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"
)

const writeMandelboxStatusSQL = `UPDATE whist.mandelboxes
  SET (status, updated_at) = ($1, $2)
  WHERE id = $3;`

type WriteMandelboxStatusParams struct {
	Status      pgtype.Varchar
	UpdatedAt   pgtype.Timestamptz
	MandelboxID string
}

// WriteMandelboxStatus implements Querier.WriteMandelboxStatus.
func (q *DBQuerier) WriteMandelboxStatus(ctx context.Context, params WriteMandelboxStatusParams) (pgconn.CommandTag, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "WriteMandelboxStatus")
	cmdTag, err := q.conn.Exec(ctx, writeMandelboxStatusSQL, params.Status, params.UpdatedAt, params.MandelboxID)
	if err != nil {
		return cmdTag, fmt.Errorf("exec query WriteMandelboxStatus: %w", err)
	}
	return cmdTag, err
}

// WriteMandelboxStatusBatch implements Querier.WriteMandelboxStatusBatch.
func (q *DBQuerier) WriteMandelboxStatusBatch(batch genericBatch, params WriteMandelboxStatusParams) {
	batch.Queue(writeMandelboxStatusSQL, params.Status, params.UpdatedAt, params.MandelboxID)
}

// WriteMandelboxStatusScan implements Querier.WriteMandelboxStatusScan.
func (q *DBQuerier) WriteMandelboxStatusScan(results pgx.BatchResults) (pgconn.CommandTag, error) {
	cmdTag, err := results.Exec()
	if err != nil {
		return cmdTag, fmt.Errorf("exec WriteMandelboxStatusBatch: %w", err)
	}
	return cmdTag, err
}
