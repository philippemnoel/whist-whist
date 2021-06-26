// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgconn"
	"github.com/jackc/pgx/v4"
)

const removeContainerSQL = `DELETE FROM hardware.container_info WHERE container_id = $1;`

// RemoveContainer implements Querier.RemoveContainer.
func (q *DBQuerier) RemoveContainer(ctx context.Context, containerID string) (pgconn.CommandTag, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "RemoveContainer")
	cmdTag, err := q.conn.Exec(ctx, removeContainerSQL, containerID)
	if err != nil {
		return cmdTag, fmt.Errorf("exec query RemoveContainer: %w", err)
	}
	return cmdTag, err
}

// RemoveContainerBatch implements Querier.RemoveContainerBatch.
func (q *DBQuerier) RemoveContainerBatch(batch *pgx.Batch, containerID string) {
	batch.Queue(removeContainerSQL, containerID)
}

// RemoveContainerScan implements Querier.RemoveContainerScan.
func (q *DBQuerier) RemoveContainerScan(results pgx.BatchResults) (pgconn.CommandTag, error) {
	cmdTag, err := results.Exec()
	if err != nil {
		return cmdTag, fmt.Errorf("exec RemoveContainerBatch: %w", err)
	}
	return cmdTag, err
}
