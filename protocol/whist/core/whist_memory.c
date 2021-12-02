/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file whist_memory.c
 * @brief This file contains custom memory handling for Whist.

============================
Usage
============================
clipboard = allocate_region(sizeof(ClipboardData) + cb->size);
memcpy(clipboard, cb, sizeof(ClipboardData) + cb->size);
deallocate_region(clipboard);
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>

/*
============================
Public Function Implementations
============================
*/

void* safe_malloc(size_t size) {
    /*
        Wrapper around malloc that will correctly exit the
        protocol when malloc fails

        Return:
            (void*): `malloc` allocated memory space
    */

    void* ret = malloc(size);
    if (ret == NULL) {
        LOG_FATAL("Malloc of size %d failed!", size);
    } else {
        return ret;
    }
}

void* safe_realloc(void* buffer, size_t new_size) {
    /*
        Wrapper around realloc that will correctly exit the
        protocol when realloc fails

        Arguments:
            buffer (void*): Buffer to be reallocated
            new_size (size_t): New size of the reallocated buffer

        Return:
            (void*): `realloc` allocated memory space
    */

    void* ret = realloc(buffer, new_size);
    if (ret == NULL) {
        LOG_FATAL("Realloc of size %d failed!", new_size);
    } else {
        return ret;
    }
}
// ------------------------------------
// Implementation of a dynamically sized buffer
// ------------------------------------

/*
============================
Public Function Implementations
============================
*/

DynamicBuffer* init_dynamic_buffer(bool use_memory_regions) {
    /*
        Initializes a new dynamically sizing buffer.
        Note that accessing a dynamic buffer's buf outside
        of db->size is undefined behavior

        Arguments:
            use_memory_regions (bool): If true, will use OS-level memory regions [See
       allocate_region] If false, will use malloc for db->buf NOTE: The DynamicBuffer struct itself
       will always use malloc

        Returns:
            (DynamicBuffer*): The dynamic buffer (With initial size 0)
    */

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
    /*
        This will resize the given DynamicBuffer to the given size. This function may
        reallocate db->buf

        Arguments:
            db (DynamicBuffer*): The DynamicBuffer to resize
            new_size (size_t): The new size to resize `db` to
    */

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
    /*
        This will free the DynamicBuffer and its contents

        Arguments:
            db (DynamicBuffer*): The DynamicBuffer to free
    */

    if (db->use_memory_regions) {
        deallocate_region(db->buf);
    } else {
        free(db->buf);
    }
    free(db);
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

/*
============================
Public Function Implementations
============================
*/

BlockAllocator* create_block_allocator(size_t block_size) {
    /*
        Creates a block allocator that will create and free blocks of the
        given block_size. The block allocator will _not_ interfere with
        the malloc heap.

        Arguments:
            block_size (size_t): The size of blocks that this block allocator will allocate/free

        Returns:
            (BlockAllocator*): The created block allocator
    */

    // Create a block allocator
    InternalBlockAllocator* blk_allocator = safe_malloc(sizeof(InternalBlockAllocator));

    // Set block allocator values
    blk_allocator->num_allocated_blocks = 0;
    blk_allocator->block_size = block_size;
    blk_allocator->num_free_blocks = 0;

    return (BlockAllocator*)blk_allocator;
}

void* allocate_block(BlockAllocator* blk_allocator_in) {
    /*
        Allocates a block using the given block allocator

        Arguments:
            blk_allocator_in (BlockAllocator*): The block allocator to use for allocating the block

        Returns:
            (void*): The new block
    */

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
    /*
        Frees a block allocated by allocate_block

        Arguments:
            blk_allocator_in (BlockAllocator*): The block allocator that the block was allocated
                from block (void*): The block to free
    */

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

/*
============================
Includes
============================
*/

#ifdef _WIN32
#include <memoryapi.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

/*
============================
Custom Types
============================
*/

// Declare the RegionHeader struct
typedef struct {
    size_t size;
} RegionHeader;
// Macros to go between the data and the header of a region
#define TO_REGION_HEADER(a) ((void*)(((char*)a) - sizeof(RegionHeader)))
#define TO_REGION_DATA(a) ((void*)(((char*)a) + sizeof(RegionHeader)))

/*
============================
Private Function Implementations
============================
*/

// Get the page size
int get_page_size() {
    /*
        Get the system page size

        Returns:
            (int): system page size
    */

#ifdef _WIN32
    SYSTEM_INFO sys_info;

    // Copy the hardware information to the SYSTEM_INFO structure.
    GetSystemInfo(&sys_info);
    return sys_info.dwPageSize;
#else
    return getpagesize();
#endif
}

/*
============================
Public Function Implementations
============================
*/

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
void* allocate_region(size_t region_size) {
    /*
        Allocates a region of memory, of the requested size.
        This region will be allocated independently of
        the malloc heap or any block allocator.
        Will be zero-initialized.

        Arguments:
            region_size (size_t): The size of the region to create

        Returns:
            (void*): The new region of memory
    */

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
    /*
        Marks the region as unused (for now). This will let other processes
        use it if they desire. This will decrease the reported memory usage,
        by the size of the region that was used

        Arguments:
            region (void*): The region to mark as unused
    */

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
    /*
        Marks the region as used again. This will grab new memory regions from the OS,
        only give other processes have taken the memory while it was unused.

        Arguments:
            region (void*): The region to mark as used now
    */

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
    /*
        Resize the memory region. This will try to simply grow the region,
        but if it can't, it will allocate a new region and copy everything over.

        Arguments:
            region (void*): The region to resize
            new_region_size (size_t): The new size of the region

        Returns:
            (void*): The new pointer to the region (Will be
                different from the given region, if an allocation + copy was necessary)
    */

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

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
void deallocate_region(void* region) {
    /*
        Give the region back to the OS

        Arguments:
            region (void*): The region to deallocate
    */

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
