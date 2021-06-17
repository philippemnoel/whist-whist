from concurrent.futures import ThreadPoolExecutor
from json import loads


import requests
from flask import Blueprint, current_app
from sqlalchemy import or_
import click

from app.models import db, RegionToAmi, InstanceInfo
from app.helpers.blueprint_helpers.aws.aws_instance_post import do_scale_up_if_necessary
from app.helpers.blueprint_helpers.aws.aws_instance_state import _poll
from app.helpers.utils.general.sql_commands import fractal_sql_commit
from app.constants.instance_state_values import (
    ACTIVE,
    PRE_CONNECTION,
    DRAINING,
    HOST_SERVICE_UNRESPONSIVE,
)

command_bp = Blueprint("command", __name__)


def _insert_disabled_amis(client_commit_hash, region_to_ami_id_mapping):
    new_disabled_amis = []
    for region_name, ami_id in region_to_ami_id_mapping.items():
        new_ami = RegionToAmi(
            region_name=region_name,
            ami_id=ami_id,
            client_commit_hash=client_commit_hash,
            enabled=False,
            allowed=True,
        )
        new_disabled_amis.append(new_ami)
    db.session.commit()
    return new_disabled_amis


def upgrade_region(region_name, ami_id):
    # TODO: Right now buffer seems to be 1 instance if it is the first of its kind(AMI),
    #       Probably move this to a config.
    force_buffer = 1
    new_instances = do_scale_up_if_necessary(region_name, ami_id, force_buffer)
    for new_instance in new_instances:
        _poll(new_instance.instance_name)


@command_bp.cli.command("ami_upgrade")
@click.argument("client_commit_hash")
@click.argument("region_to_ami_id_mapping_str")
def ami_upgrade(
    client_commit_hash: str,
    region_to_ami_id_mapping_str: str,
):
    region_to_ami_id_mapping = loads(region_to_ami_id_mapping_str)

    new_disabled_amis = _insert_disabled_amis(client_commit_hash, region_to_ami_id_mapping)

    with ThreadPoolExecutor(max_workers=10) as thread_pool_executor:
        thread_pool_executor.map(upgrade_region, region_to_ami_id_mapping.items())

    active_instances = (
        db.session.query(InstanceInfo)
        .filter(or_(InstanceInfo.status.like(ACTIVE), InstanceInfo.status.like(PRE_CONNECTION)))
        .all()
    )

    for active_instance in active_instances:
        try:
            base_url = f"http://{active_instance.ip}/{current_app.config['HOST_SERVICE_PORT']}"
            requests.post(f"{base_url}/drain_and_shutdown")
            active_instance.status = DRAINING
        except requests.exceptions.RequestException:
            active_instance.status = HOST_SERVICE_UNRESPONSIVE

    for new_disabled_ami in new_disabled_amis:
        new_disabled_ami.enabled = True

    db.session.commit()
