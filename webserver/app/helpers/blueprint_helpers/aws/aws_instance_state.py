import time

from app.database.models.cloud import (
    db,
    InstanceInfo,
)

from app.helpers.utils.general.logs import fractal_logger
from app.constants.mandelbox_host_states import MandelboxHostState

MAX_POLL_TIME = 900  # seconds
POLL_SLEEP_INTERVAL = 5  # seconds
MAX_POLL_ITERATIONS = MAX_POLL_TIME // POLL_SLEEP_INTERVAL


def _poll(instance_name: str) -> bool:
    """Poll the database until the web server receives its first ping from the new instance.
    Time out after MAX_POLL_ITERATIONS seconds.
    This may be an appropriate use case for Hasura subscriptions.
    This function should patched to immediately return True in order to get CI to pass.
    Arguments:
        instance_name: The cloud provider ID of the instance whose state to poll.
    Returns:
        True if and only if the instance's starts with ACTIVE by the end of the polling period.
    """

    instance = InstanceInfo.query.get(instance_name)
    result = False

    for i in range(MAX_POLL_ITERATIONS):
        if instance.status != str(MandelboxHostState.ACTIVE.value):
            fractal_logger.warning(
                f"{instance.instance_name} deployment in progress. {i}/{MAX_POLL_ITERATIONS}"
            )
            time.sleep(POLL_SLEEP_INTERVAL)
            db.session.refresh(instance)
        else:
            result = True
            break

    return result
