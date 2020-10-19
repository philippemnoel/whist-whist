from app import *
from app.helpers.utils.azure.azure_general import *
from app.helpers.utils.azure.azure_resource_creation import *
from app.helpers.utils.azure.azure_resource_state_management import *
from app.helpers.utils.azure.azure_resource_locks import *
from app.helpers.utils.azure.azure_resource_modification import *

from app.models.hardware import *
from app.serializers.hardware import *

user_vm_schema = UserVMSchema()
os_disk_schema = OSDiskSchema()


@celery_instance.task(bind=True)
def swapSpecificDisk(self, vm_name, disk_name, resource_group=VM_GROUP):
    """Swaps out a disk in a vm

    Args:
        disk_name (str): The name of the disk to swap out
        vm_name (str): The name of the vm the disk is attached to
        ID (int, optional): Papertrail logging ID. Defaults to -1.

    Returns:
        json: VM info
    """

    fractalLog(
        function="swapSpecificDisk",
        label=str(vm_name),
        logs="Beginning function to swap disk {disk_name} into VM {vm_name}".format(
            disk_name=disk_name, vm_name=vm_name
        ),
    )

    if spinLock(vm_name, resource_group=resource_group) < 0:
        return {"status": REQUEST_TIMEOUT, "payload": None}

    lockVMAndUpdate(vm_name=vm_name, state="ATTACHING", lock=True, temporary_lock=3)

    attachDiskToVM(disk_name, vm_name, resource_group)

    fractalVMStart(vm_name, resource_group=resource_group, s=self)

    lockVMAndUpdate(
        vm_name=vm_name,
        state="RUNNING_AVAILABLE",
        lock=False,
        temporary_lock=0,
        resource_group=resource_group,
    )

    vm = UserVM.query.get(vm_name)
    vm = user_vm_schema.dump(vm)

    return {"status": SUCCESS, "payload": vm}


@celery_instance.task(bind=True)
def deployArtifact(self, vm_name, artifact_name, run_id, resource_group=VM_GROUP):
    """Swaps out a disk in a vm

    Args:
        vm_name (str): The name of the vm
        artifact_name (str): Name of artifact
        run_id (str): Run ID

    Returns:
        json: Success/failure
    """

    _, compute_client, _ = createClients()

    with open(
        os.path.join(app.config["ROOT_DIRECTORY"], "scripts/deployArtifact.txt"), "r"
    ) as file:
        fractalLog(
            function="deployArtifact",
            label=str(vm_name),
            logs="Starting to run deploy artifact script",
        )

        command = file.read().replace("ARTIFACT_NAME", artifact_name).replace("RUN_ID", str(run_id))
        run_command_parameters = {"command_id": "RunPowerShellScript", "script": [command]}

        poller = compute_client.virtual_machines.run_command(
            resource_group, vm_name, run_command_parameters
        )

        result = poller.result()

        fractalLog(
            function="deployArtifact",
            label=str(vm_name),
            logs="Artifact script finished running. Output: {output}".format(
                output=str(result.value[0])
            ),
        )

    return {"status": SUCCESS, "payload": str(result.value[0])}


@celery_instance.task(bind=True)
def runPowershell(self, vm_name, command, resource_group=VM_GROUP):
    """Runs a powershell script on a VM

    Args:
        vm_name (str): The name of the vm
        command (str): Powershell script
        resource_group (str): Resource group of VM

    Returns:
        json: Success/failure
    """

    if spinLock(vm_name, resource_group=resource_group, s=self) > 0:
        lockVMAndUpdate(
            vm_name=vm_name,
            state=None,
            lock=True,
            temporary_lock=None,
            resource_group=resource_group,
        )

        _, compute_client, _ = createClients()

        fractalLog(
            function="runPowershell",
            label=getVMUser(vm_name),
            logs="Starting to run Powershell on VM {vm_name}".format(vm_name=vm_name),
        )

        run_command_parameters = {"command_id": "RunPowerShellScript", "script": [command]}

        poller = compute_client.virtual_machines.run_command(
            resource_group, vm_name, run_command_parameters
        )

        result = poller.result()

        fractalLog(
            function="runPowershell",
            label=getVMUser(vm_name),
            logs="Artifact script finished running. Output: {output}".format(
                output=str(result.value[0])
            ),
        )

        lockVMAndUpdate(
            vm_name=vm_name,
            state=None,
            lock=False,
            temporary_lock=None,
            resource_group=resource_group,
        )

        return {"status": SUCCESS, "payload": str(result.value[0])}

    return {"status": REQUEST_TIMEOUT, "payload": None}


@celery_instance.task(bind=True)
def automaticAttachDisk(self, disk_name, resource_group=VM_GROUP):
    """Find an available VM to attach a disk to

    Args:
        disk_name (str): The name of the disk to swap out

    Returns:
        json:
    """

    def swapDiskAndUpdate(disk_name, vm_name, needs_winlogon, resource_group, s=None):
        s.update_state(
            state="PENDING",
            meta={
                "msg": "Uploading the necessary data to our servers. This could take a few minutes."
            },
        )

        if attachDiskToVM(disk_name, vm_name, resource_group) < 1:
            return -1

        if (
            fractalVMStart(
                vm_name,
                needs_restart=True,
                needs_winlogon=needs_winlogon,
                resource_group=resource_group,
                s=s,
            )
            < 1
        ):
            self.update_state(
                state="FAILURE",
                meta={"msg": "Fractal could not start. Please contact support."},
            )

            return -1

        return 1

    # Create an Azure instance of disk

    _, compute_client, _ = createClients()
    os_disk = compute_client.disks.get(resource_group, disk_name)

    # Figure out if the disk is Windows or Linux

    os_type = "Windows" if "windows" in str(os_disk.os_type) else "Linux"
    needs_winlogon = os_type == "Windows"

    # Get the username associated with the disk

    username = None
    disk = OSDisk.query.get(disk_name)

    if disk:
        username = disk.user_id

    # Get the VM and location of the disk

    vm_name = os_disk.managed_by

    location = os_disk.location
    vm_attached = True if vm_name else False

    # If disk is already attached to a VM, start the VM and attach any secondary disks

    fractalLog(
        function="automaticAttachDisk",
        label=str(username),
        logs="Starting to automatically attach disk {disk_name}".format(disk_name=disk_name),
    )

    if vm_attached:
        fractalLog(function="automaticAttachDisk", label=str(username), logs="ALREADY ATTACHED")

        self.update_state(
            state="PENDING",
            meta={"msg": "Boot request received successfully. Preparing to launch Fractal."},
        )

        vm_name = vm_name.split("/")[-1]
        unlocked = False

        fractalLog(
            function="automaticAttachDisk",
            label=str(username),
            logs="{disk_name} is already attached to VM {vm_name}".format(
                disk_name=disk_name, vm_name=vm_name
            ),
        )

        while not unlocked and vm_attached:
            # Wait to gain access to the VM

            if spinLock(vm_name, resource_group=resource_group, s=self) > 0:
                # Access gained, lock the VM for ourselves

                fractalLog(
                    function="automaticAttachDisk",
                    label=str(username),
                    logs="Updating database and starting VM {vm_name}".format(vm_name=vm_name),
                )

                unlocked = True
                lockVMAndUpdate(
                    vm_name=vm_name,
                    state="ATTACHING",
                    lock=True,
                    temporary_lock=None,
                    resource_group=resource_group,
                )

                # Update database to make sure that the VM is associated with the correct disk
                # and username

                vm = UserVM.query.filter_by(vm_id=vm_name)
                vm.disk_id = disk_name
                vm.user_id = username
                vm.location = location
                db.session.commit()

                self.update_state(
                    state="PENDING",
                    meta={"msg": "Database updated. Sending signal to boot your cloud PC."},
                )

                if (
                    fractalVMStart(
                        vm_name,
                        needs_winlogon=needs_winlogon,
                        resource_group=resource_group,
                        s=self,
                    )
                    > 0
                ):
                    self.update_state(state="PENDING", meta={"msg": "Cloud PC is ready to use."})
                else:
                    self.update_state(
                        state="FAILURE",
                        meta={"msg": "Cloud PC could not be started. Please contact support."},
                    )

                attachSecondaryDisks(username, vm_name, resource_group, s=self)

                lockVMAndUpdate(
                    vm_name=vm_name,
                    state="RUNNING_AVAILABLE",
                    lock=False,
                    temporary_lock=1,
                    resource_group=resource_group,
                )

                vm = UserVM.query.get(vm_name)

                if vm:
                    vm = user_vm_schema.dump(vm)
                    return vm
                else:
                    return {}

            else:
                os_disk = compute_client.disks.get(resource_group, disk_name)
                vm_name = os_disk.managed_by
                location = os_disk.location
                vm_attached = True if vm_name else False

                if vm_attached:
                    vm_name = vm_name.split("/")[-1]

    if not vm_attached:
        self.update_state(
            state="PENDING",
            meta={"msg": "Boot request received successfully. Fetching your cloud PC."},
        )

        disk_attached = False
        while not disk_attached:
            fractalLog(
                function="automaticAttachDisk", label=str(username), logs="DISKNO T ATTACHED"
            )
            vm = claimAvailableVM(username, disk_name, location, resource_group, os_type, s=self)
            if vm:
                # try:
                vm_name = vm["vm_id"]

                vm_info = compute_client.virtual_machines.get(resource_group, vm_name)

                for disk in vm_info.storage_profile.data_disks:
                    if disk.name != disk_name:
                        self.update_state(
                            state="PENDING",
                            meta={"msg": "Making sure that you have a stable connection."},
                        )

                        detachSecondaryDisk(disk.name, vm_name, resource_group)

                if (
                    swapDiskAndUpdate(disk_name, vm_name, needs_winlogon, resource_group, s=self)
                    > 0
                ):
                    self.update_state(
                        state="PENDING", meta={"msg": "Data successfully uploaded to cloud PC."}
                    )

                    attachSecondaryDisks(username, vm_name, resource_group, s=self)

                    lockVMAndUpdate(
                        vm_name=vm_name,
                        state="RUNNING_AVAILABLE",
                        lock=False,
                        temporary_lock=1,
                        resource_group=resource_group,
                    )

                    vm = UserVM.query.get(vm_name)

                    if vm:
                        vm = user_vm_schema.dump(vm)
                        return vm
                    else:
                        return {}

                attachSecondaryDisks(username, vm_name, resource_group, s=self)

                lockVMAndUpdate(
                    vm_name=vm_name,
                    state="RUNNING_AVAILABLE",
                    lock=False,
                    temporary_lock=1,
                    resource_group=resource_group,
                )

                disk_attached = True

                vm = UserVM.query.filter_by(vm_id=vm_name)

                if vm:
                    vm = user_vm_schema.dump(vm)
                    return vm
                else:
                    return {}
                # except Exception as e:
                #     fractalLog(
                #         function="automaticAttachDisk",
                #         label=username,
                #         logs="Critical error attaching disk: {error}".format(
                #             error=str(e)
                #         ),
                #         level=logging.CRITICAL,
                #     )

            else:
                self.update_state(
                    state="PENDING",
                    meta={"msg": "Running performance tests. This could take a few extra minutes."},
                )
                fractalLog(
                    function="automaticAttachDisk",
                    label=username,
                    logs="No VMs available, waiting 30 seconds...",
                    level=logging.WARNING,
                )
                time.sleep(30)

    self.update_state(
        state="FAILURE", meta={"msg": "Cloud PC could not be started. Please contact support."}
    )
    return None
