from sqlalchemy.orm import relationship
from sqlalchemy_utils.types.encrypted.encrypted_type import AesEngine, StringEncryptedType

from ._meta import db, secret_key


class UserContainer(db.Model):
    __tablename__ = "user_containers"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    container_id = db.Column(db.String(250), primary_key=True, unique=True)
    ip = db.Column(db.String(250), nullable=False)
    location = db.Column(db.String(250), nullable=False)
    os = db.Column(db.String(250), nullable=False)
    state = db.Column(db.String(250), nullable=False)
    lock = db.Column(db.Boolean, nullable=False, default=False)
    user_id = db.Column(db.ForeignKey("users.user_id"), nullable=True)
    user = relationship("User", back_populates="containers")
    task_definition = db.Column(db.ForeignKey("hardware.supported_app_images.task_definition"))
    task_version = db.Column(db.Integer, nullable=True)
    app = relationship("SupportedAppImages", back_populates="containers")
    port_32262 = db.Column(db.Integer, nullable=False)
    port_32263 = db.Column(db.Integer, nullable=False)
    port_32273 = db.Column(db.Integer, nullable=False)
    last_pinged = db.Column(db.Integer)
    cluster = db.Column(db.ForeignKey("hardware.cluster_info.cluster"))
    parent_cluster = relationship("ClusterInfo", back_populates="containers")
    using_stun = db.Column(db.Boolean, nullable=False, default=False)
    branch = db.Column(db.String(250), nullable=False, default="prod")
    allow_autoupdate = db.Column(db.Boolean, nullable=False, default=True)
    temporary_lock = db.Column(db.Integer)
    dpi = db.Column(db.Integer)
    secret_key = db.Column(
        StringEncryptedType(db.String, secret_key, AesEngine, "pkcs5"), nullable=False
    )


class ClusterInfo(db.Model):
    __tablename__ = "cluster_info"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    cluster = db.Column(db.String(250), primary_key=True, unique=True)
    location = db.Column(db.String(250), nullable=False, default="N/a")
    maxCPURemainingPerInstance = db.Column(db.Float, nullable=False, default=1024.0)
    maxMemoryRemainingPerInstance = db.Column(db.Float, nullable=False, default=2000.0)
    pendingTasksCount = db.Column(db.Integer, nullable=False, default=0)
    runningTasksCount = db.Column(db.Integer, nullable=False, default=0)
    registeredContainerInstancesCount = db.Column(db.Integer, nullable=False, default=0)
    minContainers = db.Column(db.Integer, nullable=False, default=0)
    maxContainers = db.Column(db.Integer, nullable=False, default=0)
    status = db.Column(db.String(250), nullable=False)
    containers = relationship(
        "UserContainer",
        back_populates="parent_cluster",
        lazy="dynamic",
        passive_deletes=True,
    )


class SortedClusters(db.Model):
    __tablename__ = "cluster_sorted"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    cluster = db.Column(db.String(250), primary_key=True, unique=True)
    location = db.Column(db.String(250), nullable=False, default="N/a")
    maxCPURemainingPerInstance = db.Column(db.Float, nullable=False, default=1024.0)
    maxMemoryRemainingPerInstance = db.Column(db.Float, nullable=False, default=2000.0)
    pendingTasksCount = db.Column(db.Integer, nullable=False, default=0)
    runningTasksCount = db.Column(db.Integer, nullable=False, default=0)
    registeredContainerInstancesCount = db.Column(db.Integer, nullable=False, default=0)
    minContainers = db.Column(db.Integer, nullable=False, default=0)
    maxContainers = db.Column(db.Integer, nullable=False, default=0)
    status = db.Column(db.String(250), nullable=False)


class RegionToAmi(db.Model):
    """
    This class represents the region_to_ami table in hardware
    it maps region names to the AMIs which should be used
    for clusters in that region
    """

    __tablename__ = "region_to_ami"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    region_name = db.Column(db.String(250), nullable=False, unique=True, primary_key=True)
    ami_id = db.Column(db.String(250), nullable=False)


class SupportedAppImages(db.Model):
    __tablename__ = "supported_app_images"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}

    app_id = db.Column(db.String(250), nullable=False, unique=True, primary_key=True)
    logo_url = db.Column(db.String(250), nullable=False)
    task_definition = db.Column(db.String(250), nullable=False)
    task_version = db.Column(db.Integer, nullable=True)
    category = db.Column(db.String(250))
    description = db.Column(db.String(250))
    long_description = db.Column(db.String(250))
    url = db.Column(db.String(250))
    tos = db.Column(db.String(250))
    active = db.Column(db.Boolean, nullable=False, default=False)
    # The coefficient delineating what fraction of live users we should have
    # as a prewarmed buffer
    preboot_number = db.Column(db.Float, nullable=False)
    containers = relationship(
        "UserContainer",
        back_populates="app",
        lazy="dynamic",
        passive_deletes=True,
    )


class Banners(db.Model):
    __tablename__ = "banners"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}

    heading = db.Column(db.String(250), nullable=False, unique=True, primary_key=True)
    subheading = db.Column(db.String(250))
    category = db.Column(db.String(250))
    background = db.Column(db.String(250))
    url = db.Column(db.String(250))


class UserContainerState(db.Model):
    """Stores basic state information which users can query or subscribe to, to
    find out whether their container/app is ready, pending etc. Check
    container_state_values (constants) for he list of possible states and
    container_state.py in helpers/blueprint_helpers for the methods which you can use
    for this table.

    Args:
        db (SLQAlchemy db): Implements db methods to communicate with the physical infra.
    """

    __tablename__ = "user_app_state"  # may want to change going forward
    __table_args__ = {"extend_existing": True, "schema": "hardware"}

    user_id = db.Column(db.ForeignKey("users.user_id"), primary_key=True, nullable=False)
    state = db.Column(db.String(250), nullable=False)
    task_id = db.Column(db.String(250), nullable=False)
