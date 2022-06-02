package subscriptions

import "github.com/hasura/go-graphql-client"

// InsertInstances inserts multiple instances to the database.
var InsertInstances struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"insert_whist_instances(objects: $objects)"`
}

// InsertOneInstance inserts one instance to the database.
var InsertOneInstance struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"insert_whist_instances_one(objects: $objects)"`
}

// UpdateInstance updates the fields of an instance.
var UpdateInstance struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"update_whist_instances(where: {id: {_eq: $id}}, _set: $changes)"`
}

// DeleteInstanceStatusById deletes an instance with the given id and its mandelboxes from the database.
var DeleteInstanceById struct {
	MandelboxesMutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"delete_whist_mandelboxes(where: {instance_id: {_eq: $id}})"`
	InstancesMutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"delete_whist_instances(where: {id: {_eq: $id}})"`
}

// InsertImages inserts multiple images to the database.
var InsertImages struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"insert_whist_images(objects: $objects)"`
}

// UpdateMandelbox updates the row of a mandelbox on the database.
var UpdateMandelbox struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"update_whist_mandelboxes(where: {id: {_eq: $id}}, _set: $changes)"`
}

// UpdateImage updates the row of an image to the given values.
var UpdateImage struct {
	MutationResponse struct {
		AffectedRows graphql.Int `graphql:"affected_rows"`
	} `graphql:"update_whist_images(where: {region: {_eq: $region}, _and: {provider: {_eq: $provider}}}, _set: {client_sha: $client_sha, image_id: $image_id, provider: $provider, region: $region, updated_at: $updated_at})"`
}
