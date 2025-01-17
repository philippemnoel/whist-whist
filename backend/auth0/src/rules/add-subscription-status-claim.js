/* add-subscription-status-claim.js
 *
 * Add the status of the authenticated user's Whist Stripe subscription as the
 * value of the custom https://api.fractal.co/subscription_status access token
 * claim.
 *
 * The subscription status is recomputed every time a new access token is
 * issued. In a worst case scenario, it is possible for the user's subscription
 * to expire before their access token does. This would allow them to continue
 * using Whist for up to one access token lifetime, which is twenty-four
 * hours.
 */

async function addSubscriptionStatusClaim(user, context, callback) {
  const app_metadata = user.app_metadata || {};
  const stripe_customer_id = app_metadata.stripe_customer_id || null;
  const stripe = require("stripe@8.126.0")(configuration.STRIPE_API_KEY);

  if (app_metadata.stripe_customer_id) {
    const subscriptions = await stripe.subscriptions
      .list({
        customer: stripe_customer_id,
      });
    
    const subscription = subscriptions.data
    .sort((left, right) => left.created - right.created)
    .pop();
    
    let status = subscription ? subscription.status : null;
    
    context.accessToken[
      "https://api.fractal.co/subscription_status"
    ] = status;

    return callback(null, user, context);
  } else return callback(new Error("Missing Stripe customer ID"), user, context);
}
