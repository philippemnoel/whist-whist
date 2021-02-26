from flask import current_app

from app.constants.http_codes import SUCCESS, BAD_REQUEST
from app.helpers.utils.mail.mail_client import MailClient, SendGridException, TemplateNotFound


def mail_helper(email_id, from_email, to_email, email_args):
    """
    Helper function for the /mail route.

    Calls the send_email function, and catches possible exceptions.

    Parameters:
        email_id (str): ID of the email being sent (ex. EMAIL_VERIFICATION, PASSWORD_RESET,
                        etc.)
        from_email (str): email being sent from
        to_email (str): email being sent to
        email_args (dict): dict of Jinja arguments to pass into render_template()

    Returns:
        A dictionary object with the single key "status", representing the HTTP response to the
        request.
    """

    try:
        mail_client = MailClient(current_app.config["SENDGRID_API_KEY"])
        mail_client.send_email(
            from_email=from_email, to_email=to_email, email_id=email_id, jinja_args=email_args
        )
        return {"verified": True, "status": SUCCESS}
    except SendGridException:
        return {"verified": False, "status": BAD_REQUEST}
    except TemplateNotFound:
        return {"verified": False, "status": BAD_REQUEST}
