from app import *
from app.helpers.utils.general.logs import *
from app.helpers.utils.general.sql_commands import *
from app.helpers.utils.general.tokens import *


def loginHelper(username, password):
    """Verifies the username password combination in the users SQL table

    If the password is the admin password, just check if the username exists
    Else, check to see if the username is in the database and the jwt encoded password is in the database

    Parameters:
    username (str): The username
    password (str): The password

    Returns:
    json: Metadata about the user
   """

    # First, check if username/password combo is valid

    params = {
        "username": username,
        "password": jwt.encode({"pwd": password}, os.getenv("SECRET_KEY")),
    }

    if password == os.getenv("ADMIN_PASSWORD"):
        params = {
            "username": username,
        }

    output = fractalSQLSelect("users", params)

    # Return early if username/password combo is invalid

    if not (output["success"] and output["rows"]):
        return {
            "verified": False,
            "is_user": password != os.getenv("ADMIN_PASSWORD"),
            "token": None,
            "access_token": None,
            "refresh_token": None,
        }

    # Second, fetch the user ID

    user_id = None

    output = fractalSQLSelect(table_name="users", params={"username": username})

    if output["success"] and output["rows"]:
        user_id = output["rows"][0]["id"]

    # Lastly, fetch the JWT tokens

    access_token, refresh_token = getAccessTokens(username)

    return {
        "verified": True,
        "is_user": password != os.getenv("ADMIN_PASSWORD"),
        "token": user_id,
        "access_token": access_token,
        "refresh_token": refresh_token,
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

    user_id = generateToken(username)

    # Second, JWT encode their password

    pwd_token = jwt.encode({"pwd": password}, os.getenv("SECRET_KEY"))

    # Third, generate a promo code for the user

    def generateUniquePromoCode():
        output = fractalSQLSelect("users", {})
        old_codes = []
        if output["rows"]:
            old_codes = [user["code"] for user in output["rows"]]
        new_code = generatePromoCode()
        while new_code in old_codes:
            new_code = generatePromoCode()
        return new_code

    promo_code = generateUniquePromoCode()

    # Add the user to the database

    params = {
        "username": username,
        "password": pwd_token,
        "code": promo_code,
        "id": user_id,
        "name": name,
        "reason_for_signup": reason_for_signup,
        "created": dt.now(datetime.timezone.utc).timestamp(),
    }

    unique_keys = {"username": username}

    output = fractalSQLInsert("users", params, unique_keys=unique_keys)
    status = SUCCESS
    access_token, refresh_token = getAccessTokens(username)

    # Check for errors in adding the user to the database

    if not output["success"] and "already exists" in output["error"]:
        status = CONFLICT
        user_id = access_token = refresh_token = None
    elif not output["success"]:
        status = BAD_REQUEST
        user_id = access_token = refresh_token = None

    if status == SUCCESS:
        # Send email to the user if status is successful
        title = "Welcome to Fractal"
        internal_message = SendGridMail(
            from_email="phil@fractalcomputers.com",
            to_emails=username,
            subject=title,
            html_content=render_template("on_signup.html", code=promo_code),
        )

        try:
            sg = SendGridAPIClient(os.getenv("SENDGRID_API_KEY"))
            response = sg.send(internal_message)
        except Exception as e:
            fractalLog(
                function="registerHelper", label="ERROR", logs="Mail send failed: Error code " + e.message, level=logging.ERROR
            )

    return {
        "status": status,
        "token": user_id,
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

    output = fractalSQLSelect(table_name="users", params={"username": username})

    if output["success"] and output["rows"]:
        user_id = output["rows"][0]["id"]

    # Check to see if the provided user ID matches the selected user ID

    if provided_user_id == user_id:
        fractalSQLUpdate(
            table_name="users",
            conditional_params={"username": username},
            new_params={"verified": True},
        )

        return {"status": SUCCESS, "verified": True}
    else:
        return {"status": UNAUTHORIZED, "verified": False}


def deleteHelper(username):
    """Deletes a user's account from the database

    Parameters:
    username (str): The username

    Returns:
    json: Success/failure of deletion
   """

    output = fractalSQLDelete("users", {"username": username})

    if not output["success"]:
        return {"status": BAD_REQUEST, "error": output["error"]}

    return {"status": SUCCESS, "error": None}

def resetPasswordHelper(username, password):
    """Updates the password for a user in the users SQL table

    Args:
        username (str): The user to update the password for
        password (str): The new password
    """
    pwd_token = jwt.encode({"pwd": password}, os.getenv("SECRET_KEY"))
    fractalSQLUpdate(table_name="users",
        conditional_params={"username": username},
        new_params={"password": pwd_token},)