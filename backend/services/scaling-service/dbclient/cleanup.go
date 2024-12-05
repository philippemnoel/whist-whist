package dbclient

import (
	"context"
	"time"

	"github.com/hasura/go-graphql-client"
	"github.com/whisthq/whist/backend/services/subscriptions"
)

// LockBrokenInstances sets the column of each row of the database corresponding
// to an instance that hasn't been updated since maxAge to TERMINATING. It
// returns the instance ID of every such instance.
func (c *DBClient) LockBrokenInstances(ctx context.Context, client subscriptions.WhistGraphQLClient, region string, maxAge time.Time) ([]string, error) {
	var m subscriptions.LockBrokenInstances
	vars := map[string]interface{}{
		"maxAge": timestamptz(maxAge),
		"region": graphql.String(region),
	}

	if err := client.Mutate(ctx, &m, vars); err != nil {
		return nil, err
	}

	ids := make([]string, 0, m.Response.Count)

	for _, host := range m.Response.Hosts {
		ids = append(ids, string(host.ID))
	}

	return ids, nil
}

// TerminatedLockedInstances removes the requested rows, all of whose status
// columns should have TERMINATING, corresponding to unresponsive instances from
// the whist.instances table of the database. It also deletes all rows from the
// whist.mandelboxes table that are foreign keyed to a whist.instances row whose
// deletion has been requested.
func (c *DBClient) TerminateLockedInstances(ctx context.Context, client subscriptions.WhistGraphQLClient, region string, ids []string) ([]string, error) {
	var m subscriptions.TerminateLockedInstances

	// We need to pass the instance IDs as a slice of graphql String type
	//instances, not just a normal string slice.
	_ids := make([]graphql.String, 0, len(ids))

	for _, id := range ids {
		_ids = append(_ids, graphql.String(id))
	}

	vars := map[string]interface{}{
		"ids":    _ids,
		"region": graphql.String(region),
	}

	if err := client.Mutate(ctx, &m, vars); err != nil {
		return nil, err
	}

	terminated := make([]string, 0, m.InstancesResponse.Count)

	for _, host := range m.InstancesResponse.Hosts {
		terminated = append(terminated, string(host.ID))
	}

	return terminated, nil
}
