// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgconn"
	"github.com/jackc/pgx/v4"
)

const writeHeartbeatSQL = `UPDATE hardware.instance_info
  SET (memory_remaining_kb, nanocpus_remaining, gpu_vram_remaining_kb, last_updated_utc_unix_ms)
  = ($1, $2, $3, $4)
  WHERE instance_name = $5;`

type WriteHeartbeatParams struct {
	MemoryRemainingKB    int
	NanoCPUsRemainingKB  int
	GpuVramRemainingKb   int
	LastUpdatedUtcUnixMs int
	InstanceName         string
}

// WriteHeartbeat implements Querier.WriteHeartbeat.
func (q *DBQuerier) WriteHeartbeat(ctx context.Context, params WriteHeartbeatParams) (pgconn.CommandTag, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "WriteHeartbeat")
	cmdTag, err := q.conn.Exec(ctx, writeHeartbeatSQL, params.MemoryRemainingKB, params.NanoCPUsRemainingKB, params.GpuVramRemainingKb, params.LastUpdatedUtcUnixMs, params.InstanceName)
	if err != nil {
		return cmdTag, fmt.Errorf("exec query WriteHeartbeat: %w", err)
	}
	return cmdTag, err
}

// WriteHeartbeatBatch implements Querier.WriteHeartbeatBatch.
func (q *DBQuerier) WriteHeartbeatBatch(batch *pgx.Batch, params WriteHeartbeatParams) {
	batch.Queue(writeHeartbeatSQL, params.MemoryRemainingKB, params.NanoCPUsRemainingKB, params.GpuVramRemainingKb, params.LastUpdatedUtcUnixMs, params.InstanceName)
}

// WriteHeartbeatScan implements Querier.WriteHeartbeatScan.
func (q *DBQuerier) WriteHeartbeatScan(results pgx.BatchResults) (pgconn.CommandTag, error) {
	cmdTag, err := results.Exec()
	if err != nil {
		return cmdTag, fmt.Errorf("exec WriteHeartbeatBatch: %w", err)
	}
	return cmdTag, err
}
