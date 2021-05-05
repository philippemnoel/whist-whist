import os
import sys
from pprint import pprint
import requests

HEROKU_BASE_URL = os.environ.get("HEROKU_BASE_URL", "https://api.heroku.com")
HEROKU_API_TOKEN = os.environ.get("HEROKU_API_TOKEN")


def heroku_config(app_name):
    """Retrieves a map of configuration variables from Heroku app

    Given an app name, makes a call to the Heroku API to ask for all
    configuration variables stored under that app. This function relies on a
    valid API in the environement variable HEROKU_API_TOKEN.

    Args:
        app_name: A string that represents the name of the app, and configured
                  in Heroku.
    Returns:
        A dictionary of the configuration variable names and values in Heroku.
    """
    url = f"{HEROKU_BASE_URL}/apps/{app_name}/config-vars"

    resp = requests.get(
        url,
        headers={
            "Accept": "application/vnd.heroku+json; version=3",
            "Authorization": f"Bearer {HEROKU_API_TOKEN}",
        },
    )

    resp.raise_for_status()
    return resp.json()


if __name__ == "__main__":
    _, heroku_app_name, *keys = sys.argv

    config = heroku_config(heroku_app_name)

    if keys:
        print(*(config.get(f) for f in keys if f))
    else:
        pprint(config)
