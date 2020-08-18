from .imports import *
from .factory import *
from .constants.config import *

from .helpers.utils.general.logs import *
from .helpers.utils.general.sql_commands import *
from .helpers.utils.general.time import *


def make_celery(app_name=__name__):
    broker = os.getenv("REDIS_URL")
    backend = os.getenv("REDIS_URL")
    return Celery(app_name, broker=broker, backend=backend)


def fractalPreProcess(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        received_from = (
            request.headers.getlist("X-Forwarded-For")[0]
            if request.headers.getlist("X-Forwarded-For")
            else request.remote_addr
        )

        try:
            body = json.loads(request.data) if request.method == "POST" else None
        except Exception as e:
            print(str(e))
            body = None

        kwargs["body"] = body
        kwargs["received_from"] = received_from

        silence = False
        for endpoint in SILENCED_ENDPOINTS:
            if endpoint in request.url:
                silence = True
                break

        if not silence:
            format = "%(asctime)s %(levelname)-8s [%(filename)s:%(lineno)d] %(message)s"

            logging.basicConfig(format=format, datefmt="%b %d %H:%M:%S")
            logger = logging.getLogger(__name__)
            logger.setLevel(logging.DEBUG)

            if body and request.method == "POST":
                body = {
                    k: str(v)[0 : min(len(str(v)), 500)] for k, v in dict(body).items()
                }
                body = str(body)

            logger.info("{}\n{}\r\n".format(request.method + " " + request.url, body,))

        return f(*args, **kwargs)

    return wrapper


celery_instance = make_celery()

app, jwtManager = create_app(celery=celery_instance)
app.config['SQLALCHEMY_DATABASE_URI'] = DATABASE_URL
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

db = SQLAlchemy(app) # initialize FlaskSQLAlchemy
app = init_app(app)

app.config["JWT_SECRET_KEY"] = JWT_SECRET_KEY
app.config["ROOT_DIRECTORY"] = os.path.dirname(os.path.abspath(__file__))

CORS(app)
