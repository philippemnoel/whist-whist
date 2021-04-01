import os

from flask import Blueprint
from flask.json import jsonify
from flask_jwt_extended import jwt_required

from app import fractal_pre_process
from app.constants.http_codes import SUCCESS, ACCEPTED, BAD_REQUEST, FORBIDDEN
from app.models import UserContainerState

host_service_bp = Blueprint("host_service_bp", __name__)


@host_service_bp.route("/host_service", methods=("GET",))
@jwt_required()
@fractal_pre_process
def host_service(**kwargs):
    """Return the user's host service info for app config security

    Returns:
        JSON of user's host service info
    """
    username = kwargs.pop("username")

    host_service_info = UserContainerState.query.filter_by(user_id=username)

    if not host_service_info:
        return jsonify(
            {
                "ip": None,
                "port": None,
                "client_app_auth_secret": None
            }
        )
    else:
        host_service_info = host_service_info[0]
        return jsonify(
            {
                "ip": host_service_info.ip,
                "port": host_service_info.port,
                "client_app_auth_secret": host_service_info.client_app_auth_secret,
            }
        )


@host_service_bp.route("/host_service/auth", methods=("POST",))
@fractal_pre_process
def host_service_auth(**kwargs):
    # pylint: disable=unused-variable
    body = kwargs.pop("body")
    address = kwargs.pop("received_from")

    try:
        instance_id = body.pop("InstanceID")
    except KeyError:
        return jsonify({"status": BAD_REQUEST}), BAD_REQUEST

    # TODO: replace with a real celery task taking in instance_id and ip address and eventually
    # writing an entry to the db with pkey instance_id and an authentication token
    # TODO: update ecs-host-service to wait for task success via /status/<task id> and then pull the
    # authentication token from the response. Do this in a separate goroutine
    auth_token = os.urandom(16).hex()
    already_happened = False

    if already_happened:
        return jsonify({"status": BAD_REQUEST}), BAD_REQUEST
    return jsonify({"AuthToken": auth_token}), SUCCESS


@host_service_bp.route("/host_service/heartbeat", methods=("POST",))
@fractal_pre_process
def host_service_heartbeat(**kwargs):
    # pylint: disable=unused-variable
    body = kwargs.pop("body")
    address = kwargs.pop("received_from")

    try:
        auth_token = body.pop("AuthToken")
        instance_id = body.pop("InstanceID")
    except KeyError:
        return jsonify({"status": BAD_REQUEST}), BAD_REQUEST

    # TODO: replace with a helper checking db for instance_id, address, auth_token
    valid_auth = True
    if not valid_auth:
        return jsonify({"status": FORBIDDEN}), FORBIDDEN

    try:
        timestamp = body.pop("Timestamp")
        heartbeat_number = body.pop("HeartbeatNumber")
        instance_type = body.pop("InstanceType")
        total_ram_kb = body.pop("TotalRAMinKB")
        free_ram_kb = body.pop("FreeRAMinKB")
        available_ram_kb = body.pop("AvailRAMinKB")
        dying_heartbeat = body.pop("IsDyingHeartbeat")
    except KeyError:
        return jsonify({"status": BAD_REQUEST}), BAD_REQUEST

    return jsonify({"status": ACCEPTED}), ACCEPTED
