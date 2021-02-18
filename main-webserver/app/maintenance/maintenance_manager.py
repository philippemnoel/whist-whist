import logging
import time
from functools import wraps
import inspect
import random
from typing import Callable, Tuple

from app.helpers.utils.general.logs import fractal_log
from app.models import RegionToAmi


_REDIS_LOCK_KEY = "WEBSERVER_REDIS_LOCK"
_REDIS_UPDATE_KEY = "WEBSERVER_UPDATE_{region_name}"
_REDIS_TASKS_KEY = "WEBSERVER_TASKS_{region_name}"


def check_if_maintenance(region_name: str) -> bool:
    """
    Check if an update is going on.

    Args:
        region_name: name of region to check for updates.

    Returns:
        True iff the web server is currently in maintenance mode.
    """
    from app import redis_conn

    update_key = _REDIS_UPDATE_KEY.format(region_name=region_name)
    # we don't need a lock because we are just checking if the key exists,
    # not modifying data. it is posisble for an update to start right after
    # the check happens; that is handled through task tracking and failing out
    # celery tasks that try to start during an update
    return redis_conn.exists(update_key)


def _get_lock(max_tries: int = 100, should_sleep: bool = True) -> bool:
    """
    Tries to get lock using redis SETNX command. Tries `max_tries` time with a
    sleep time chosen randomly between 0 and 1 seconds to improve lock contention.

    Args:
        max_tries: number of tries to get the lock
        should_sleep: True if method should sleep between attempts to grab lock

    Returns:
        True iff the lock is acquired.
    """
    from app import redis_conn

    for _ in range(max_tries):
        got_lock = redis_conn.setnx(_REDIS_LOCK_KEY, 1)
        if got_lock:
            return True
        if should_sleep:
            sleep_time = random.random()
            time.sleep(sleep_time)
    return False


def _release_lock():
    """
    Release the lock
    """
    from app import redis_conn

    redis_conn.delete(_REDIS_LOCK_KEY)


def try_start_update(region_name: str) -> Tuple[bool, str]:
    """
    Try to start an update. Steps:
        1. Grab the lock
        2. Set the update key if no tracked tasks are running
        3. Release lock
    In order to prevent excessive polling of this method, the region's update
    key will be created to prevent new tasks from being made. However, the
    webserver is not officially in update mode until all tasks in the region finish.
    The first element of the return will only be True if all tasks have finished.
    However, the implementation allows a client to call this function once to stop new tasks
    from being started. The client just needs to grab the lock once to do so.
    Then, after some polling, the return should become true.

    Args:
        region_name: name of region to start updating.

    Returns:
        (success, human_readable_msg)
        success: True if webserver in update mode, otherwise false
        human_readable_msg: explains exactly what has happened
    """
    from app import redis_conn

    update_key = _REDIS_UPDATE_KEY.format(region_name=region_name)
    tasks_key = _REDIS_TASKS_KEY.format(region_name=region_name)

    got_lock = _get_lock(10, should_sleep=False)  # only try 10 times since this runs synchronously
    if not got_lock:
        return False, "Did not get lock"

    # now we freely operate on the keys. we need to make sure no tasks are going
    success = False
    return_msg = None
    tasks = redis_conn.lrange(tasks_key, 0, -1)
    if tasks is None or not tasks:
        # no tasks, we can start the update
        success = True
        redis_conn.set(update_key, 1)

        fractal_log(
            "try_start_update",
            None,
            f"putting webserver into maintenance mode in region {region_name}",
        )
        return_msg = f"Put webserver into maintenance mode in region {region_name}."

    else:
        # no tasks, we cannot start the update. However, we set update_key so no new tasks start.
        success = False
        redis_conn.set(update_key, 1)

        log = (
            "cannot start update, but stopping new tasks from running."
            f" Waiting on {len(tasks)} task to finish. Task IDs:\n{tasks}."
        )
        fractal_log(
            "try_start_update",
            None,
            log,
        )
        return_msg = (
            f"There are {len(tasks)} tasks currently running."
            f" However, all new tasks in region {region_name} will not run."
            " Please poll until these tasks finish."
        )

    _release_lock()
    return success, return_msg


def try_start_update_all() -> Tuple[bool, str]:
    """
    Tries to start an update in all regions. Mainly works around try_start_update.

    Returns:
        (success, human_readable_msg)
        success: True iff ALL regions are in update mode
        human_readable_msg: explains exactly what has happened
    """
    region_to_ami = RegionToAmi.query.all()
    all_regions = [region.region_name for region in region_to_ami]
    pending_regions = []
    for region in all_regions:
        success, _ = try_start_update(region)
        if not success:
            pending_regions.append(region)
    if not pending_regions:
        fractal_log("try_start_update_all", None, "Started global update.")
        return True, "All regions put in maintenance mode"
    else:
        fractal_log(
            "try_start_update_all",
            None,
            f"Cannot do global update. Waiting on tasks in {pending_regions}.",
        )
        return False, f"Waiting on the following regions: {pending_regions}"


def try_end_update(region_name: str) -> bool:
    """
    Try to end an update. Steps:
        1. Grab the lock
        2. Delete the update key
        3. Release lock

    Args:
        region_name: name of region to end updating

    Returns:
        True iff there is no update in the region. This can be because there
        already was no update, or because the update was ended by this function.
    """
    from app import redis_conn

    update_key = _REDIS_UPDATE_KEY.format(region_name=region_name)
    got_lock = _get_lock(10, should_sleep=False)  # only try 10 times since this runs synchronously
    if not got_lock:
        return False, "Did not get lock"

    #  now we freely operate on the keys. first make sure we are not already in update mode
    # then, we need to make sure no tasks are going
    update_exists = redis_conn.exists(update_key)
    if not update_exists:
        # release lock
        redis_conn.delete(_REDIS_LOCK_KEY)
        # even if no update exists, we return True because try_update_all does not cache
        # which regions have stopped updating. A second request to it will try to end updates
        # in all regions, including ones the first request stopped.
        return True, f"No update in {region_name}"

    fractal_log(
        "try_end_update",
        None,
        f"ending webserver maintenance mode on region {region_name}",
    )

    redis_conn.delete(update_key)
    _release_lock()

    return True, f"Ended update in {region_name}"


def try_end_update_all() -> Tuple[bool, str]:
    """
    Tries to start an update in all regions. Mainly works around try_start_update.

    Returns:
        (success, human_readable_msg)
        success: True iff ALL regions are in update mode
        human_readable_msg: explains exactly what has happened
    """
    region_to_ami = RegionToAmi.query.all()
    all_regions = [region.region_name for region in region_to_ami]
    pending_regions = []
    for region in all_regions:
        success, _ = try_end_update(region)
        if not success:
            pending_regions.append(region)
    if not pending_regions:
        fractal_log("try_end_update_all", None, "Global maintenance mode ended.")
        return True, "Global maintenance mode ended."
    else:
        fractal_log(
            "try_end_update_all", None, f"Cannot end global update. Waiting on {pending_regions}."
        )
        return False, f"Waiting on the following regions: {pending_regions}."


def try_register_task(region_name: str, task_id: int) -> bool:
    """
    Try to register a tracked task. Steps:
        1. Grab the lock
        2. Check if an update is going on; if so return false
        3. Add task_id to the tasks list
        3. Release lock

    Args:
        region_name: name of region to start updating
        task_id: celery task that is being tracked

    Returns:
        True if registered, False if lock was not acquired or an update is happening
    """
    from app import redis_conn

    update_key = _REDIS_UPDATE_KEY.format(region_name=region_name)
    tasks_key = _REDIS_TASKS_KEY.format(region_name=region_name)

    got_lock = _get_lock(100)
    if not got_lock:
        return False

    success = False
    # now we freely operate on the keys. first check if an update is in progress. if so,
    # the task does not get registered. otherwise, register it.
    update_exists = redis_conn.exists(update_key)
    if update_exists:
        success = False
    else:
        redis_conn.rpush(tasks_key, task_id)
        success = True

    _release_lock()
    return success


def try_deregister_task(region_name: str, task_id: int) -> bool:
    """
    Try to deregister a tracked task. Steps:
        1. Grab the lock
        2. Remove task_id from the tasks list
        3. Release lock

    Args:
        region_name: name of region to start updating
        task_id: celery task that is being tracked

    Returns:
        True if deregistered, False if lock was not acquired or task_id does not exist in list
    """
    from app import redis_conn

    tasks_key = _REDIS_TASKS_KEY.format(region_name=region_name)

    got_lock = _get_lock(100)
    if not got_lock:
        return False

    # remove from list
    success = redis_conn.lrem(tasks_key, 1, task_id) == 1
    _release_lock()
    return success


def get_arg_number(func: Callable, desired_arg: str) -> int:
    """
    Given a function `func`, this function gives the argument number of `desired_arg`
    in `func`'s arguments.

    Args:
        func (Callable): a python function that takes arguments
        desired_arg (str): the desired argument

    Returns:
        index in *args of desired_arg, -1 if not found.
    """
    args = inspect.getfullargspec(func).args
    if args is None:
        return -1
    for argn, arg in enumerate(args):
        if desired_arg == arg:
            return argn
    return -1


def maintenance_track_task(func: Callable):
    """
    Decorator to do three things:
        1. check if there is no update going on. if so error out and never call the celery task.
        2. track a celery task during its execution
        3. clean up tracking of the celery task after it finishes
    """
    # must have "self" argument
    self_argn = get_arg_number(func, "self")
    if self_argn == -1:
        raise ValueError(f"Function {func.__name__} needs to have argument self to be tracked")

    # must have "region" or "region_name" argument
    region_argn = get_arg_number(func, "region")
    if region_argn == -1:
        region_argn = get_arg_number(func, "region_name")
        if region_argn == -1:
            raise ValueError(f"Function {func.__name__} needs to have argument self to be tracked")

    @wraps(func)
    def wrapper(*args, **kwargs):
        self_obj = args[self_argn]  # celery "self" object created with @shared_task(bind=True)
        # try different ways of getting region. it can be a KWARG if the caller did
        # func(pos_arg1, pos_arg2, region_name=region)
        region_name = None
        if "region" in kwargs:
            region_name = kwargs["region"]
        elif "region_name" in kwargs:
            region_name = kwargs["region_name"]
        else:
            region_name = args[region_argn]

        # pre-function logic: register the task
        if not try_register_task(region_name=region_name, task_id=self_obj.request.id):
            msg = (
                "Could not register a tracked task. Debug info:\n"
                f"Celery Task ID: {self_obj.request.id}\n"
                f"Function: {func.__name__}\n"
                f"Args: {args}\n"
                f"Kwargs: {kwargs}\n"
            )
            fractal_log(
                "maintenance_track_task",
                None,
                msg,
            )
            raise ValueError("MAINTENANCE ERROR. Failed to register task.")

        exception = None
        try:
            ret = func(*args, **kwargs)
        except Exception as e:  # pylint: disable=broad-except
            exception = e

        # post-function logic: deregister the task
        if not try_deregister_task(
            region_name=region_name,
            task_id=self_obj.request.id,
        ):
            msg = (
                f"CRITICAL! Could not deregister a tracked task.\n"
                "Debug info:\n"
                f"Celery Task ID: {self_obj.request.id}\n"
                f"Function: {func.__name__}\n"
                f"Args: {args}\n"
                f"Kwargs: {kwargs}\n"
            )
            fractal_log(
                "maintenance_track_task",
                None,
                msg,
                level=logging.ERROR,
            )
            raise ValueError("MAINTENANCE ERROR. Failed to deregister task.")

        # raise any exception to caller
        if exception is not None:
            raise exception
        return ret

    return wrapper
