/*
 * General Fractal helper functions and headers.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/

#include "fractal.h"  // header file for this protocol, includes winsock
#include "../utils/logging.h"

#include <ctype.h>
#include <stdio.h>

#include "../utils/sysinfo.h"

char sentry_environment[FRACTAL_ARGS_MAXLEN + 1];
bool using_sentry = false;

// Print Memory Info

int multithreaded_print_system_info(void* opaque) {
    UNUSED(opaque);

    LOG_INFO("Hardware information:");

    print_os_info();
    print_model_info();
    print_cpu_info();
    print_ram_info();
    print_monitors();
    print_hard_drive_info();

    return 0;
}

void print_system_info() {
    FractalThread sysinfo_thread =
        fractal_create_thread(multithreaded_print_system_info, "print_system_info", NULL);
    fractal_detach_thread(sysinfo_thread);
}

DynamicBuffer* init_dynamic_buffer(bool use_memory_regions) {
    DynamicBuffer* db = safe_malloc(sizeof(DynamicBuffer));
    db->size = 0;
    db->use_memory_regions = use_memory_regions;
    if (db->use_memory_regions) {
        // We have to allocate a page anyway,
        // So we can start off the capacity large
        db->capacity = 4096;
        db->buf = allocate_region(db->capacity);
    } else {
        db->capacity = 128;
        db->buf = safe_malloc(db->capacity);
    }
    // No need to check db->buf because safe_malloc and allocate_region already do
    return db;
}

void resize_dynamic_buffer(DynamicBuffer* db, size_t new_size) {
    size_t new_capacity = db->capacity;
    // If the capacity is too large, keep halving it
    while (new_capacity / 4 > new_size) {
        new_capacity /= 2;
    }
    // If the new capacity is too small, keep doubling it
    while (new_capacity < new_size) {
        new_capacity *= 2;
    }

    if (db->use_memory_regions) {
        new_capacity = max(new_capacity, 4096);
    } else {
        new_capacity = max(new_capacity, 128);
    }

    // If the desired capacity has changed, realloc
    if (db->capacity != new_capacity) {
        char* new_buffer;
        if (db->use_memory_regions) {
            new_buffer = realloc_region(db->buf, new_capacity);
        } else {
            new_buffer = safe_realloc(db->buf, new_capacity);
        }
        // Update the capacity and buffer
        db->capacity = new_capacity;
        db->buf = new_buffer;
    }

    // Update the size of the dynamic buffer
    db->size = new_size;
}

void free_dynamic_buffer(DynamicBuffer* db) {
    if (db->use_memory_regions) {
        deallocate_region(db->buf);
    } else {
        free(db->buf);
    }
    free(db);
}

int runcmd(const char* cmdline, char** response) {
#ifdef _WIN32
    HANDLE h_child_std_in_rd = NULL;
    HANDLE h_child_std_in_wr = NULL;
    HANDLE h_child_std_out_rd = NULL;
    HANDLE h_child_std_out_wr = NULL;

    SECURITY_ATTRIBUTES sa_attr;

    // Set the bInheritHandle flag so pipe handles are inherited.

    sa_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa_attr.bInheritHandle = TRUE;
    sa_attr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT.

    if (response) {
        if (!CreatePipe(&h_child_std_out_rd, &h_child_std_out_wr, &sa_attr, 0)) {
            LOG_ERROR("StdoutRd CreatePipe failed");
            *response = NULL;
            return -1;
        }
        if (!SetHandleInformation(h_child_std_out_rd, HANDLE_FLAG_INHERIT, 0)) {
            LOG_ERROR("Stdout SetHandleInformation failed");
            *response = NULL;
            return -1;
        }
        if (!CreatePipe(&h_child_std_in_rd, &h_child_std_in_wr, &sa_attr, 0)) {
            LOG_ERROR("Stdin CreatePipe failed");
            *response = NULL;
            return -1;
        }
        if (!SetHandleInformation(h_child_std_in_wr, HANDLE_FLAG_INHERIT, 0)) {
            LOG_ERROR("Stdin SetHandleInformation failed");
            *response = NULL;
            return -1;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (response) {
        si.hStdError = h_child_std_out_wr;
        si.hStdOutput = h_child_std_out_wr;
        si.hStdInput = h_child_std_in_rd;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    ZeroMemory(&pi, sizeof(pi));

    char cmd_buf[1000];

    while (cmdline[0] == ' ') {
        cmdline++;
    }

    if (strlen((const char*)cmdline) + 1 > sizeof(cmd_buf)) {
        LOG_WARNING("runcmd cmdline too long!");
        if (response) {
            *response = NULL;
        }
        return -1;
    }

    memcpy(cmd_buf, cmdline, strlen((const char*)cmdline) + 1);

    if (CreateProcessA(NULL, (LPSTR)cmd_buf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                       &pi)) {
    } else {
        LOG_ERROR("CreateProcessA failed!");
        if (response) {
            *response = NULL;
        }
        return -1;
    }

    if (response) {
        CloseHandle(h_child_std_out_wr);
        CloseHandle(h_child_std_in_rd);

        CloseHandle(h_child_std_in_wr);

        DWORD dw_read;
        CHAR ch_buf[2048];
        BOOL b_success = FALSE;

        DynamicBuffer* db = init_dynamic_buffer(false);
        for (;;) {
            b_success = ReadFile(h_child_std_out_rd, ch_buf, sizeof(ch_buf), &dw_read, NULL);
            if (!b_success || dw_read == 0) break;

            size_t original_size = db->size;
            resize_dynamic_buffer(db, original_size + dw_read);
            memcpy(db->buf + original_size, ch_buf, dw_read);
            if (!b_success) break;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        resize_dynamic_buffer(db, db->size + 1);
        db->buf[db->size] = '\0';
        resize_dynamic_buffer(db, db->size - 1);
        int size = (int)db->size;
        // The caller will have to free this later
        *response = db->buf;
        // Free the DynamicBuffer struct
        free(db);
        return size;
    } else {
        CloseHandle(h_child_std_out_wr);
        CloseHandle(h_child_std_in_rd);
        CloseHandle(h_child_std_in_wr);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
#else
    if (response == NULL) {
        system(cmdline);
        return 0;
    } else {
        FILE* p_pipe;

        /* Run DIR so that it writes its output to a pipe. Open this
         * pipe with read text attribute so that we can read it
         * like a text file.
         */

        char* cmd = safe_malloc(strlen(cmdline) + 128);
        snprintf(cmd, strlen(cmdline) + 128, "%s 2>/dev/null", cmdline);

        if ((p_pipe = popen(cmd, "r")) == NULL) {
            LOG_WARNING("Failed to popen %s", cmd);
            free(cmd);
            return -1;
        }
        free(cmd);

        /* Read pipe until end of file, or an error occurs. */

        int current_len = 0;
        DynamicBuffer* db = init_dynamic_buffer(false);

        while (true) {
            char c = (char)fgetc(p_pipe);

            resize_dynamic_buffer(db, current_len + 1);

            if (c == EOF) {
                db->buf[current_len] = '\0';
                break;
            } else {
                db->buf[current_len] = c;
                current_len++;
            }
        }

        // The caller will have to free this later
        *response = db->buf;

        // Free the DynamicBuffer struct
        free(db);

        /* Close pipe and print return value of pPipe. */
        if (feof(p_pipe)) {
            return current_len;
        } else {
            LOG_WARNING("Error: Failed to read the pipe to the end.");
            *response = NULL;
            return -1;
        }
    }
#endif
}

bool read_hexadecimal_private_key(char* hex_string, char* binary_private_key,
                                  char* hex_private_key) {
    // It looks wasteful to convert from string to binary and back, but we need
    // to validate the hex string anyways, and it's easier to see exactly the
    // format in which we're storing it (big-endian).

    if (strlen(hex_string) != 32) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        if (!isxdigit(hex_string[2 * i]) || !isxdigit(hex_string[2 * i + 1])) {
            return false;
        }
        sscanf(&hex_string[2 * i], "%2hhx", &(binary_private_key[i]));
    }

    snprintf(hex_private_key, 33, "%08X%08X%08X%08X",
             (unsigned int)htonl(*((uint32_t*)(binary_private_key))),
             (unsigned int)htonl(*((uint32_t*)(binary_private_key + 4))),
             (unsigned int)htonl(*((uint32_t*)(binary_private_key + 8))),
             (unsigned int)htonl(*((uint32_t*)(binary_private_key + 12))));

    return true;
}

int get_fmsg_size(FractalClientMessage* fmsg) {
    if (fmsg->type == MESSAGE_KEYBOARD_STATE || fmsg->type == MESSAGE_DISCOVERY_REQUEST) {
        return sizeof(*fmsg);
    } else if (fmsg->type == CMESSAGE_CLIPBOARD) {
        return sizeof(*fmsg) + fmsg->clipboard.size;
    } else {
        // Send small fmsg's when we don't need unnecessarily large ones
        return sizeof(fmsg->type) + 40;
    }
}

void terminate_protocol() {
    LOG_INFO("Terminating Protocol");
    destroy_logger();
    print_stacktrace();
    exit(-1);
}

void* safe_malloc(size_t size) {
    void* ret = malloc(size);
    if (ret == NULL) {
        LOG_FATAL("Malloc of size %d failed!", size);
    } else {
        return ret;
    }
}

void* safe_realloc(void* buffer, size_t new_size) {
    void* ret = realloc(buffer, new_size);
    if (ret == NULL) {
        LOG_FATAL("Realloc of size %d failed!", new_size);
    } else {
        return ret;
    }
}

bool safe_strncpy(char* destination, const char* source, size_t num) {
    /*
     * Safely copy a string from source to destination.
     *
     * Copies at most `num` bytes * after the first null character of `source` are not copied.
     * If no null character is encountered within the first `num` bytes
     * of `source`, `destination[num - 1]` will be manually set to zero,
     * so `destination` is guaranteed to be null terminated, unless
     * `num` is zero, in which case the `destination` is left unchanged.
     *
     * Arguments:
     *     destination: Address to copy to. Should have at least `num` bytes available.
     *     source: Address to copy from.
     *     num: Number of bytes to copy.
     *
     * Return:
     *     True if all bytes of source were copied (i.e. strlen(source) <= num - 1)
     */
    if (num > 0) {
        size_t i;
        for (i = 0; i < num - 1 && source[i] != '\0'; i++) {
            destination[i] = source[i];
        }
        destination[i] = '\0';
        return source[i] == '\0';
    }
    return false;
}

// Include FRACTAL_GIT_REVISION
#include <fractal.v>
// Defines to stringize a macro
#define xstr(s) str(s)
#define str(s) #s
// Return git revision as string, or "none" if no git revision found
char* fractal_git_revision() {
#ifdef FRACTAL_GIT_REVISION
    return xstr(FRACTAL_GIT_REVISION);
#else
    return "none";
#endif
}

// ------------------------------------
// Implementation of a block allocator
// that allocates blocks of constant size
// and maintains a free list of recently freed blocks
// ------------------------------------

#define MAX_FREES 1024

// The internal block allocator struct,
// which is what a BlockAllocator* actually points to
typedef struct {
    size_t block_size;

    int num_allocated_blocks;

    // Free blocks
    int num_free_blocks;
    char* free_blocks[MAX_FREES];
} InternalBlockAllocator;

BlockAllocator* create_block_allocator(size_t block_size) {
    // Create a block allocator
    InternalBlockAllocator* blk_allocator = safe_malloc(sizeof(InternalBlockAllocator));

    // Set block allocator values
    blk_allocator->num_allocated_blocks = 0;
    blk_allocator->block_size = block_size;
    blk_allocator->num_free_blocks = 0;

    return (BlockAllocator*)blk_allocator;
}

void* allocate_block(BlockAllocator* blk_allocator_in) {
    InternalBlockAllocator* blk_allocator = (InternalBlockAllocator*)blk_allocator_in;

    // If a free block already exists, just use that one instead
    if (blk_allocator->num_free_blocks > 0) {
        char* block = blk_allocator->free_blocks[--blk_allocator->num_free_blocks];
        mark_used_region(block);
        return block;
    }

    // Otherwise, create a new block
    blk_allocator->num_allocated_blocks++;
    char* block = allocate_region(blk_allocator->block_size);
    // Return the newly allocated block
    return block;
}

void free_block(BlockAllocator* blk_allocator_in, void* block) {
    InternalBlockAllocator* blk_allocator = (InternalBlockAllocator*)blk_allocator_in;

    // If there's room in the free block list, just store the free block there instead
    if (blk_allocator->num_free_blocks < MAX_FREES) {
        mark_unused_region(block);
        blk_allocator->free_blocks[blk_allocator->num_free_blocks++] = block;
    } else {
        // Otherwise, actually free the block at an OS-level
        deallocate_region(block);
        blk_allocator->num_allocated_blocks--;
    }
}

// ------------------------------------
// Implementation of an allocator that
// allocates regions directly from mmap/MapViewOfFile
// ------------------------------------

#ifdef _WIN32
#include <memoryapi.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

// Get the page size
int get_page_size() {
#ifdef _WIN32
    SYSTEM_INFO sys_info;

    // Copy the hardware information to the SYSTEM_INFO structure.
    GetSystemInfo(&sys_info);
    return sys_info.dwPageSize;
#else
    return getpagesize();
#endif
}

// Declare the RegionHeader struct
typedef struct {
    size_t size;
} RegionHeader;
// Macros to go between the data and the header of a region
#define TO_REGION_HEADER(a) ((void*)(((char*)a) - sizeof(RegionHeader)))
#define TO_REGION_DATA(a) ((void*)(((char*)a) + sizeof(RegionHeader)))

void* allocate_region(size_t region_size) {
    size_t page_size = get_page_size();
    // Make space for the region header as well
    region_size += sizeof(RegionHeader);
    // Round up to the nearest page size
    region_size = region_size + (page_size - (region_size % page_size)) % page_size;

#ifdef _WIN32
    void* p = VirtualAlloc(NULL, region_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL) {
        LOG_FATAL("Could not VirtualAlloc. Error %x", GetLastError());
    }
    ((RegionHeader*)p)->size = region_size;
#else
    void* p = mmap(0, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == NULL) {
        LOG_FATAL("mmap failed!");
    }
    ((RegionHeader*)p)->size = region_size;
#endif
    return TO_REGION_DATA(p);
}

void mark_unused_region(void* region) {
    RegionHeader* p = TO_REGION_HEADER(region);
    size_t page_size = get_page_size();
    // Only mark the next page and beyond as freed,
    // since we need to maintain the RegionHeader
    if (p->size > page_size) {
        char* next_page = (char*)p + page_size;
        size_t advise_size = p->size - page_size;
#ifdef _WIN32
        // Offer the Virtual Memory up so that task manager knows we're not using those pages
        // anymore
        OfferVirtualMemory(next_page, advise_size, VmOfferPriorityNormal);
#elif __APPLE__
        // Lets you tell the Apple Task Manager to report correct memory usage
        madvise(next_page, advise_size, MADV_FREE_REUSABLE);
#else
        // Linux won't update `top`, but it will have the correct OOM semantics
        madvise(next_page, advise_size, MADV_FREE);
#endif
    }
}

void mark_used_region(void* region) {
    RegionHeader* p = TO_REGION_HEADER(region);
    size_t page_size = get_page_size();
    if (p->size > page_size) {
        char* next_page = (char*)p + page_size;
        size_t advise_size = p->size - page_size;
#ifdef _WIN32
        // Reclaim the virtual memory for usage again
        ReclaimVirtualMemory(next_page, advise_size);
#elif __APPLE__
        // Tell the Apple Task Manager that we'll use this memory again
        // Apparently we can lie to their Task Manager by not calling this
        // Hm.
        madvise(next_page, advise_size, MADV_FREE_REUSE);
#else
        // Do Nothing, Linux will know when you touch the memory again
        UNUSED(next_page);
        UNUSED(advise_size);
#endif
    }
}

void* realloc_region(void* region, size_t new_region_size) {
    RegionHeader* p = TO_REGION_HEADER(region);
    size_t region_size = p->size;

    // Allocate new region
    void* new_region = allocate_region(new_region_size);
    // Copy the actual data over, truncating to new_region_size if there's not enough space
    memcpy(new_region, region, min(region_size - sizeof(RegionHeader), new_region_size));
    // Allocate the old region
    deallocate_region(region);

    // Return the new region
    return new_region;
}

void deallocate_region(void* region) {
    RegionHeader* p = TO_REGION_HEADER(region);

#ifdef _WIN32
    if (VirtualFree(p, 0, MEM_RELEASE) == 0) {
        LOG_FATAL("VirtualFree failed! Error %x", GetLastError());
    }
#else
    if (munmap(p, p->size) != 0) {
        LOG_FATAL("munmap failed!");
    }
#endif
}
