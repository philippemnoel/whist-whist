from tests import *


def fetchCurrentDisks():
    output = fractalSQLSelect(table_name="disks", params={})
    return output["rows"]


def deleteDisk(disk_name, resource_group, input_token):
    return requests.post(
        (SERVER_URL + "/azure_disk/delete"),
        json={"disk_name": disk_name, "resource_group": resource_group},
        headers={"Authorization": "Bearer " + input_token},
    )


def cloneDisk(
    username, location, vm_size, operating_system, apps, resource_group, input_token
):
    return requests.post(
        (SERVER_URL + "/azure_disk/clone"),
        json={
            "username": username,
            "location": location,
            "vm_size": vm_size,
            "operating_system": operating_system,
            "apps": apps,
            "resource_group": resource_group,
        },
        headers={"Authorization": "Bearer " + input_token},
    )
