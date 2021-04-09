"""
Analyze a load test. All JSONs saved to `OUTFOLDER`. Can handle
both a local load test or a distributed load test. Main functions
of interest:
- analyze_load_test
"""

import sys
import os
import json
import subprocess
import statistics
from typing import List, Dict, Union
import argparse
import math

# this adds the load_testing folder root to the python path no matter where
# this file is called from. We can now import from `scripts`.
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__)))

from load_test_core import OUTFOLDER
from load_test_models import initialize_db, dummy_flask_app, db, LoadTesting


def load_load_test_data(fp):
    """
    Loads load test outputs as Python Lists. The format will be:
    [
        {
            'task_id': <>,
            'errors': [...],
            'status_codes': [...],
            'web_times': [...],
            'poll_container_time': float,
        },
        ...
    ]

    Handles both the distributed load test case and the local load test case.

    Args:
        fp: pointer to file to load data from

    Returns:
        List of dictionaries structured as described above
    """
    data = json.load(fp)
    if "statusCode" in data:
        # this is the distributed load test data; we need to parse out the "body" key
        assert data["statusCode"] == 200
        return json.loads(data["body"])
    return data


def compute_stats(data: List[Union[int, float]]):
    """
    Extract data from a list of int/float numbers.

    Args:
        A list of int/float numbers

    Returns:
        A dict of the max, min, avg, and stdev of the numbers.
    """
    stdev = 0
    if len(data) > 1:
        stdev = math.sqrt(statistics.variance(data))
    return {
        "max": max(data),
        "min": min(data),
        "avg": sum(data) / len(data),
        "stdev": stdev,
    }


def print_stats(stats: Dict[str, float]):
    """
    Print the dictionary created by compute_stats
    """
    print(f"min: {stats['min']}")
    print(f"max: {stats['max']}")
    print(f"mean: {stats['avg']}")
    print(f"stdev: {stats['stdev']}")


def analyze_load_test(run_id: str = None, db_uri: str = None):
    """
    Analyze a load test by inspecting the files in OUTFOLDER. First we check for errors.
    If there are some, we print them and error out. Otherwise, we compute the following stats:
    - max web request time
    - avg web request time
    - std web request time
    - max container time
    - avg container time
    - std container time

    Args:
        run_id: The id of this load testing run. In workflows, just use the workflow id. If None,
            nothing will be saved to the db.
        db_uri: Must be provided if `run_id` is not None. Allows the script to save the test
            results at `db_uri`.
    """
    if run_id is None:
        print("Warning. No run_id was passed, so results will not be saved to the db.")
    else:
        assert db_uri is not None, "run_id is not None so db_uri must be given"
        initialize_db(db_uri)

    assert os.path.isdir(OUTFOLDER)
    files = os.listdir(OUTFOLDER)
    all_load_test_data = []
    for f in files:
        with open(os.path.join(OUTFOLDER, f)) as fp:
            # data will be a list of JSON outputs
            data = load_load_test_data(fp)
            all_load_test_data += data

    # scan for errors
    user_had_error = False
    for data in all_load_test_data:
        if len(data["errors"]) > 0:
            user_had_error = True
            task_id = data["task_id"]
            errors = data["errors"]
            print(f"Task {task_id} experienced errors: {errors}")
    assert user_had_error is False

    # do analysis if no errors
    all_web_times = []
    all_container_times = []
    for data in all_load_test_data:
        # TODO: use data["status_codes"]
        all_web_times += data["web_times"]  # this is a list of all web request times
        pct = data["poll_container_time"]
        all_container_times.append(pct)

    web_time_stats = compute_stats(all_web_times)
    container_time_stats = compute_stats(all_container_times)

    print("Web stats:\n------")
    print_stats(web_time_stats)

    print("\nContainer stats:\n------")
    print_stats(container_time_stats)

    if run_id is None:
        return

    new_lt_entry = LoadTesting(
        run_id=run_id,
        num_users=len(all_container_times),
        avg_container_time=container_time_stats["avg"],
        max_container_time=container_time_stats["max"],
        std_container_time=container_time_stats["stdev"],
        avg_web_time=web_time_stats["avg"],
        max_web_time=web_time_stats["max"],
        std_web_time=web_time_stats["stdev"],
    )

    with dummy_flask_app.app_context():
        db.session.add(new_lt_entry)
        db.session.commit()

    print(f"Saved new load testing entry for run {run_id}")
