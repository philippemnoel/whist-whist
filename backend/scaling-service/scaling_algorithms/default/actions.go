package scaling_algorithms

import (
	"context"

	"github.com/hasura/go-graphql-client"
	"github.com/whisthq/whist/backend/core-go/subscriptions"
	"github.com/whisthq/whist/backend/core-go/utils"
	logger "github.com/whisthq/whist/backend/core-go/whistlogger"
	"github.com/whisthq/whist/backend/scaling-service/hosts"
)

func (s *DefaultScalingAlgorithm) VerifyInstanceScaleDown(scalingCtx context.Context, host hosts.HostHandler, event ScalingEvent, instance subscriptions.Instance) error {
	logger.Infof("Verifying instance scale down for event: %v", event)

	// First, verify if the draining instance has mandelboxes running
	mandelboxesRunningQuery := &subscriptions.QueryMandelboxesByInstanceName
	queryParams := map[string]interface{}{
		"instance_name": graphql.String(instance.Name),
		"status":        graphql.String("RUNNING"),
	}

	err := s.GraphQLClient.Query(scalingCtx, mandelboxesRunningQuery, queryParams)
	if err != nil {
		return utils.MakeError("failed to query database for running mandelboxes with params: %v. Error: %v", queryParams, err)
	}

	// If instance has active mandelboxes, leave it alone
	runningMandelboxes := len(mandelboxesRunningQuery.CloudMandelboxInfo)
	if runningMandelboxes > 0 {
		logger.Infof("Instance has %v running mandelboxes. Not marking as draining.", runningMandelboxes)
		return nil
	}

	// If not, wait until the instance is terminated from the cloud provider
	err = host.WaitForInstanceTermination(scalingCtx, instance)
	if err != nil {
		// Err is already wrapped here
		return err
	}

	// Once its terminated, verify that it was removed from the database
	instanceExistsQuery := &subscriptions.QueryInstanceByName
	queryParams = map[string]interface{}{
		"instance_name": graphql.String(instance.Name),
	}

	err = s.GraphQLClient.Query(scalingCtx, instanceExistsQuery, queryParams)
	if err != nil {
		return utils.MakeError("failed to query database for instance with params: %v. Error: %v", queryParams, err)
	}

	// Verify that instance removed itself from the database
	instanceResult := instanceExistsQuery.CloudInstanceInfo
	if len(instanceResult) == 0 {
		logger.Info("Instance %v was successfully removed from database.", instance.Name)
		return nil
	}

	// If instance still exists on the database, forcefully delete as it no longer exists on cloud provider
	logger.Info("Forcefully removing instance %v from database as it no longer exists on cloud provider.", instance.Name)

	instanceDeleteMutation := &subscriptions.DeleteInstanceByName
	deleteParams := map[string]interface{}{
		"instance_name": graphql.String(instance.Name),
	}

	err = s.GraphQLClient.Mutate(scalingCtx, instanceDeleteMutation, deleteParams)
	if err != nil {
		return utils.MakeError("failed to delete instance from database with params: %v. Error: %v", queryParams, err)
	}

	logger.Infof("Deleted %v rows from database.", instanceDeleteMutation.MutationResponse.AffectedRows)

	return nil
}

func (s *DefaultScalingAlgorithm) ScaleDownIfNecessary(scalingCtx context.Context, host hosts.HostHandler, event ScalingEvent) error {
	// check database for active instances without mandelboxes
	freeInstancesQuery := &subscriptions.QueryFreeInstances
	queryParams := map[string]interface{}{
		"num_mandelboxes": graphql.Int(0),
		"status":          graphql.String("ACTIVE"),
	}

	s.GraphQLClient.Query(scalingCtx, freeInstancesQuery, queryParams)

	// Verify if there are free instances that can be scaled down
	freeInstances := len(freeInstancesQuery.CloudInstanceInfo)
	if freeInstances == 0 {
		logger.Info("There are no available instances to scale down.")
		return nil
	}

	// check database for draining instances without mandelboxes
	lingeringInstancesQuery := &subscriptions.QueryFreeInstances
	queryParams = map[string]interface{}{
		"num_mandelboxes": graphql.Int(0),
		"status":          graphql.String("DRAINING"),
	}

	s.GraphQLClient.Query(scalingCtx, freeInstancesQuery, queryParams)

	// Verify if there are lingering instances that can be scaled down
	lingeringInstances := len(lingeringInstancesQuery.CloudInstanceInfo)
	if lingeringInstances == 0 {
		logger.Info("There are no lingering to scale down.")
		return nil
	}

	var lingeringIds []string
	for _, lingeringInstance := range lingeringInstancesQuery.CloudInstanceInfo {
		lingeringIds = append(lingeringIds, string(lingeringInstance.CloudProviderID))
	}

	logger.Info("Forcefully terminating lingering instances from cloud provider.")

	_, err := s.Host.SpinDownInstances(lingeringIds)
	if err != nil {
		logger.Warningf("Failed to forcefully terminate lingering instances from cloud provider. Err: %v", err)
	}

	if freeInstances < DEFAULT_INSTANCE_BUFFER {
		logger.Warningf("Available instances are less than desired buffer, scaling up to match %v", DEFAULT_INSTANCE_BUFFER)
		wantedNumInstances := DEFAULT_INSTANCE_BUFFER - freeInstances

		activeImageID := freeInstancesQuery.CloudInstanceInfo[0].ImageID
		// Try scale up instances to match buffer size
		s.ScaleUpIfNecessary(wantedNumInstances, scalingCtx, host, event, string(activeImageID))
	}

	logger.Info("Scaling down %v free instances.", freeInstances)

	drainMutation := &subscriptions.UpdateInstanceStatus
	mutationParams := map[string]interface{}{
		"status": graphql.String("DRAINING"),
	}

	for _, instance := range freeInstancesQuery.CloudInstanceInfo {
		err := s.GraphQLClient.Mutate(scalingCtx, drainMutation, mutationParams)
		if err != nil {
			logger.Errorf("Failed to mark instance %v as draining.", instance)
		}

		logger.Info("Marked instance %v as draining on database.", instance)
		// s.VerifyInstanceScaleDown(scalingCtx, host, event, si)
	}

	return nil
}

func (s *DefaultScalingAlgorithm) ScaleUpIfNecessary(instancesToScale int, scalingCtx context.Context, host hosts.HostHandler, event ScalingEvent, imageID string) error {
	// Try scale up in given region
	instanceNum := int32(instancesToScale)
	createdInstances, err := host.SpinUpInstances(&instanceNum, imageID)

	if err != nil {
		return utils.MakeError("Failed to spin up instances, created %v, err: %v", createdInstances, err)
	}

	// Wait for instances to be ready on cloud provider
	for _, createdInstance := range createdInstances {
		err = s.Host.WaitForInstanceReady(scalingCtx, createdInstance)

		if err != nil {
			logger.Warningf("Failed to wait for instance %v to be ready. Err: %v", createdInstance.Name, err)
			continue
		}
	}

	// Check if we could create the desired number of instances
	if len(createdInstances) != instancesToScale {
		return utils.MakeError("Could not scale up %v instances, only scaled up %v.", instancesToScale, len(createdInstances))
	}

	logger.Infof("Inserting newly created instances to database.")

	// If successful, write to db
	insertMutation := &subscriptions.InsertInstances
	mutationParams := map[string]interface{}{
		"instance": createdInstances,
	}

	err = s.GraphQLClient.Mutate(scalingCtx, insertMutation, mutationParams)
	if err != nil {
		logger.Errorf("Failed to insert instances into database")
	}

	logger.Infof("Inserted %v rows to database.", insertMutation.MutationResponse.AffectedRows)

	return nil
}

func (s *DefaultScalingAlgorithm) UpgradeImage(scalingCtx context.Context, host hosts.HostHandler, event ScalingEvent, image subscriptions.Image) error {
	// create instance buffer with new image

	// wait for buffer to be ready

	// drain instances with old image

	// swapover active image on database
	return nil
}
