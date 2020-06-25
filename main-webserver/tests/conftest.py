import pytest
import requests
import os
import logging

SERVER_URL = "https://" + os.getenv("HEROKU_APP_NAME") + ".herokuapp.com" if os.getenv("HEROKU_APP_NAME") else "http://localhost:5000"

LOGGER = logging.getLogger(__name__)

def getVersions():
    return requests.get((SERVER_URL + "/version"))

@pytest.fixture(scope="session")
def setup(request):
    LOGGER.info("Waiting for server to deploy...")
    i = 1
    while getVersions().status_code != 200:
        sleep(3)
        i += 1
        LOGGER.info(i + " times pinging server")
    LOGGER.info("Server deployed! Tests starting now.")
    return
