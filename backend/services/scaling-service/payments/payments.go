/*
Package payments includes all types and functions necessary to validate
payments using Stripe, and create Stripe checkout sessions.
*/
package payments

import (
	"context"
	"os"
	"strconv"

	"github.com/whisthq/whist/backend/services/host-service/auth"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/scaling-service/dbclient"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// PaymentsClient is a wrapper struct that will call our
// own Stripe client and allow testing. Packages that handle
// payments should use this struct.
type PaymentsClient struct {
	stripeClient WhistStripeClient
}

// Initialize will pull all necessary configurations from the database
// and set the StripeClient fields with the values extracted from
// the access token.
func (whistPayments *PaymentsClient) Initialize(customerID string, subscriptionStatus string, configGraphqlClient subscriptions.WhistGraphQLClient, stripeClient WhistStripeClient) error {
	// If running on local environment, get the Stripe key from an environment
	// variable, and use the default configurations. By doing this, we avoid
	// hardcoding the key or having to query the config database, since they
	// are disabled on localdev environment.
	if metadata.IsLocalEnvWithoutDB() && !metadata.IsRunningInCI() {
		key := os.Getenv("STRIPE_KEY")
		monthlyPriceInCents := int64(2500)
		stripeClient.configure(key, key, customerID, subscriptionStatus, monthlyPriceInCents)
		whistPayments.stripeClient = stripeClient
		return nil
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Get configurations depending on environment
	var (
		configs map[string]string
		err     error
	)

	switch metadata.GetAppEnvironmentLowercase() {
	case string(metadata.EnvDev):
		configs, err = dbclient.GetDevConfigs(ctx, configGraphqlClient)
	case string(metadata.EnvStaging):
		configs, err = dbclient.GetStagingConfigs(ctx, configGraphqlClient)
	case string(metadata.EnvProd):
		configs, err = dbclient.GetProdConfigs(ctx, configGraphqlClient)
	default:
		configs, err = dbclient.GetDevConfigs(ctx, configGraphqlClient)
	}

	if err != nil {
		// Err is already wrapped here
		return err
	}

	// Verify that all necessary configurations are present
	secret, ok := configs["STRIPE_SECRET"]
	if !ok {
		return utils.MakeError("Could not find key STRIPE_SECRET in configurations map.")
	}

	// Use the restriced Stripe key if on localdev or testing
	restricedKey, ok := configs["STRIPE_RESTRICTED"]
	if !ok {
		logger.Warningf("Could not find key STRIPE_RESTRICTED in configurations map.")
	}

	price, ok := configs["MONTHLY_PRICE_IN_CENTS"]
	if !ok {
		return utils.MakeError("Could not find key MONTHLY_PRICE_IN_CENTS in configurations map.")
	}

	monthlyPrice, err := strconv.ParseInt(price, 10, 64)
	if err != nil {
		return utils.MakeError("failed to parse monthly price. Err: %v", err)
	}

	stripeClient.configure(secret, restricedKey, customerID, subscriptionStatus, monthlyPrice)
	whistPayments.stripeClient = stripeClient
	return nil
}

// CreateSession is the method exposed to other packages for getting a Stripe Session URL.
// It should only be called after `Initialize` has been run first. It decides what kind of
// Stripe Session to return based on the customer's subscription status.
func (whistPayments *PaymentsClient) CreateSession() (string, error) {
	var (
		status     string
		sessionUrl string
	)

	subscription, err := whistPayments.stripeClient.getSubscription()
	if err != nil {
		logger.Warningf("Failed to get subscription. Defaulting to access token subscription status.")
		status = whistPayments.stripeClient.getSubscriptionStatus()
	} else {
		status = string(subscription.Status)
	}

	isNewUser := whistPayments.stripeClient.isNewUser()

	if status == "active" || status == "trialing" {
		// If the authenticated user already has a Whist subscription in a non-terminal state
		// (one of `active` or `trialing`), create a Stripe billing portal that the customer
		// can use to manage their subscription and billing information.
		sessionUrl, err = whistPayments.stripeClient.createBillingPortal()
		if err != nil {
			return "", utils.MakeError("error creating Stripe billing portal. Err: %v", err)
		}
	} else if !isNewUser {
		// The authenticated user has previous Whist subscriptions. This means that the
		// user has already gone through the initial trial period.
		withTrialPeriod := false
		sessionUrl, err = whistPayments.stripeClient.createCheckoutSession(withTrialPeriod)
		if err != nil {
			return "", utils.MakeError("error creating Stripe billing portal. Err: %v", err)
		}
	} else {
		// The authenticated user does not have an active Whist subscription, so we offer
		// a free trial period.
		withTrialPeriod := true
		sessionUrl, err = whistPayments.stripeClient.createCheckoutSession(withTrialPeriod)
		if err != nil {
			return "", utils.MakeError("error creating Stripe checkout session. Err: %v", err)
		}
	}

	return sessionUrl, nil
}

// VerifyPayment takes an access token and extracts the claims within it.
// Once it has obtained the claims, it checks if the customer has an active
// Stripe subscription using the subscription status custom claim.
func VerifyPayment(accessToken string) (bool, error) {
	claims, err := auth.ParseToken(accessToken)
	if err != nil {
		return false, utils.MakeError("failed to parse access token. Err: %v", err)
	}

	status := claims.SubscriptionStatus
	if status == "" {
		return false, utils.MakeError("subscription status claim is empty.")
	}

	paymentValid := status == "active" || status == "trialing"

	return paymentValid, nil
}
