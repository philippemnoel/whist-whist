from app import *
from app.helpers.utils.general.auth import *
from app.helpers.blueprint_helpers.azure.azure_disk_post import *
from app.helpers.utils.azure.azure_general import *

from app.celery.azure_resource_creation import *
from app.celery.azure_resource_deletion import *
from app.celery.azure_resource_modification import *

from app.models.hardware import *

azure_disk_bp = Blueprint("azure_disk_bp", __name__)


@azure_disk_bp.route("/azure_disk/<action>", methods=["POST"])
@fractalPreProcess
@jwt_required
@fractalAuth
def azure_disk_post(action, **kwargs):
    if action == "clone":
        # Clone a Fractal disk

        branch = "master"
        if "branch" in kwargs["body"].keys():
            branch = kwargs["body"]["branch"]

        task = cloneDisk.apply_async(
            [
                kwargs["body"]["username"],
                kwargs["body"]["location"],
                kwargs["body"]["vm_size"],
                kwargs["body"]["operating_system"],
                branch,
                kwargs["body"]["apps"],
                kwargs["body"]["resource_group"],
            ]
        )

        if not task:
            return jsonify({"ID": None}), BAD_REQUEST

        return jsonify({"ID": task.id}), ACCEPTED

    elif action == "delete":
        # Delete a disk from Azure and database

        resource_group = VM_GROUP
        if "resource_group" in kwargs["body"].keys():
            resource_group = kwargs["body"]["resource_group"]

        if "username" in kwargs["body"].keys():
            username = kwargs["body"]["username"]

            disks = OSDisk.query.filter_by(user_id=username).all()

            if disks:
                task = None
                for disk in disks:
                    task = deleteDisk.apply_async([disk.disk_id, resource_group])

                return jsonify({"ID": task.id}), ACCEPTED

        elif "disk_name" in kwargs["body"].keys():
            disk_name = kwargs["body"]["disk_name"]

            task = deleteDisk.apply_async([disk_name, resource_group])
            return jsonify({"ID": task.id}), ACCEPTED

        return jsonify({"ID": None}), BAD_REQUEST

    elif action == "attach":
        # Find a VM to attach disk to

        disk_name, resource_group = (
            kwargs["body"]["disk_name"],
            kwargs["body"]["resource_group"],
        )

        if not checkResourceGroup(resource_group):
            return jsonify({"ID": None}), BAD_REQUEST

        attach_to_specific_vm = False
        task = None

        if "vm_name" in kwargs["body"].keys():
            vm_name = kwargs["body"]["vm_name"]
            if vm_name:
                attach_to_specific_vm = True
                task = swapSpecificDisk.apply_async(
                    [vm_name, disk_name, resource_group]
                )

        if not attach_to_specific_vm:
            task = automaticAttachDisk.apply_async([disk_name, resource_group])

        if not task:
            return jsonify({"ID": None}), BAD_REQUEST

        return jsonify({"ID": task.id}), ACCEPTED

    elif action == "create":
        # Create a blank, default disk

        disk_size, username = kwargs["body"]["disk_size"], kwargs["body"]["username"]
        location, resource_group = (
            kwargs["body"]["location"],
            kwargs["body"]["resource_group"],
        )
        operating_system = kwards["body"]["operating_system"]

        output = createHelper(disk_size, username, location, resource_group, operating_system)

        return jsonify({"ID": output["ID"]}), output["status"]

    elif action == "stun":
        # Toggle whether a disk uses the STUN server

        using_stun, disk_name = (
            kwargs["body"]["using_stun"],
            kwargs["body"]["disk_name"],
        )

        output = stunHelper(using_stun, disk_name)

        return jsonify(output), output["status"]
    elif action == "branch":
        # Toggle which protocol branch to run on (master, staging, or dev)

        disk_name, branch = (kwargs["body"]["disk_name"], kwargs["body"]["branch"])
        output = branchHelper(branch, disk_name)

        return jsonify(output), output["status"]
