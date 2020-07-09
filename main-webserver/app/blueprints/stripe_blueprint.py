from app import *
from app.helpers.blueprint_helpers.stripe_post import *

stripe_bp = Blueprint("stripe_bp", __name__)

@stripe_bp.route("/stripe/<action>", methods=["POST"])
@fractalPreProcess
def payment(action, **kwargs):
    body=kwargs["body"]

    # Adds a subscription to the customer
    if action == "charge":
        body = request.get_json()
        return chargeHelper(body["token"], body["email"], body["code"], body["plan"])

    # Retrieves the stripe subscription of the customer
    elif action == "retrieve":
        return retrieveStripeHelper(body["email"])

    # Cancel a stripe subscription
    elif action == "cancel":
        return cancelStripeHelper(body["email"])

    elif action == "discount":
        return discountHelper(body["code"])

    # Inserts a customer to the table
    elif action == "insert":
        return insertCustomerHelper(body["email"], body["location"])

    # Endpoint for stripe webhooks
    elif action == "hooks":
        sigHeader = request.headers["Stripe-Signature"]
        endpointSecret = os.getenv("ENDPOINT_SECRET")
        event = None

        try:
            event = stripe.Webhook.construct_event(body, sigHeader, endpointSecret)
        except ValueError as e:
            # Invalid payload
            return jsonify({"status": "Invalid payload"}), 400
        except stripe.error.SignatureVerificationError as e:
            # Invalid signature
            return jsonify({"status": "Invalid signature"}), 400

        return webhookHelper(event)
        
    elif action == "update" and request.method == "POST":
        # When a customer requests to change their plan type
        return updateHelper(body["username"], body["plan"])


# REFERRAL endpoint
@stripe_bp.route("/referral/<action>", methods=["POST"])
@jwt_required
@fractalPreProcess
def referral(action, **kwargs):
    body = kwargs["body"]
    if action == "validate":
        return validateReferralHelper(body["code"], body["username"])
