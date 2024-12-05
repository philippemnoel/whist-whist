#include <whist/core/whist.h>
#include <mimalloc/mimalloc.h>
#include "whist_memory.h"
#include <mach/mach_vm.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/mman.h>
#include <string.h>

// Helper function for locking statics

// documentation notes: the source code for mach/etc are in https://github.com/apple/darwin-xnu/.
// mach_vm_region(vm_map_t map,
// mach_vm_address_t *address,
// mach_vm_size_t *size,
// vm_region_flavor_t flavor,
// vm_region_info_t info,
// mach_msg_type_number_t *cnt,
// mach_port_t *object_name)
// this displays information about the VM Region for virtual memory address address for task map
// there are two "flavors", but we only need the basic one (and in fact the extended flavor takes a
// lot longer to run) output is: - address: starting address for the region
// - size: size of the region
// - info : returned info struct (we don't use this at all)
// - count: the # of entries of structs corresponding to the flavor (we don't use this either)
// - obj_name: some other output, also unused
static void mlock_statics() {
    // NOTE: this address is empirical
    // I didn't figure out a way to get the end of static memory, but I found that the static
    // variables definitely lived before this address
    static mach_vm_address_t max_addr = (mach_vm_address_t)0x200000000;
    task_t t = mach_task_self();

    mach_vm_address_t addr = 1;
    kern_return_t rc;  // return value for mach_vm_region
    vm_region_basic_info_data_t info;
    mach_vm_address_t prev_addr = 0;
    mach_vm_size_t size, prev_size;

    mach_port_t obj_name;
    mach_msg_type_number_t count;

    int done = 0;
    // starting from address 1, use mach_vm_region to obtain the address and size of the next region
    // repeat until we've mlock'ed all static memory
    while (!done) {
        count = VM_REGION_BASIC_INFO_COUNT_64;

        rc = mach_vm_region(t, &addr, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &count,
                            &obj_name);
        if (rc) {
            // indicates that we've given an invalid address.
            LOG_ERROR("mach_vm_region_failed, %s", mach_error_string(rc));
            max_addr = addr;
            done = 1;
        } else {
            // check for overflow; all VM regions are distinct, so this should not error out until
            // we have reached the end of the memory we want to mlock
            if (addr <= max_addr) {
                // if the addr is <, that implies the regions overlapped or we overflowed, which
                // shouldn't happen
                FATAL_ASSERT(addr >= prev_addr + prev_size);
                // update region list data and mlock
                int ret = mlock((void*)addr, size);
                // the error check is LOG_INFO because we expect mlock to fail on
                // unmapped/unallocated regions here
                if (ret != 0) {
                    LOG_INFO("mlock at addr %p size 0x%llx failed with error %s", (void*)addr, size,
                             strerror(errno));
                }
                prev_addr = addr;
                prev_size = size;
                // repeatedly call mach_vm_region
                addr = prev_addr + prev_size;
                // means address space has wrapped around
                if (prev_size == 0 || addr >= max_addr) {
                    // finished because we've wrapped around
                    done = 1;
                }
            } else {
                done = 1;
            }
        }
    }
}

void init_whist_mlock() {
    // this makes it so that resets and unresets happen once a second instead of once every 100ms,
    // the default
    mi_option_set(mi_option_reset_delay, 1000);
    LOG_INFO("mlocking statics");
    mlock_statics();
}
