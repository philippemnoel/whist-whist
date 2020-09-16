from app import *
from app.helpers.blueprint_helpers.mail.mail_post import *

mail_bp = Blueprint("mail_bp", __name__)


@mail_bp.route("/mail/<action>", methods=["POST"])
@fractalPreProcess
def mail(action, **kwargs):
    body = kwargs["body"]
    if action == "forgot":
        return forgotPasswordHelper(body["username"])
f
    elif action == "cancel":
        return cancelHelper(body["username"], body["feedback"])

    elif action == "verification":
        return verificationHelper(body["username"], body["token"])

    elif action == "referral":
        return referralMailHelper(body["username"], body["recipients"], body["code"])

    elif action == "feedback":
        return feedbackHelper(body["username"], body["feedback"], body["type"])

    elif action == "trialStart":
        return trialStartHelper(body["username"], body["location"], body["code"])

    elif action == "computerReady":
        return computerReadyHelper(
            body["username"], body["date"], body["code"], body["location"]
        )
    elif action == "joinWaitlist":
        return joinWaitlistHelper(body["email"], body["name"], body["date"])
