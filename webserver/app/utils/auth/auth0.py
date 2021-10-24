"""Utilities for enforcing scoped access.

Example usage::

    # wsgi.py

    from app.utils.auth.auth0 import scope_required, ScopeError
    from flask import Flask

    app = Flask(__name__)

    app.config["AUTH0_DOMAIN"] = "auth.whist.com"
    app.config["JWT_DECODE_ALGORITHMS"] = ("RS256",)
    app.config["JWT_DECODE_AUDIENCE"] = "https://api.whist.com/"

    @app.errorhandler(ScopeError)
    def _handle_scope_error(_e: ScopeError) -> Any:
        return "Unauthorized", 401

    @app.route("/")
    @scope_required("admin")
    def hello() -> Any:
        return "Hello world"

"""

import functools

from typing import Any, Callable

from flask_jwt_extended import get_jwt, verify_jwt_in_request


class ScopeError(Exception):
    """Raised when an access token does not include the scopes required to access a resource."""


def has_scope(scope: str) -> bool:
    """Determine whether or not an access token contains a claim to the specified scope.

    Args:
        scope: A string containing the name of an OAuth 2.0 scope.

    Returns:
        True if and only if the specified scope appears in the JWT's scope claim.
    """

    verify_jwt_in_request(optional=True)

    return scope in get_jwt().get("scope", "").split()


def scope_required(scope: str) -> Callable[..., Any]:
    """Allow a user to access a decorated endpoint if their access token has the proper scope.

    Instructions for writing custom authorization decorators can be found here:
    https://flask-jwt-extended.readthedocs.io/en/stable/custom_decorators/

    Args:
        scope: A string indicating the name of the OAuth 2.0 scope by which this view function is
            protected.

    Returns:
        A decorator that can be applied to a Flask view function to enforce authentication.

    Raises:
        ScopeError: The access token does not authorize access to the specified API scope.
    """

    def view_decorator(view_func: Callable[..., Any]) -> Callable[..., Any]:
        @functools.wraps(view_func)
        def decorated_view(*args: Any, **kwargs: Any) -> Any:
            verify_jwt_in_request()

            if not has_scope(scope):
                raise ScopeError("Insufficient permissions")

            return view_func(*args, **kwargs)

        return decorated_view

    return view_decorator
