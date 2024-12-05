package scaling_algorithms

import (
	"context"
	"net"
	"time"

	hashicorp "github.com/hashicorp/go-version"
	"github.com/whisthq/whist/backend/services/httputils"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/scaling-service/assign"
	"github.com/whisthq/whist/backend/services/scaling-service/config"
	"github.com/whisthq/whist/backend/services/scaling-service/scaling_algorithms/helpers"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
	"go.uber.org/zap"
)

// MandelboxAssign is the action responsible for assigning an instance to a user,
// and scaling as necessary to satisfy demand.
func (s *DefaultScalingAlgorithm) MandelboxAssign(scalingCtx context.Context, event ScalingEvent) error {
	contextFields := []interface{}{
		zap.String("id", event.ID),
		zap.Any("type", event.Type),
		zap.String("scaling_region", event.Region),
	}
	logger.Infow("Starting mandelbox assign action.", contextFields)
	defer logger.Infow("Finished mandelbox assign action.", contextFields)

	// We want to verify if we have the desired capacity after assigning a mandelbox
	defer func() {
		err := s.VerifyCapacity(scalingCtx, event)
		if err != nil {
			logger.Errorf("error verifying capacity when assigning mandelbox: %s", err)
		}
	}()

	mandelboxRequest := event.Data.(*httputils.MandelboxAssignRequest)

	// This is necessary so that the request is always closed
	// when encountering an error in the scaling action.
	var serviceUnavailable bool = true
	defer func() {
		if serviceUnavailable {
			mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
				Error: SERVICE_UNAVAILABLE,
			}, nil)
		}
	}()

	// Note: we receive the email from the client, so its value should
	// not be trusted for anything else other than logging since
	// it can be spoofed. We sanitize the email before using to help mitigate
	// potential attacks.
	unsafeEmail, err := helpers.SanitizeEmail(mandelboxRequest.UserEmail)
	if err != nil {
		return err
	}
	logger.Infof("Frontend reported email %s, this value might not be accurate and is untrusted.", unsafeEmail)

	// Append user email to logging context for better debugging.
	contextFields = append(contextFields, zap.String("user", unsafeEmail))

	shouldAllocateMandelbox, err := assign.CheckForExistingMandelbox(scalingCtx, s.DBClient, s.GraphQLClient, string(mandelboxRequest.UserID))
	if err != nil {
		return utils.MakeError("failed to get mandelboxes from database: %s", err)
	}

	// Before proceeding with the assign process, find out if the user already has other mandelboxes
	// allocated.
	if !shouldAllocateMandelbox {
		serviceUnavailable = false
		err := utils.MakeError("user %s already has mandelboxes allocated or running, so not assigning more mandelboxes", unsafeEmail)
		mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
			Error: USER_ALREADY_ACTIVE,
		}, err)
		return err
	}

	var (
		requestedRegions   = mandelboxRequest.Regions // This is a list of the regions requested by the frontend, in order of proximity.
		availableRegions   []string                   // List of regions that are enabled and match the regions in the request.
		unavailableRegions []string                   // List of regions that are not enabled but are present in the request.
		assignedInstance   subscriptions.Instance     // The instance that is assigned to the user.
	)

	// The number of elements  to truncate a slice of regions to. Used when logging unavailable region errors.
	const truncateTo = 3

	// Populate availableRegions
	for _, requestedRegion := range requestedRegions {
		var regionFound bool
		for _, enabledRegion := range config.GetEnabledRegions() {
			if enabledRegion == requestedRegion {
				availableRegions = append(availableRegions, requestedRegion)
				regionFound = true
			}
		}

		if !regionFound {
			unavailableRegions = append(unavailableRegions, requestedRegion)
		}
	}

	// This means that the user has requested access to some regions that are not yet enabled,
	// but could still be allocated to a region that is relatively close.
	if len(unavailableRegions) != 0 && len(unavailableRegions) != len(requestedRegions) {
		logger.Warningf("User %s requested access to the following unavailable regions: %s", unsafeEmail, utils.PrintSlice(unavailableRegions, truncateTo))
	}

	// The user requested access to only unavailable regions. This means the user is far from
	// any of the available regions, and the frontend should handle that accordingly.
	if len(unavailableRegions) == len(requestedRegions) {
		serviceUnavailable = false
		err := utils.MakeError("user %s requested access to only unavailable regions: %s", unsafeEmail, utils.PrintSlice(unavailableRegions, truncateTo))
		mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
			Error: REGION_NOT_ENABLED,
		}, err)
		return err
	}

	// TODO: set different cloud provider when doing multi-cloud
	latestImage, err := s.DBClient.QueryLatestImage(scalingCtx, s.GraphQLClient, "AWS", event.Region)
	if err != nil {
		return err
	}

	// This condition is to accomodate the workflow for developers of the Whist frontend
	// to test their changes without needing to update the development database with
	// commit_hashes on their local machines.
	if metadata.IsLocalEnv() || mandelboxRequest.CommitHash == CLIENT_COMMIT_HASH_DEV_OVERRIDE {
		mandelboxRequest.CommitHash = string(latestImage.ClientSHA)
	}

	var (
		instanceFound          bool
		instanceProximityIndex int
	)

	// This is the "main" loop that does all the work and tries to find an instance for a user. first, it will iterate
	// over the list of regions provided on the request, and will query the database on each to return the list of
	// instances with capacity on the current region. Once it gets the instances, it will iterate over them and try
	// to find an instance with a matching commit hash. If it fails to do so, move on to the next region.
	for i, region := range availableRegions {
		assignContext := contextFields
		assignContext = append(assignContext, zap.String("assign_region", region))

		// TODO: set different cloud provider when doing multi-cloud
		regionImage, err := s.DBClient.QueryLatestImage(scalingCtx, s.GraphQLClient, "AWS", region)
		if err != nil {
			return utils.MakeError("failed to query latest image in %s: %s", region, err)
		}

		logger.Infow(utils.Sprintf("Trying to find instance in region %s, with commit hash %s and image %s",
			region, mandelboxRequest.CommitHash, regionImage.ImageID), assignContext)

		instanceResult, err := s.DBClient.QueryInstanceWithCapacity(scalingCtx, s.GraphQLClient, region)
		if err != nil {
			return utils.MakeError("failed to query for instances with capacity in %s: %s", region, err)
		}

		if len(instanceResult) == 0 {
			logger.Warningw(utils.Sprintf("Failed to find any instances with capacity in %s. Trying on next region", region), assignContext)
			continue
		}

		// Iterate over available instances, try to find one with a matching commit hash and image.
		for i := range instanceResult {
			assignedInstance = instanceResult[i]

			if assignedInstance.ClientSHA == mandelboxRequest.CommitHash &&
				assignedInstance.ImageID == regionImage.ImageID {
				logger.Infow(utils.Sprintf("Found instance %s with commit hash %s", assignedInstance.ID, assignedInstance.ClientSHA), assignContext)
				instanceFound = true
				break
			}
			logger.Warningw(utils.Sprintf("Found an instance in %s but it has a different commit hash %s. Trying on next region", region, assignedInstance.ClientSHA), assignContext)
		}

		// Break of outer loop if instance was found. If no instance with
		// matching commit hash was found, move on to the next region.
		if instanceFound {
			// Persist the assigned region context
			contextFields = assignContext
			instanceProximityIndex = i
			break
		}

		logger.Infow(utils.Sprintf("No instances found in %s with commit hash %s", region, mandelboxRequest.CommitHash), assignContext)
	}

	// No instances with capacity were found
	if assignedInstance == (subscriptions.Instance{}) {
		serviceUnavailable = false
		err := utils.MakeError("did not find an instance with capacity for user %s and commit hash %s", mandelboxRequest.UserEmail, mandelboxRequest.CommitHash)
		mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
			Error: NO_INSTANCE_AVAILABLE,
		}, err)
		return err
	}

	// If the index of the region we assigned is not the first, it means the
	// user was not assigned to the closest available region (since the slice
	// is already sorted by proximity).
	if instanceProximityIndex > 0 {
		logger.Errorw(utils.Sprintf("failed to assign user to closest region: assigned to %s instead of %s (region %d of %d)",
			assignedInstance.Region, availableRegions[0], instanceProximityIndex+1, len(availableRegions)), contextFields)
	} else {
		logger.Infow(utils.Sprintf("Successfully assigned user to closest requested region %s", assignedInstance.Region), contextFields)
	}

	var (
		// The parsed version from the config
		parsedFrontendVersion *hashicorp.Version
		// The parsed version from the request
		parsedRequestVersion *hashicorp.Version
		// whether the frontend has an outdated version
		isOutdatedFrontend bool
	)

	// Get the version we keep locally for comparing the incoming request value.
	frontendVersion := config.GetFrontendVersion()

	// Parse the version with the `hashicorp/go-version` package so we can compare.
	parsedFrontendVersion, err = hashicorp.NewVersion(frontendVersion)
	if err != nil {
		logger.Errorf("failed parsing frontend version from scaling algorithm config: %s", err)
	}

	// Parse the version we got in the request.
	if mandelboxRequest.Version == "" {
		logger.Warningf("Request version is empty, this is caused by frontend testing and development.")
	} else {
		parsedRequestVersion, err = hashicorp.NewVersion(mandelboxRequest.Version)
		if err != nil {
			logger.Errorf("failed parsing frontend version from request: %s", err)
		}
	}

	// Compare the request version with the one from the config. If the
	// version from the request is less than the one we have locally, it
	// means the request comes from an outdated frontend application.
	if parsedFrontendVersion != nil && parsedRequestVersion != nil {
		logger.Infow(utils.Sprintf("Local version is %s, version received from request is %s", parsedFrontendVersion.String(), parsedRequestVersion.String()), contextFields)
		isOutdatedFrontend = parsedRequestVersion.LessThan(parsedFrontendVersion)
	}

	// There are instances with capacity available, but none of them with the desired commit hash.
	// We only consider this error in cases when the frontend has a version greater or equal than
	// the one in the config database.
	if assignedInstance.ClientSHA != mandelboxRequest.CommitHash {
		var msg error

		// Only log the commit mismatch error when running on prod. This is because we only update the full version
		// (major, minor, micro) on the config database when deploying to prod, so this is only a real error in that case.
		if metadata.GetAppEnvironment() == metadata.EnvProd && !isOutdatedFrontend {
			msg = utils.MakeError("found instance with capacity but different commit hash")
		}

		// Regardless if we log the error, its necessary to return an appropiate response.
		serviceUnavailable = false
		mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
			Error: COMMIT_HASH_MISMATCH,
		}, msg)
		return msg
	}

	// Try to find a mandelbox in the WAITING status in the assigned instance.
	mandelboxResult, err := s.DBClient.QueryMandelbox(scalingCtx, s.GraphQLClient, assignedInstance.ID, "WAITING")
	if err != nil {
		return utils.MakeError("failed to query database for mandelbox on instance %s: %s", assignedInstance.ID, err)
	}

	if len(mandelboxResult) == 0 {
		return utils.MakeError("failed to find a waiting mandelbox even though the instance %s had sufficient capacity", assignedInstance.ID)
	}

	waitingMandelbox := mandelboxResult[0]
	if err != nil {
		return utils.MakeError("failed to parse mandelbox id for instance %s: %s", assignedInstance.ID, err)
	}

	mandelboxForDb := subscriptions.Mandelbox{
		ID:         waitingMandelbox.ID,
		App:        string(waitingMandelbox.App),
		InstanceID: assignedInstance.ID,
		UserID:     mandelboxRequest.UserID,
		SessionID:  waitingMandelbox.SessionID,
		Status:     "ALLOCATED",
		CreatedAt:  waitingMandelbox.CreatedAt,
		UpdatedAt:  time.Now(),
	}

	// Allocate mandelbox on database
	affectedRows, err := s.DBClient.UpdateMandelbox(scalingCtx, s.GraphQLClient, mandelboxForDb)
	if err != nil {
		return utils.MakeError("error while inserting mandelbox to database: %s", err)
	}

	logger.Infow(utils.Sprintf("Inserted %d rows to database.", affectedRows), contextFields)

	if assignedInstance.RemainingCapacity <= 0 {
		// This should never happen, but we should consider
		// possible edge cases before updating the database.
		return utils.MakeError("instance with id %s has a remaning capacity less than or equal to 0", assignedInstance.ID)
	}

	// Subtract 1 from the current instance capacity because we allocated a mandelbox
	updatedCapacity := assignedInstance.RemainingCapacity - 1
	assignedInstance.RemainingCapacity = updatedCapacity

	affectedRows, err = s.DBClient.UpdateInstance(scalingCtx, s.GraphQLClient, assignedInstance)
	if err != nil {
		return utils.MakeError("error while updating instance capacity on database: %s", err)
	}

	logger.Infow(utils.Sprintf("Updated %d rows in database.", affectedRows), contextFields)

	// Parse IP address. The database uses the CIDR notation (192.0.2.0/24)
	// so we need to extract the address and send it to the frontend.
	ip, _, err := net.ParseCIDR(assignedInstance.IPAddress)
	if err != nil {
		return utils.MakeError("failed to parse IP address %s: %s", assignedInstance.IPAddress, err)
	}

	serviceUnavailable = false
	// Return result to assign request
	mandelboxRequest.ReturnResult(httputils.MandelboxAssignRequestResult{
		IP:          ip.String(),
		MandelboxID: types.MandelboxID(waitingMandelbox.ID),
	}, nil)

	return nil
}
