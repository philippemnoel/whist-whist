package subscriptions

import "github.com/hasura/go-graphql-client"

type (

	// LockBrokenInstances takes two arguments: region and maxAge. The region
	// argument matches values from the region column of the whist.instances
	// table. The maxAge argument is a timestamptz that specifies the time after
	// which, if an instance has not updated itself in the database, it will be
	// considered broken. This query gives the status column of all rows of the
	// database that have an updated_at timestamp that is older than maxAge the
	// TERMINATING status. It returns the instance_ids of the affected rows.
	LockBrokenInstances struct {
		Response struct {
			Count graphql.Int `graphql:"affected_rows"`
			Hosts []struct {
				ID graphql.String `graphql:"id"`
			} `graphql:"returning"`
		} `graphql:"update_whist_instances(where: {region: {_eq: $region}, updated_at: {_lt: $maxAge}, status: {_in: [\"ACTIVE\", \"TERMINATING\"]}}, _set: {status: \"TERMINATING\"})"`
	}

	// TerminateLockedInstances takes two arguments: region and ids. The region
	// argument matches values from the region column of the whist.instances
	// table. The ids argument specifies the instance_ids of the instances to
	// terminate. All of these instances should already be locked, i.e. the status
	// column of each of their rows in the whist.instances database should have
	// TERMINATING.
	TerminateLockedInstances struct {
		MandelboxesResponse struct {
			Count graphql.Int `graphql:"affected_rows"`
		} `graphql:"delete_whist_mandelboxes(where: {instance: {region: {_eq: $region}, id: {_in: $ids}, status: {_eq: \"TERMINATING\"}}})"`
		InstancesResponse struct {
			Count graphql.Int `graphql:"affected_rows"`
			Hosts []struct {
				ID graphql.String `graphql:"id"`
			} `graphql:"returning"`
		} `graphql:"delete_whist_instances(where: {region: {_eq: $region}, id: {_in: $ids}, status: {_eq: \"TERMINATING\"}})"`
	}
)

var (

	// InsertInstances inserts multiple instances to the database.
	InsertInstances struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"insert_whist_instances(objects: $objects)"`
	}

	// UpdateInstance updates the fields of an instance.
	UpdateInstance struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"update_whist_instances(where: {id: {_eq: $id}}, _set: $changes)"`
	}

	// DeleteInstanceStatusById deletes an instance with the given id and its mandelboxes from the database.
	DeleteInstanceById struct {
		MandelboxesMutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"delete_whist_mandelboxes(where: {instance_id: {_eq: $id}})"`
		InstancesMutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"delete_whist_instances(where: {id: {_eq: $id}})"`
	}

	// InsertInstances inserts multiple instances to the database.
	InsertMandelboxes struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"insert_whist_mandelboxes(objects: $objects)"`
	}

	// UpdateMandelbox updates the row of a mandelbox on the database.
	UpdateMandelbox struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"update_whist_mandelboxes(where: {id: {_eq: $id}}, _set: $changes)"`
	}

	// InsertImages inserts multiple images to the database.
	InsertImages struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"insert_whist_images(objects: $objects)"`
	}

	// UpdateImage updates the row of an image to the given values.
	UpdateImage struct {
		MutationResponse struct {
			AffectedRows graphql.Int `graphql:"affected_rows"`
		} `graphql:"update_whist_images(where: {region: {_eq: $region}, _and: {provider: {_eq: $provider}}}, _set: {client_sha: $client_sha, image_id: $image_id, provider: $provider, region: $region, updated_at: $updated_at})"`
	}
)
