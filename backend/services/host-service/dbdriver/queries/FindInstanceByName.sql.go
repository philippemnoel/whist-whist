// Code generated by pggen. DO NOT EDIT.

package queries

import (
	"context"
	"fmt"
	"github.com/jackc/pgtype"
	"github.com/jackc/pgx/v4"
)

const findInstanceByNameSQL = `SELECT * FROM whist.instances WHERE id = $1;`

type FindInstanceByNameRow struct {
	ID                pgtype.Varchar     `json:"id"`
	Provider          CloudProvider      `json:"provider"`
	Region            pgtype.Varchar     `json:"region"`
	ImageID           pgtype.Varchar     `json:"image_id"`
	ClientSha         pgtype.Varchar     `json:"client_sha"`
	IpAddr            pgtype.Inet        `json:"ip_addr"`
	InstanceType      pgtype.Varchar     `json:"instance_type"`
	RemainingCapacity int32              `json:"remaining_capacity"`
	Status            InstanceState      `json:"status"`
	CreatedAt         pgtype.Timestamptz `json:"created_at"`
	UpdatedAt         pgtype.Timestamptz `json:"updated_at"`
}

// FindInstanceByName implements Querier.FindInstanceByName.
func (q *DBQuerier) FindInstanceByName(ctx context.Context, instanceID string) ([]FindInstanceByNameRow, error) {
	ctx = context.WithValue(ctx, "pggen_query_name", "FindInstanceByName")
	rows, err := q.conn.Query(ctx, findInstanceByNameSQL, instanceID)
	if err != nil {
		return nil, fmt.Errorf("query FindInstanceByName: %w", err)
	}
	defer rows.Close()
	items := []FindInstanceByNameRow{}
	for rows.Next() {
		var item FindInstanceByNameRow
		if err := rows.Scan(&item.ID, &item.Provider, &item.Region, &item.ImageID, &item.ClientSha, &item.IpAddr, &item.InstanceType, &item.RemainingCapacity, &item.Status, &item.CreatedAt, &item.UpdatedAt); err != nil {
			return nil, fmt.Errorf("scan FindInstanceByName row: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("close FindInstanceByName rows: %w", err)
	}
	return items, err
}

// FindInstanceByNameBatch implements Querier.FindInstanceByNameBatch.
func (q *DBQuerier) FindInstanceByNameBatch(batch genericBatch, instanceID string) {
	batch.Queue(findInstanceByNameSQL, instanceID)
}

// FindInstanceByNameScan implements Querier.FindInstanceByNameScan.
func (q *DBQuerier) FindInstanceByNameScan(results pgx.BatchResults) ([]FindInstanceByNameRow, error) {
	rows, err := results.Query()
	if err != nil {
		return nil, fmt.Errorf("query FindInstanceByNameBatch: %w", err)
	}
	defer rows.Close()
	items := []FindInstanceByNameRow{}
	for rows.Next() {
		var item FindInstanceByNameRow
		if err := rows.Scan(&item.ID, &item.Provider, &item.Region, &item.ImageID, &item.ClientSha, &item.IpAddr, &item.InstanceType, &item.RemainingCapacity, &item.Status, &item.CreatedAt, &item.UpdatedAt); err != nil {
			return nil, fmt.Errorf("scan FindInstanceByNameBatch row: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("close FindInstanceByNameBatch rows: %w", err)
	}
	return items, err
}
