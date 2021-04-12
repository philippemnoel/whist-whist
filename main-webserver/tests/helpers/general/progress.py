import time
import threading
import progressbar
from functools import wraps

from tests.constants.settings import ALLOW_MULTITHREADING


def queryStatus(client, resp, timeout=10):
    """
    @params:
        resp        - Required  : Flask response
        timeout     - Required  : Timeout in minutes, return -1 if timeout is succeeded
    """
    assert (
        resp.status_code == 202
    ), f"Instead got code: {resp.status_code}, content: {resp.get_data()}"

    def getStatus(status_id):
        if status_id:
            resp = client.get(f"/status/{status_id}")
            return resp.json
        return None

    try:
        status_id = resp.json["ID"]
    except Exception as e:
        print(str(e))
        return {"status": -3, "output": "No status ID provided"}

    total_timeout_seconds = timeout * 60
    seconds_elapsed = 0

    # Create progress bar

    bar = progressbar.ProgressBar(
        maxval=total_timeout_seconds + 60,
        widgets=[progressbar.Bar("=", "[", "]"), " ", progressbar.Percentage()],
    )
    bar.start()

    status = "PENDING"
    returned_json = None

    # Wait for job to finish

    while (status == "PENDING" or status == "STARTED") and seconds_elapsed < total_timeout_seconds:
        returned_json = getStatus(status_id)
        if not returned_json:
            return {"status": -3, "output": "No status ID provided"}
        status = returned_json["state"]

        time.sleep(10)
        seconds_elapsed = seconds_elapsed + 10
        bar.update(seconds_elapsed)

    bar.finish()

    # Check for success, timeout, or failure

    if seconds_elapsed > total_timeout_seconds:
        return {"status": -1, "output": "Timeout error"}
    elif status != "SUCCESS":
        return {
            "status": -2,
            "output": "Did not receive SUCCESS, instead saw {error}".format(
                error=str(returned_json)
            ),
        }
    else:
        return {"status": 1, "output": "SUCCESS detected", "result": returned_json["output"]}


def fractalJobRunner(f, initial_list, multithreading=ALLOW_MULTITHREADING):
    if initial_list:
        if multithreading:
            thread_tracker = [None] * len(initial_list)

            for i in range(0, len(initial_list)):
                element = initial_list[i]
                thread_tracker[i] = threading.Thread(target=f, args=(element,))
                thread_tracker[i].start()
            for thread in thread_tracker:
                thread.join()
        else:
            for element in initial_list:
                f(element)


def disabled(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        assert True

    return wrapper
