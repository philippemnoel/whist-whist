import hashlib
from app.imports import *
from app.helpers.utils.general.sql_commands import *
from app.constants.bad_words_hashed import BAD_WORDS_HASHED
from app.constants.bad_words import *
from app.constants.generate_subsequences_for_words import generate_subsequence_for_word
from app.models.public import *


def generatePrivateKey():
    return secrets.token_hex(16)


def generateUniquePromoCode():
    users = User.query.all()
    old_codes = []
    if users:
        old_codes = [user.referral_code for user in users]
    new_code = generatePromoCode()
    while new_code in old_codes:
        new_code = generatePromoCode()
    return new_code


def getAccessTokens(username):
    access_token = create_access_token(identity=username, expires_delta=False)
    refresh_token = create_refresh_token(identity=username, expires_delta=False)
    return (access_token, refresh_token)


def generateToken(username):
    username_subsq = generate_subsequence_for_word(username)
    for result in username_subsq:
    	username_encoding = result.lower().encode('utf-8')
        if hashlib.md5(username_encoding).hexdigest() in BAD_WORDS_HASHED:
	    return None

    token = jwt.encode({"email": username}, JWT_SECRET_KEY)
    if len(token) > 15:
        token = token[-15:]
    else:
        token = token[-1 * len(pwd_token) :]

    return token


def generatePromoCode():
    upperCase = string.ascii_uppercase
    numbers = "1234567890"
    allowed = False
    while not allowed:
        c1 = "".join([random.choice(numbers) for _ in range(0, 3)])
        c2 = "".join([random.choice(upperCase) for _ in range(0, 3)]) + "-" + c1
        c2_subsq = generate_subsequence_for_word(c2)
        for result in c2_subsq:
                c2_encoding = result.lower().encode('utf-8')
                if hashlib.md5(c2_encoding).hexdigest() not in BAD_WORDS_HASHED:
                        allowed = True
                else:
                        allowed = False
                        break
    return c2


def genHaiku(n):
    """Generates an array of haiku names (no more than 15 characters) using haikunator

    Args:
        n (int): Length of the array to generate

    Returns:
        arr: An array of haikus
    """
    haikunator = Haikunator()
    haikus = [
        haikunator.haikunate(delimiter="") + str(np.random.randint(0, 10000)) for _ in range(0, n)
    ]
    haikus = [haiku[0 : np.min([15, len(haiku)])] for haiku in haikus]
    return haikus


def getGoogleTokens(code, clientApp):
    if clientApp:
        client_secret = "secrets/google_client_secret_desktop.json"
        redirect_uri = "urn:ietf:wg:oauth:2.0:oob:auto"
    else:
        client_secret = "secrets/google_client_secret.json"
        redirect_uri = "postmessage"

    flow = Flow.from_client_secrets_file(
        client_secret,
        scopes=[
            "https://www.googleapis.com/auth/userinfo.email",
            "openid",
            "https://www.googleapis.com/auth/userinfo.profile",
        ],
        redirect_uri=redirect_uri,
    )

    flow.fetch_token(code=code)

    credentials = flow.credentials

    session = flow.authorized_session()
    profile = session.get("https://www.googleapis.com/userinfo/v2/me").json()

    return {
        "access_token": credentials.token,
        "refresh_token": credentials.refresh_token,
        "google_id": profile["id"],
        "email": profile["email"],
        "name": profile["given_name"],
    }
