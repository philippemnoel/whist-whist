from app import *
from app.helpers.utils.sql_commands import *
from app.helpers.utils.tokens import *


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
    status = 200
    access_token, refresh_token = getAccessTokens(username)

    # Check for errors in adding the user to the database

    if not output["success"] and "already exists" in output["error"]:
        status = 409
        user_id = access_token = refresh_token = None
    elif not output["success"]:
        status = 400
        user_id = access_token = refresh_token = None

    return {
        "status": status,
        "token": user_id,
        "access_token": access_token,
        "refresh_token": refresh_token,
    }
