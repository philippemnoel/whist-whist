from flask import Blueprint, jsonify

from app import fractalPreProcess
from app.constants.http_codes import NOT_FOUND
from app.helpers.blueprint_helpers.auth.google_auth_post import (
    loginHelper,
    reasonHelper,
)

google_auth_bp = Blueprint("google_auth_bp", __name__)


@google_auth_bp.route("/google/<action>", methods=["POST"])
@fractalPreProcess
def google_auth_post(action, **kwargs):
    if action == "login":
        code = kwargs["body"]["code"]

        clientApp = False
        if "clientApp" in kwargs["body"].keys():
            clientApp = kwargs["body"]["clientApp"]

        output = loginHelper(code, clientApp)

        return jsonify(output), output["status"]

    if action == "reason":
        username, reason_for_signup = (kwargs["body"]["username"], kwargs["body"]["reason"])

        output = reasonHelper(username, reason_for_signup)

        return jsonify(output), output["status"]

    return jsonify({"error": "Not Found"}), NOT_FOUND
