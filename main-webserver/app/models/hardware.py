from sqlalchemy.orm import relationship
from sqlalchemy.sql.expression import true
from sqlalchemy_utils.types.encrypted.encrypted_type import AesEngine, StringEncryptedType

from ._meta import db, secret_key


class UserContainer(db.Model):
    """User container data - information for spun up protocol containers

    This SQLAlchemy model provides an interface to the hardware.user_containers
    table of the database

    Attributes:
        container_id (string): container ID, taken from AWS
        ip (string): ip address of the container
        location (string): AWS region of the container
        os (string): Container operating system
        state (string): Container state, either [RUNNING_AVAILABLE, RUNNING_UNAVAILABLE,
            DEALLOCATED, DEALLOCATING, STOPPED, STOPPING, DELETING, CREATING, RESTARTING, STARTING]
        user_id (string): User ID, typically email
        user (User): reference to user in public.users with id user_id
        task_definition (string): foreign key to supported_app_images of streamed application
        app (SupportedAppImage): reference to hardware.supported_app_image obj of
            streamed application
        port_32262 (int): port corresponding to port 32262
        port_32263 (int): port corresponding to port 32263
        port_32273 (int): port corresponding to port 32273
        last_pinged (int): timestamp representing when the container was last pinged
        cluster (string): foreign key for the cluster the container is hosted on
        parent_cluster (ClusterInfo): reference to hardware.cluster_info object of the parent
        using_stun (bool): true/false whether we're using STUN server
        branch (string): branch the container was created on - prod, staging, dev
        allow_autoupdate (bool): true/false allow protocol to update automatically
        dpi (int): pixel density of the stream
        secret_key (string): 16-byte AES key used to encrypt communication between protocol
            server and client


    """

    __tablename__ = "user_containers"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    container_id = db.Column(db.String(250), primary_key=True, unique=True)
    ip = db.Column(db.String(250), nullable=False)
    location = db.Column(db.String(250), nullable=False)
    os = db.Column(db.String(250), nullable=False)
    state = db.Column(db.String(250), nullable=False)
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
    dpi = db.Column(db.Integer)
    secret_key = db.Column(
        StringEncryptedType(db.String, secret_key, AesEngine, "pkcs5"), nullable=False
    )


class ClusterInfo(db.Model):
    """Stores data for ECS clusters spun up by the webserver. Containers information such as
       total task count, number of running instances, and container status.

    Attributes:
        cluster (string): cluster id from AWS console
        location (string): AWS region (i.e. us-east-1)
        maxCPURemainingPerInstance (float): maximum available CPU that has not
            been allocated to tasks
        maxMemoryRemainingPerInstance (float): maximum avilable RAM that has not
            been allocated to tasks
        pendingTaskscount (int): number of tasks in the cluster that are in the PENDING state
        runningTasksCount (int): number of tasks in the cluster that are in the RUNNING state
        registeredContainerInstancesCount (int): The number of container instances registered
            into the cluster. This includes container instances in both ACTIVE and DRAINING status.
        minContainers (int): The minimum size of the Auto Scaling group.
        maxContainers (int): The maximum size of the Auto Scaling Group
        status: (string): Status of the cluster, either [ACTIVE, PROVISIONING,
            DEPROVISINING, FAILED, INACTIVE]
        containers (UserContainer): reference to hardware.user_containers of containers
            within given cluster

    """

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
    """Sorted list of cluster data, sorted by registeredCotnainerInstancesCount

    Attributes:
        cluster (string): cluster id from AWS console
        location (string): AWS region (i.e. us-east-1)
        maxCPURemainingPerInstance (float): maximum available CPU that has not
            been allocated to tasks
        maxMemoryRemainingPerInstance (float): maximum avilable RAM that has not
            been allocated to tasks
        pendingTaskscount (int): number of tasks in the cluster that are in the PENDING state
        runningTasksCount (int): number of tasks in the cluster that are in the RUNNING state
        registeredContainerInstancesCount (int): The number of container instances registered
            into the cluster. This includes container instances in both ACTIVE and DRAINING status.
        minContainers (int): The minimum size of the Auto Scaling group.
        maxContainers (int): The maximum size of the Auto Scaling Group
        status: (string): Status of the cluster, either [ACTIVE, PROVISIONING,
            DEPROVISINING, FAILED, INACTIVE]
    """

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

    Attributes:
        region_name: The name of the region to which the AMI corresponds as a string.
        ami_id: A string representing the AMI ID of the latest AMI provisioned in the region
            corresponding to this row.
        allowed: A boolean indicating whether or not users are allowed to deploy tasks in the
            region corresponding to this row.
    """

    __tablename__ = "region_to_ami"
    __table_args__ = {"extend_existing": True, "schema": "hardware"}
    region_name = db.Column(db.String(250), nullable=False, unique=True, primary_key=True)
    ami_id = db.Column(db.String(250), nullable=False)
    allowed = db.Column(db.Boolean, nullable=False, server_default=true())


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
    """Contains info for website banners, such as picture URL, headings, and subheadings

    Attributes:
        heading (string): banner heading
        subheading (string) banner subheading
        category (string): banner category
        background (string): banner image url
        url (string): url to any external links
    """

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
    ip = db.Column(db.String(250))
    client_app_auth_secret = db.Column(db.String(250))
    port = db.Column(db.Integer)
    task_id = db.Column(db.String(250), nullable=False)
