package dbclient

import (
	"context"

	"github.com/hasura/go-graphql-client"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/utils"
)

// QueryImage queries the database for an instance image (AMI) that matches the given id.
func (client *DBClient) QueryImage(scalingCtx context.Context, graphQLClient subscriptions.WhistGraphQLClient, provider string, region string) ([]subscriptions.Image, error) {
	latestImageQuery := subscriptions.QueryLatestImage
	queryParams := map[string]interface{}{
		"provider": graphql.String(provider),
		"region":   graphql.String(region),
	}

	err := graphQLClient.Query(scalingCtx, &latestImageQuery, queryParams)
	imageResult := subscriptions.ToImages(latestImageQuery.WhistImages)
	return imageResult, err
}

// QueryLatestImage will query the database and try to find the most recent image registered in the database
// for a specific cloud-provider and region.
func (client *DBClient) QueryLatestImage(scalingCtx context.Context, graphQLClient subscriptions.WhistGraphQLClient, provider string, region string) (subscriptions.Image, error) {
	imageResult, err := client.QueryImage(scalingCtx, graphQLClient, provider, region) // TODO: set different provider when doing multi-cloud.
	if err != nil {
		return subscriptions.Image{}, utils.MakeError("failed to query database for current image: %s", err)
	}

	if len(imageResult) == 0 {
		return subscriptions.Image{}, utils.MakeError("image not found in %s", region)
	}

	return imageResult[0], nil
}

// InsertInstances adds the received instances to the database.
func (client *DBClient) InsertImages(scalingCtx context.Context, graphQLClient subscriptions.WhistGraphQLClient, insertParams []subscriptions.Image) (int, error) {
	insertMutation := subscriptions.InsertImages

	var imagesForDb []whist_images_insert_input

	// Due to some quirks with the Hasura client, we have to convert the
	// slice of instances to a slice of `whist_instances_insert_input`.
	for _, image := range insertParams {
		imagesForDb = append(imagesForDb, whist_images_insert_input{
			Provider:  graphql.String(image.Provider),
			Region:    graphql.String(image.Region),
			ImageID:   graphql.String(image.ImageID),
			ClientSHA: graphql.String(image.ClientSHA),
			UpdatedAt: image.UpdatedAt,
		})
	}

	mutationParams := map[string]interface{}{
		"objects": imagesForDb,
	}
	err := graphQLClient.Mutate(scalingCtx, &insertMutation, mutationParams)
	return int(insertMutation.MutationResponse.AffectedRows), err
}

// UpdateImage updates the received fields on the database.
func (client *DBClient) UpdateImage(scalingCtx context.Context, graphQLClient subscriptions.WhistGraphQLClient, image subscriptions.Image) (int, error) {
	updateMutation := subscriptions.UpdateImage

	mutationParams := map[string]interface{}{
		"provider":   graphql.String(image.Provider),
		"region":     graphql.String(image.Region),
		"image_id":   graphql.String(image.ImageID),
		"client_sha": graphql.String(image.ClientSHA),
		"updated_at": timestamptz{image.UpdatedAt},
	}

	err := graphQLClient.Mutate(scalingCtx, &updateMutation, mutationParams)
	return int(updateMutation.MutationResponse.AffectedRows), err
}
