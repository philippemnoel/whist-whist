/**
 * @copyright Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file whist_memory.h
 * @brief API for memory allocation and management.
 */
#ifndef WHIST_CORE_WHIST_MEMORY_H
#define WHIST_CORE_WHIST_MEMORY_H

#include <whist/core/whist.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @defgroup memory Memory
 *
 * Whist API for memory allocation and management.
 *
 * @{
 */

/**
 * @brief                          Wrapper around malloc that will correctly exit the
 *                                 protocol when malloc fails
 *
 * @param size                     Number of bytes of memory to allocate.
 *
 * @returns                        Allocated memory space
 */
void* safe_malloc(size_t size);

/**
 * @brief                          Wrapper around calloc that will correctly exit the
 *                                 protocol when calloc fails
 *
 * @param size                     Number of bytes of zeroed memory to allocate.
 *
 * @returns                        Allocated memory space
 */
void* safe_zalloc(size_t size);

/**
 * @brief                          Wrapper around realloc that will correctly exit the
 *                                 protocol when realloc fails
 *
 * @param buffer                   Buffer to be reallocated
 * @param new_size                 New size of the reallocated buffer
 *
 * @returns                        Reallocated memory space
 */
void* safe_realloc(void* buffer, size_t new_size);

/**
 * @brief   Dynamic buffer.
 * @details Memory buffer used to store any allocated data.
 */
typedef struct {
    size_t size;
    size_t capacity;
    bool use_memory_regions;
    char* buf;
} DynamicBuffer;

/**
 * @brief                          Initializes a new dynamically sizing buffer.
 *                                 Note that accessing a dynamic buffer's buf outside
 *                                 of db->size is undefined behavior
 *
 * @param use_memory_regions       If true, will use OS-level memory regions [See allocate_region]
 *                                 If false, will use malloc for db->buf
 *                                 The DynamicBuffer struct itself will always use malloc
 *
 * @returns                        The dynamic buffer (With initial size 0)
 */
DynamicBuffer* init_dynamic_buffer(bool use_memory_regions);

/**
 * @brief                          This will resize the given DynamicBuffer to the given size.
 *                                 This function may reallocate db->buf
 *
 * @param db                       The DynamicBuffer to resize
 *
 * @param new_size                 The new size to resize db to
 */
void resize_dynamic_buffer(DynamicBuffer* db, size_t new_size);

/**
 * @brief                          This will free the DynamicBuffer and its contents
 *
 * @param db                       The DynamicBuffer to free
 */
void free_dynamic_buffer(DynamicBuffer* db);

/**
 * @defgroup block_allocator Block Allocator
 *
 * Allocator to optimise allocation of many equal-sized blocks.
 *
 * @{
 */
typedef struct BlockAllocator BlockAllocator;

/**
 * @brief                          Creates a block allocator that will create and free blocks of the
 *                                 given block_size. The block allocator will _not_ interfere with
 *                                 the malloc heap.
 *
 * @param block_size               The size of blocks that this block allocator will allocate/free
 *
 * @returns                        The block allocator
 */
BlockAllocator* create_block_allocator(size_t block_size);

/**
 * @brief                          Allocates a block using the given block allocator
 *
 * @param block_allocator          The block allocator to use for allocating the block
 *
 * @returns                        The new block
 */
void* allocate_block(BlockAllocator* block_allocator);

/**
 * @brief                          Frees a block allocated by allocate_block
 *
 * @param block_allocator          The block allocator that the block was allocated from
 *
 * @param block                    The block to free
 */
void free_block(BlockAllocator* block_allocator, void* block);

/**
 * @brief                          Destroys a block allocator. All blocks allocated from this
 *                                 instance must have been freed before calling this!
 *
 * @param block_allocator          The block allocator to destroy
 */
void destroy_block_allocator(BlockAllocator* block_allocator);

/** @} */

/**
 * @defgroup region_allocator Region Allocator
 *
 * Region allocator wrapping mmap()/VirtualAlloc().

Note: All of these functions are safe, and will FATAL_ERROR on failure

This is a thin wrapper around mmap/VirtualAlloc, and will give us fine-grained
control over regions of memory and whether or not the OS will allocate actual pages of RAM for us.
(Pages are usually size 4096, but are in-general OS-defined)

This gives us fine-grained control over the "reported memory usage", which represents pages of
RAM that we demand must be allocated, and must not be written-to/read-from by any other processes.
This is what controls when the OS is "out of RAM", and what is reported by the OS's "Task Manager".

The OS by-default uses lazy allocation. So we won't actually use RAM until we read or write
to a page allocated by allocate_region. All fresh pages we read from will be 0-ed out first

Once we read/write to a page, that page becomes allocated in RAM. If you're done using it,
and you want to tell the OS that the data in the region isn't important to you anymore, call the
`mark_unused_region` function. That will decrease our reported memory usage by the size of the
region. However, the OS won't actually take those pages from us until it's low on RAM.

If we want to tell the OS that the region is important to us again, call
`mark_used_region`. Just like allocate_region, this won't actually increase reported memory usage,
until we read/write to those pages. However, our pages will usually be left exactly as they were
prior to `mark_unused_region`. They'll only be zero-ed out if the OS needed those pages in the
meantime. This is much more efficient than having it be zero-ed out every time, it'll
only be zero-ed out if it was actually necessary to zero it out.

If we want to completely give the region back to the OS, call `deallocate_region`,
and the RAM + address space will be freed.

 * @{
 */

/**
 * @brief                          Allocates a region of memory, of the requested size.
 *                                 This region will be allocated independently of
 *                                 the malloc heap or any block allocator.
 *                                 Will be zero-initialized.
 *
 *                                 This will only increase reported memory usage,
 *                                 once the pages are actually read or written to
 *
 * @param region_size              The size of the region to create
 *
 * @returns                        The new region of memory
 */
void* allocate_region(size_t region_size);

/**
 * @brief                          Marks the region as unused (for now).
 *                                 This will let other processes use it if they desire.
 *
 *                                 This will decrease the reported memory usage,
 *                                 by the size of the region that was used
 *
 * @param region                   The region to mark as unused
 */
void mark_unused_region(void* region);

/**
 * @brief                          Marks the region as used again.
 *                                 This will grab new memory regions from the OS,
 *                                 only give other processes have taken the
 *                                 memory while it was unused
 *
 *                                 This will only increase the reported memory usage,
 *                                 once the pages are used
 *
 * @param region                   The region to mark as used now
 */
void mark_used_region(void* region);

/**
 * @brief                          Resize the memory region.
 *
 *                                 This will try to simply grow the region,
 *                                 but if it can't, it will allocate a new region
 *                                 and copy everything over
 *
 * @param region                   The region to resize
 *
 * @param new_region_size          The new size of the region
 *
 * @returns                        The new pointer to the region
 *                                 (Will be different from the given region,
 *                                 if an allocation + copy was necessary)
 */
void* realloc_region(void* region, size_t new_region_size);

/**
 * @brief                          Give the region back to the OS
 *
 * @param region                   The region to deallocate
 */
void deallocate_region(void* region);

#if USING_MLOCK
/**
 * @brief                           Override system macOS malloc with mimalloc calls that have
 * mlock's inserted; this ensures that all our allocations are mlock'ed.
 */
void init_whist_mlock(void);
#endif

/** @} */
/** @} */

#endif /* WHIST_CORE_WHIST_MEMORY_H */
