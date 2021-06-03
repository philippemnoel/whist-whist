from flask import Blueprint
from flask_jwt_extended import jwt_required

from app import fractal_pre_process
from app.constants.http_codes import BAD_REQUEST
from app.helpers.utils.general.limiter import limiter, RATE_LIMIT_PER_MINUTE

from payments.stripe_helpers import (
    get_checkout_id,
    get_billing_portal_url,
)

stripe_bp = Blueprint("stripe_bp", __name__)


@stripe_bp.route("/stripe/create_checkout_session", methods=["POST"])
@limiter.limit(RATE_LIMIT_PER_MINUTE)
@fractal_pre_process
@jwt_required()
def create_checkout_session(**kwargs):
    """
    Returns checkout session id from a given product and customer

    Args:
        customer_id (str): the stripe id of the user
        success_url (str): url to redirect to upon completion success
        cancel_url (str): url to redirect to upon cancelation

    Returns:
        json, int: JSON containing session id and status code. The JSON is in the format
            {
                "session_id": the checkout session id
            }
                or

            {
                "error": {
                    "message": error message
                }
            }
                if the creation fails
    """
    body = kwargs["body"]
    try:
        customer_id = body["customer_id"]
        success_url = body["success_url"]
        cancel_url = body["cancel_url"]
    except:
        return {"error": "The request body is incorrectly formatted."}, BAD_REQUEST

    # TODO: use jwt _+ Auth0 to figure out a user's stripe customer_id

    return get_checkout_id(success_url, cancel_url, customer_id)


@stripe_bp.route("/stripe/customer_portal", methods=["POST"])
@limiter.limit(RATE_LIMIT_PER_MINUTE)
@fractal_pre_process
@jwt_required()
def customer_portal(**kwargs):
    """
    Returns billing portal url.

    Args:
        customer_id (str): the stripe id of the user
        return_url (str): the url to redirect to upon leaving the billing portal

    Returns:
        json, int: JSON containing billing url and status code. The JSON is in the format
            {
                "url": the billing portal url
            }
                or

            {
                "error": {
                    "message": error message
                }
            }
                if the creation fails
    """

    body = kwargs["body"]
    try:
        customer_id = body["customer_id"]
        return_url = body["return_url"]
    except:
        return {"error": "The request body is incorrectly formatted."}, BAD_REQUEST

    # TODO: use jwt _+ Auth0 to figure out a user's stripe customer_id

    return get_billing_portal_url(customer_id, return_url)
