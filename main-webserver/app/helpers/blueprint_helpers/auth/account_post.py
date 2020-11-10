import datetime
import logging

from datetime import datetime as dt
from flask import jsonify, current_app
from sendgrid import SendGridAPIClient
from sendgrid.helpers.mail import Mail

from app.constants.config import SENDGRID_API_KEY, SENDGRID_EMAIL
from app.constants.http_codes import BAD_REQUEST, NOT_ACCEPTABLE, SUCCESS, UNAUTHORIZED, NOT_FOUND
from app.helpers.blueprint_helpers.mail.mail_post import verificationHelper
from app.helpers.utils.general.crypto import check_value, hash_value
from app.helpers.utils.general.logs import fractalLog
from app.helpers.utils.general.sql_commands import (
    fractalSQLCommit,
    fractalSQLUpdate,
)
from app.helpers.utils.general.tokens import (
    generateToken,
    generateUniquePromoCode,
    getAccessTokens,
)
from app.models import db, User

from app.helpers.utils.datadog.events import (
    datadogEvent_userLogon,
)


def loginHelper(email, password):
    """Verifies the username password combination in the users SQL table

    If the password is the admin password, just check if the username exists
    Otherwise, check to see if the username is in the database and the JWT
    encoded password is in the database

    Parameters:
    email (str): The email
    password (str): The password

    Returns:
    json: Metadata about the user
    """

    # First, check if username is valid

    is_user = True

    user = User.query.get(email)

    # Return early if username/password combo is invalid

    if is_user:
        if not user or not check_value(user.password, password):
            return {
                "verified": False,
                "is_user": is_user,
                "access_token": None,
                "refresh_token": None,
                "verification_token": None,
                "name": None,
                "can_login": False,
            }

    # Fetch the JWT tokens

    access_token, refresh_token = getAccessTokens(email)

    if not current_app.testing:
        datadogEvent_userLogon(email)

    return {
        "verified": user.verified,
        "is_user": is_user,
        "access_token": access_token,
        "refresh_token": refresh_token,
        "verification_token": user.token,
        "name": user.name,
        "can_login": user.can_login,
    }


def registerHelper(username, password, name, reason_for_signup):
    """Stores username and password in the database and generates user metadata, like their
    user ID and promo code

    Parameters:
    username (str): The username
    password (str): The password
    name (str): The person's name
    reason_for_signup (str): The person's reason for signing up

    Returns:
    json: Metadata about the user
    """

    # First, generate a user ID

    token = generateToken(username)

    # Second, hash their password

    pwd_token = hash_value(password)

    # Third, generate a promo code for the user

    promo_code = generateUniquePromoCode()

    # Add the user to the database

    new_user = User(
        user_id=username,
        password=pwd_token,
        token=token,
        referral_code=promo_code,
        name=name,
        reason_for_signup=reason_for_signup,
        release_stage=50,
        created_timestamp=dt.now(datetime.timezone.utc).timestamp(),
    )

    status = SUCCESS
    access_token, refresh_token = getAccessTokens(username)

    # Check for errors in adding the user to the database

    try:
        db.session.add(new_user)
        db.session.commit()
    except Exception as e:
        fractalLog(
            function="registerHelper",
            label=username,
            logs="Registration failed: " + str(e),
            level=logging.ERROR,
        )
        status = BAD_REQUEST
        access_token = refresh_token = None

    if status == SUCCESS:
        try:
            message = Mail(
                from_email=SENDGRID_EMAIL,
                to_emails="support@tryfractal.com",
                subject=username + " just created an account!",
                html_content=(
                    f"<p>Just letting you know that {name} created an account. Their reason for "
                    f"signup is: {reason_for_signup}. Have a great day.</p>"
                ),
            )
            sg = SendGridAPIClient(SENDGRID_API_KEY)
            sg.send(message)
        except Exception as e:
            fractalLog(
                function="registerHelper",
                label=username,
                logs="Mail send failed: Error code " + str(e),
                level=logging.ERROR,
            )

    return {
        "status": status,
        "verification_token": new_user.token,
        "access_token": access_token,
        "refresh_token": refresh_token,
    }


def verifyHelper(username, provided_user_id):
    """Checks provided verification token against database token. If they match, we verify the
    user's email.

    Parameters:
    username (str): The username
    token (str): Email verification token

    Returns:
    json: Success/failure of email verification
    """

    # Select the user's ID

    user_id = None

    user = User.query.filter_by(user_id=username).first()

    if user:
        # Check to see if the provided user ID matches the selected user ID

        if provided_user_id == user.token:
            fractalLog(
                function="verifyHelper",
                label=user_id,
                logs="Verification token is valid, verifying.",
            )
            fractalSQLCommit(db, fractalSQLUpdate, user, {"verified": True})

            return {"status": SUCCESS, "verified": True}
        else:
            fractalLog(
                function="verifyHelper",
                label=user_id,
                logs="Verification token {token} is invalid, cannot validate.".format(
                    token=provided_user_id
                ),
                level=logging.WARNING,
            )
            return {"status": UNAUTHORIZED, "verified": False}
    else:
        return {"status": UNAUTHORIZED, "verified": False}


def deleteHelper(username):
    """Deletes a user's account and their disks from the database

    Parameters:
    username (str): The username

    Returns:
    json: Success/failure of deletion
    """

    user = User.query.get(username)

    if not user:
        return {"status": BAD_REQUEST, "error": f"User {username} not found"}

    db.session.delete(user)
    db.session.commit()

    return {"status": SUCCESS, "error": None}


def resetPasswordHelper(username, password):
    """Updates the password for a user in the users SQL table

    Args:
        username (str): The user to update the password for
        password (str): The new password
    """
    user = User.query.get(username)

    if user:
        pwd_token = hash_value(password)

        user.password = pwd_token
        db.session.commit()
        return {"status": SUCCESS}
    else:
        return {"status": BAD_REQUEST}


def lookupHelper(username):
    """Checks if user exists in the users SQL table

    Args:
        username (str): The user to lookup
    """
    user = User.query.get(username)

    if user:
        return {"exists": True, "status": SUCCESS}
    else:
        return {"exists": False, "status": SUCCESS}


def updateUserHelper(body):
    user = User.query.get(body["username"])
    if user:
        if "name" in body:
            user.name = body["name"]
            db.session.commit()

            return jsonify({"msg": "Name updated successfully"}), SUCCESS
        if "email" in body:
            user.user_id = body["email"]
            db.session.commit()

            token = user.token
            return verificationHelper(body["email"], token)
        if "password" in body:
            resetPasswordHelper(body["username"], body["password"])
            return jsonify({"msg": "Password updated successfully"}), SUCCESS
        return jsonify({"msg": "Field not accepted"}), NOT_ACCEPTABLE
    return jsonify({"msg": "User not found"}), NOT_FOUND


def autoLoginHelper(email):
    user = User.query.get(email)
    access_token, refresh_token = getAccessTokens(email)

    if user:
        return {
            "status": SUCCESS,
            "access_token": access_token,
            "refresh_token": refresh_token,
            "verification_token": user.token,
            "name": user.name,
        }
    else:
        return {
            "status": UNAUTHORIZED,
            "access_token": None,
            "refresh_token": None,
            "verification_token": None,
            "name": None,
        }

def verifyPasswordHelper(email, password):
    user = User.query.get(email)

    if not user or not check_value(user.password, password):
        return {
            "status": UNAUTHORIZED,
        }
    else:
        return {
            "status": SUCCESS,
        }

