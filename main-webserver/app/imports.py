import os
import random
import string
import time
import sys
import logging
import requests
import os.path
import traceback
import stripe
import datetime
import pytz
import socket
import dateutil.parser
import sqlalchemy
import flask_sqlalchemy
import numpy as np
import json
import secrets
import pandas as pd

from dateutil.relativedelta import relativedelta
from inspect import getsourcefile
from sqlalchemy.orm import sessionmaker, relationship
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy import *
from azure.common.credentials import ServicePrincipalCredentials
from azure.mgmt.resource import ResourceManagementClient
from azure.mgmt.network import NetworkManagementClient
from azure.mgmt.compute import ComputeManagementClient
from azure.mgmt.compute.models import DiskCreateOption
from msrestazure.azure_exceptions import CloudError
from haikunator import Haikunator
from dotenv import *
from flask_migrate import Migrate
from celery import Celery, uuid, task
from flask import Flask, request, jsonify, Blueprint, make_response, render_template
from sqlalchemy.sql import text
from flask_sqlalchemy import SQLAlchemy
from flask_marshmallow import Marshmallow
from jose import jwt
from flask_cors import CORS
from flask_mail import Mail, Message
from datetime import timedelta, datetime as dt, timezone
from multiprocessing.util import register_after_fork
from flask_jwt_extended import *
from sendgrid import SendGridAPIClient
from sendgrid.helpers.mail import Mail as SendGridMail
from logging.handlers import SysLogHandler
from functools import wraps
from msrest.exceptions import ClientException
from google_auth_oauthlib.flow import Flow

from app.constants.config import *
from app.constants.http_codes import *

load_dotenv()
