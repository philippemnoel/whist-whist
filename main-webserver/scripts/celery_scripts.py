import time
import sys
import os

# this adds the webserver repo root to the python path no matter where
# this file is called from. We can now import from `scripts`.
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), ".."))

from scripts.utils import make_get_request


def poll_celery_task(
    web_url: str,
    task_id: str,
    admin_token: str = None,
    num_tries: int = 300,
    sleep_time: float = 2.0,
):
    """
    Poll celery task `task_id` for up to 10 minutes.
    Args:
        web_url: URL of webserver instance to run operation on
        task_id: Task to poll
        admin_token: Optional; can provide traceback info.
        num_tries: number of times to try getting task result
        sleep_time: sleep time in between tries

    Returns:
        The task output on success, otherwise errors out.
    """
    print(f"Polling task {task_id}")
    endpoint = f"/status/{task_id}"
    resp_json = None
    for _ in range(num_tries):
        resp = make_get_request(web_url, endpoint, admin_token=admin_token)
        assert resp.status_code == 200, f"got code: {resp.status_code}, content: {resp.content}"
        resp_json = resp.json()
        if resp_json["state"] in ("PENDING", "STARTED"):
            # sleep and try again
            time.sleep(sleep_time)
        else:
            # non-pending/started states should exit
            break
    assert resp_json is not None, f"No JSON response for task {task_id}"
    print(f"Got JSON: {resp_json}")
    assert resp_json["state"] == "SUCCESS", f"Got response: {resp_json} for task {task_id}"
    return resp_json["output"]
