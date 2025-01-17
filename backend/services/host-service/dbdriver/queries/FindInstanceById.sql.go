// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"
)

const findInstanceByIDSQL = `SELECT * FROM whist.instances WHERE id = $1;`

type FindInstanceByIDRow struct {
	ID                pgtype.Varchar     `json:"id"`
	Provider          pgtype.Varchar     `json:"provider"`
	Region            pgtype.Varchar     `json:"region"`
	ImageID           pgtype.Varchar     `json:"image_id"`
	ClientSha         pgtype.Varchar     `json:"client_sha"`
	IpAddr            pgtype.Inet        `json:"ip_addr"`
	InstanceType      pgtype.Varchar     `json:"instance_type"`
	RemainingCapacity int32              `json:"remaining_capacity"`
	Status            pgtype.Varchar     `json:"status"`
	CreatedAt         pgtype.Timestamptz `json:"created_at"`
	UpdatedAt         pgtype.Timestamptz `json:"updated_at"`
}

// FindInstanceByID implements Querier.FindInstanceByID.
func (q *DBQuerier) FindInstanceByID(ctx context.Context, instanceID string) ([]FindInstanceByIDRow, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "FindInstanceByID")
	rows, err := q.conn.Query(ctx, findInstanceByIDSQL, instanceID)
	if err != nil {
		return nil, fmt.Errorf("query FindInstanceByID: %w", err)
	}
	defer rows.Close()
	items := []FindInstanceByIDRow{}
	for rows.Next() {
		var item FindInstanceByIDRow
		if err := rows.Scan(&item.ID, &item.Provider, &item.Region, &item.ImageID, &item.ClientSha, &item.IpAddr, &item.InstanceType, &item.RemainingCapacity, &item.Status, &item.CreatedAt, &item.UpdatedAt); err != nil {
			return nil, fmt.Errorf("scan FindInstanceByID row: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("close FindInstanceByID rows: %w", err)
	}
	return items, err
}

// FindInstanceByIDBatch implements Querier.FindInstanceByIDBatch.
func (q *DBQuerier) FindInstanceByIDBatch(batch genericBatch, instanceID string) {
	batch.Queue(findInstanceByIDSQL, instanceID)
}

// FindInstanceByIDScan implements Querier.FindInstanceByIDScan.
func (q *DBQuerier) FindInstanceByIDScan(results pgx.BatchResults) ([]FindInstanceByIDRow, error) {
	rows, err := results.Query()
	if err != nil {
		return nil, fmt.Errorf("query FindInstanceByIDBatch: %w", err)
	}
	defer rows.Close()
	items := []FindInstanceByIDRow{}
	for rows.Next() {
		var item FindInstanceByIDRow
		if err := rows.Scan(&item.ID, &item.Provider, &item.Region, &item.ImageID, &item.ClientSha, &item.IpAddr, &item.InstanceType, &item.RemainingCapacity, &item.Status, &item.CreatedAt, &item.UpdatedAt); err != nil {
			return nil, fmt.Errorf("scan FindInstanceByIDBatch row: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("close FindInstanceByIDBatch rows: %w", err)
	}
	return items, err
}
