from app import *
from .helperFuncs import *

@celery.task(bind = True)
def createVM(self, vm_size, location):
    _, compute_client, _ = createClients()
    vmName = genVMName()
    nic = createNic(vmName, location, 0)
    if not nic: 
    	return jsonify({})
    vmParameters = createVMParameters(vmName, nic.id, vm_size, location)
    async_vm_creation = compute_client.virtual_machines.create_or_update(
        os.environ.get('VM_GROUP'), vmParameters['vmName'], vmParameters['params'])
    async_vm_creation.wait()
    async_vm_start = compute_client.virtual_machines.start(
        os.environ.get('VM_GROUP'), vmParameters['vmName'])
    async_vm_start.wait()
    return fetchVMCredentials(vmParameters['vmName'])

@celery.task(bind = True)
def fetchAll(self, update):
    _, compute_client, _ = createClients()
    vms = {'value': []}
    azure_portal_vms = compute_client.virtual_machines.list(os.getenv('VM_GROUP'))
    vm_usernames = []
    vm_names = []
    current_usernames = []
    current_names = []

    if update:
        current_vms = fetchUserVMs(None)
        current_usernames = [current_vm['vm_username'] for current_vm in current_vms]
        current_names = [current_vm['vm_name'] for current_vm in current_vms]

    for entry in azure_portal_vms:
        vm = getVM(entry.name)
        vm_ip = getIP(vm)
        vm_info = {
            'vm_name': entry.name,
            'username': entry.os_profile.admin_username,
            'ip': vm_ip
        }
        
        if entry.name in current_names:
            for vm in current_vms:
                if entry.name == vm['vm_name']:
                    vm_info['username'] = vm['vm_username']

        vms['value'].append(vm_info)
        vm_usernames.append(entry.os_profile.admin_username)
        vm_names.append(entry.name)

        if update:
            try:
                if not entry.name in current_names:
                    insertRow(entry.os_profile.admin_username, entry.name, current_usernames, current_names)
            except:
                pass

    if update:
        for current_vm in current_vms:
            print(current_vm['vm_name'])
            deleteRow(current_vm['vm_username'], current_vm['vm_name'], vm_usernames, vm_names)

    return vms
