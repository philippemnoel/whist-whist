import os

import sentry_sdk

from flask import Flask
from flask_cors import CORS
from flask_jwt_extended import JWTManager
from flask_marshmallow import Marshmallow
from flask_sendgrid import SendGrid
from sentry_sdk.integrations.flask import FlaskIntegration
from sentry_sdk.integrations.celery import CeleryIntegration

from .config import CONFIG_MATRIX

jwtManager = JWTManager()
ma = Marshmallow()
mail = SendGrid()


def create_app(testing=False):
    """A Flask application factory.

    See https://flask.palletsprojects.com/en/1.1.x/patterns/appfactories/?highlight=factory.

    Args:
        testing: A boolean indicating whether or not to configure the application for testing.

    Returns:
        A Flask application instance.
    """

    # Set up Sentry - only log errors on prod (main) and staging webservers
    env = None
    if os.getenv("HEROKU_APP_NAME") == "fractal-prod-server":
        env = "production"
    elif os.getenv("HEROKU_APP_NAME") == "fractal-staging-server":
        env = "staging"
    if env is not None:
        sentry_sdk.init(
            dsn="https://9b796075e3f44ebca47c2a59c12251ca@o400459.ingest.sentry.io/5394545",
            integrations=[FlaskIntegration(), CeleryIntegration()],
            environment=env,
            release="main-webserver@" + os.getenv("HEROKU_SLUG_COMMIT", "local"),
        )

    template_dir = os.path.dirname(os.path.realpath(__file__))
    template_dir = os.path.join(template_dir, "templates")
    app = Flask(__name__.split(".")[0], template_folder=template_dir)

    # We want to look up CONFIG_MATRIX.location.action
    action = "test" if testing else "serve"
    location = "deployment" if "DYNO" in os.environ else "local"
    config = getattr(getattr(CONFIG_MATRIX, location), action)

    app.config.from_object(config())

    from .models import db
    from .helpers.utils.general.limiter import limiter

    # we don't want rate limits in test apps
    if not testing:
        limiter.init_app(app)
    db.init_app(app)
    jwtManager.init_app(app)
    ma.init_app(app)
    mail.init_app(app)

    CORS(app)

    return register_blueprints(app)


def register_blueprints(app):
    """
    Registers all blueprints (endpoints) for the Flask app

    Args:
        - app: Flask object
    """

    from .blueprints.admin.admin_blueprint import admin_bp
    from .blueprints.admin.logs_blueprint import logs_bp
    from .blueprints.admin.hasura_blueprint import hasura_bp

    from .blueprints.auth.account_blueprint import account_bp
    from .blueprints.auth.token_blueprint import token_bp
    from .blueprints.auth.google_auth_blueprint import google_auth_bp

    from .blueprints.celery.celery_status_blueprint import celery_status_bp

    from .blueprints.aws.aws_container_blueprint import aws_container_bp

    from .blueprints.mail.mail_blueprint import mail_bp
    from .blueprints.mail.newsletter_blueprint import newsletter_bp

    from .blueprints.payment.stripe_blueprint import stripe_bp

    from .blueprints.host_service.host_service_blueprint import host_service_bp

    from .blueprints.oauth import oauth_bp

    app.register_blueprint(account_bp)
    app.register_blueprint(token_bp)
    app.register_blueprint(celery_status_bp)
    app.register_blueprint(aws_container_bp)
    app.register_blueprint(hasura_bp)
    app.register_blueprint(google_auth_bp)
    app.register_blueprint(mail_bp)
    app.register_blueprint(newsletter_bp)
    app.register_blueprint(stripe_bp)
    app.register_blueprint(admin_bp)
    app.register_blueprint(logs_bp)
    app.register_blueprint(host_service_bp)
    app.register_blueprint(oauth_bp)

    return app
