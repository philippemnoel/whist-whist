package payments

import (
	"github.com/stripe/stripe-go/v72"
	billingPortal "github.com/stripe/stripe-go/v72/billingportal/session"
	checkout "github.com/stripe/stripe-go/v72/checkout/session"
	"github.com/stripe/stripe-go/v72/price"
	"github.com/stripe/stripe-go/v72/sub"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

type WhistStripeClient interface {
	configure(string, string, string, string, int64)
	getSubscriptionStatus() string
	createCheckoutSession() (string, error)
	createBillingPortal() (string, error)
	createPrice(cents int64, name string, interval string) (*stripe.Price, error)
}

// StripeClient interacts directly with the official
// Stripe client.
type StripeClient struct {
	key                 string // The secret key to authenticate calls to the Stripe API
	customerID          string // The Stripe ID of the customer
	subscriptionStatus  string // The subscription status ("active", "trialing", etc.)
	monthlyPriceInCents int64  // The desired price in cents for a Whist subscription
}

// configure will set the fields of the StripeClient to the received values. This method
// makes it easier to mock for testing.
func (sc *StripeClient) configure(secret string, restrictedSecret string, customerID string, subscriptionStatus string, monthlyPriceInCents int64) {
	// Dynamically set the Stripe key depending on environment
	if metadata.IsLocalEnv() || metadata.IsRunningInCI() {
		sc.key = restrictedSecret
	} else {
		sc.key = secret
	}

	stripe.Key = sc.key
	sc.customerID = customerID
	sc.subscriptionStatus = subscriptionStatus
	sc.monthlyPriceInCents = monthlyPriceInCents
}

// getSubscriptionStatus will try to get the current subscription status for the customer.
// If it fails to obtain it, default to the subscription status included in the access token.
func (sc *StripeClient) getSubscriptionStatus() string {
	subscriptions := sub.List(&stripe.SubscriptionListParams{
		Customer: sc.customerID,
	})

	var subscription *stripe.Subscription
	for subscriptions.Next() {
		subscription = subscriptions.Subscription()
	}
	if subscriptions.Err() != nil || subscription == nil {
		logger.Warningf("Failed to get subscription for customer %v. Defaulting to access token status %v. Err: %v", sc.customerID, sc.subscriptionStatus, subscriptions.Err())
	} else {
		logger.Infof("Found subscription %v for customer %v with status %v.", subscription, sc.customerID, subscription.Status)
		sc.subscriptionStatus = string(subscription.Status)
	}

	return sc.subscriptionStatus
}

// createCheckoutSession creates a Stripe checkout session for the current customer.
// It applies the desired price from the database, and if it doesn't exist in Stripe,
//  creates it.
func (sc *StripeClient) createCheckoutSession() (string, error) {
	priceList := price.List(&stripe.PriceListParams{
		Active: stripe.Bool(true),
	})

	var priceID string

	// Try to find a Stripe Price with the desired monthly price.
	for priceList.Next() {
		currentPrice := priceList.Current().(*stripe.Price)
		if currentPrice.UnitAmount == sc.monthlyPriceInCents {
			priceID = currentPrice.ID
			break
		}
	}
	if priceList.Err() != nil {
		return "", utils.MakeError("failed to read price list from Stripe. Err: %v", priceList.Err())
	}

	// If a Stripe Price was not found, create one.
	if priceID != "" {
		logger.Infof("Found price of %v in Stripe.", sc.monthlyPriceInCents)
	} else {
		//Create new price
		newPrice, err := sc.createPrice(sc.monthlyPriceInCents, "Whist", "month")
		if err != nil {
			return "", utils.MakeError("failed to create new Stripe price. Err: %v", err)
		}
		priceID = newPrice.ID
	}

	// Set how many subscriptions we want started, and what number of
	// days we offer as a trial period.
	subscriptionQuantity := 1
	trialDays := int64(14)

	params := &stripe.CheckoutSessionParams{
		Customer: stripe.String(sc.customerID),
		LineItems: []*stripe.CheckoutSessionLineItemParams{
			{
				Price:    stripe.String(priceID),
				Quantity: stripe.Int64(int64(subscriptionQuantity)),
			},
		},
		SubscriptionData: &stripe.CheckoutSessionSubscriptionDataParams{
			TrialPeriodDays: stripe.Int64(trialDays),
		},
		PaymentMethodTypes: []*string{
			stripe.String("card"),
		},
		Mode:       stripe.String(string(stripe.CheckoutSessionModeSubscription)),
		SuccessURL: stripe.String("http://localhost/callback/payment?success=true"),
		CancelURL:  stripe.String("http://localhost/callback/payment?success=false"),
	}

	s, err := checkout.New(params)
	if err != nil {
		return "", utils.MakeError("failed to create checkout session. Err: %v", err)
	}

	return s.URL, nil
}

// createBillingPortal creates a Stripe billing portal for the current customer.
func (sc *StripeClient) createBillingPortal() (string, error) {
	s, err := billingPortal.New(&stripe.BillingPortalSessionParams{
		Customer:  stripe.String(sc.customerID),
		ReturnURL: stripe.String("http://localhost/callback/payment"),
	})

	return s.URL, err
}

// createPrice is a helper function that creates a new Price object in Stripe
// with the desired price (in cents), for the product `name` in the given interval.
func (sc *StripeClient) createPrice(cents int64, name string, interval string) (*stripe.Price, error) {
	return price.New(&stripe.PriceParams{
		UnitAmount: stripe.Int64(cents),
		Currency:   stripe.String("usd"),
		ProductData: &stripe.PriceProductDataParams{
			Name: stripe.String(name),
		},
		Recurring: &stripe.PriceRecurringParams{
			Interval: stripe.String(interval),
		},
	})
}
