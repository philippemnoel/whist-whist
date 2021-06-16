from typing import Tuple
from flask import current_app, jsonify

import stripe

from app.constants.http_codes import (
    BAD_REQUEST,
    SUCCESS,
)


def get_billing_portal_url(customer_id: str, return_url: str) -> Tuple[str, int]:
    """
    Returns the url to a customer's billing portal.

    Args:
        customer_id (str): the stripe id of the user
        return_url (str): the url to redirect to upon leaving the billing portal

    Returns:
        json, int: JSON containing billing url and status code
    """
    try:
        billing_session = stripe.billing_portal.Session.create(
            customer=customer_id, return_url=return_url
        )
        return jsonify({"url": billing_session.url}), SUCCESS
    except Exception as e:
        return jsonify({"error": {"message": str(e)}}), BAD_REQUEST
