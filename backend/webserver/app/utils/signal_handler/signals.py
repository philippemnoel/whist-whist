"""
This modules contains handlers for the following signals:
1. SIGTERM. Sent by Heroku before dyno restart. See
https://www.notion.so/whisthq/Resolving-Heroku-Dyno-Restart-db63f4cbb9bd49a1a1fdab7aeb1f77e6
for more details on when this happens and how we are solving it.
"""
import signal
from types import FrameType
from typing import Optional

from app.utils.general.logs import whist_logger
from app.utils.flask.flask_handlers import set_web_requests_status


class WebSignalHandler:
    """
    Handles signals for the web dyno. Specifically:
    - SIGTERM (`handle_sigterm`)

    Initialize in webserver with:
    >>> WebSignalHandler() # signals are now handled
    """

    def __init__(self) -> None:
        signal.signal(signal.SIGTERM, self.handle_sigterm)

    def handle_sigterm(
        self, signum: int, _frame: Optional[FrameType]
    ) -> None:  # pylint: disable=no-self-use
        """
        Handles SIGTERM, which is sent by Heroku on various conditions outlined here:
        https://www.notion.so/whisthq/Resolving-Heroku-Dyno-Restart-db63f4cbb9bd49a1a1fdab7aeb1f77e6

        SIGKILL follows 30 seconds from now, so this is our chance to clean-up resources cleanly.
        Specifically, we do the following:
        1. Flip a flag to stop incoming webserver requests, thereby making sure a webserver request
        is not interrupted. We don't kill webserver now because a web request might be ongoing.
        """
        # 1. disallow web requests
        if not set_web_requests_status(False):
            whist_logger.error("Could not disable web requests after SIGTERM.")
