/**
 * @copyright Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file threads.h
 * @brief API for cross-platform thread handling.
 */
#ifndef WHIST_UTILS_THREADS_H
#define WHIST_UTILS_THREADS_H

#include <whist/core/platform.h>

// So that SDL sees symbols such as memcpy
#if OS_IS(OS_WIN32)
#include <windows.h>
#else
#include <string.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <stdbool.h>

/**
 * @defgroup threading Threading
 *
 * Whist API for cross-platform thread management.
 *
 * This is the Whist API for cross-platform thread creation, destruction
 * and management, along with synchronisation between threads.
 *
 * @{
 */

/**
 * Mutex object type.
 */
typedef struct WhistMutexStruct* WhistMutex;
/**
 * Condition variable object type.
 *
 * This must have an associated mutex.
 */
typedef struct WhistConditionStruct* WhistCondition;
/**
 * Semaphore object type.
 */
typedef struct WhistSemaphoreStruct* WhistSemaphore;
/**
 * Thread handle type.
 *
 * Used with functions which manipulate threads.
 */
typedef struct WhistThreadStruct* WhistThread;
/**
 * System-specific thread ID type.
 *
 * The meaning of this depends on the system.
 */
typedef unsigned long WhistThreadID;

/**
 * Thread function type.
 *
 * Thread starting functions must match this signature.
 */
typedef int (*WhistThreadFunction)(void*);

/**
 * Enumeration of thread priority values.
 */
typedef enum WhistThreadPriority {
    /**
     * Low priority.
     *
     * Threads with this priority will generally run after other
     * threads, but should still get some time to run.
     */
    WHIST_THREAD_PRIORITY_LOW = SDL_THREAD_PRIORITY_LOW,
    /**
     * Normal priority.
     *
     * This is the default for newly-created threads.
     */
    WHIST_THREAD_PRIORITY_NORMAL = SDL_THREAD_PRIORITY_NORMAL,
    /**
     * High priority.
     *
     * Threads with this priority will generally run before other
     * threads, but all will still get some chance to run.
     */
    WHIST_THREAD_PRIORITY_HIGH = SDL_THREAD_PRIORITY_HIGH,
    /**
     * Real-time priority.
     *
     * Threads with this priority will always run first if they can.
     * Do not use this for threads which will not spend a large
     * proportion of time sleeping, as it can prevent other threads (and
     * processes) from running indefinitely.
     */
    WHIST_THREAD_PRIORITY_REALTIME = SDL_THREAD_PRIORITY_TIME_CRITICAL
} WhistThreadPriority;

/**
 * Thread-local storage key type.
 *
 * This is used to store and retrieve data accessible only to
 * the current thread.
 */
typedef unsigned int WhistThreadLocalStorageKey;

/**
 * Thread-local storage destructor function type.
 *
 * Thread-local storage cleanup functions must match this signature.
 */
typedef void (*WhistThreadLocalStorageDestructor)(void*);

/**
 * Initialize threading.
 *
 * This must be called before any threads are created.
 */
void whist_init_multithreading(void);

/**
 * Create a new thread.
 *
 * @param thread_function  Function to call in the new thread.
 * @param thread_name      String name for the thread.
 * @param data             Argument to pass to thread_function.
 * @return  Thread handle for the new thread, or NULL on failure.
 */
WhistThread whist_create_thread(WhistThreadFunction thread_function, const char* thread_name,
                                void* data);

/**
 * Get the system-specific thread ID of a thread.
 *
 * For example, if the underlying threading system is pthreads then this
 * will return the pthread_t handle for the thread.
 *
 * @param thread  Thread handle to get ID for, or NULL to get the ID for
 *                the calling thread.
 * @return  System-specific thread ID.
 */
WhistThreadID whist_get_thread_id(WhistThread thread);

/**
 * Detach a thread so that it can finish asynchronously.
 *
 * The thread will not become a zombie and cannot be waited for.  There
 * is also no longer any way to tell when it has definitely finished.
 *
 * @param thread  Thread handle to detach.
 */
void whist_detach_thread(WhistThread thread);

/**
 * Wait for a thread to finish.
 *
 * @param thread  Thread handle of the thread to wait for.
 * @param ret     Return value of thread_function for the finished
 *                thread.  If NULL the return value is discarded.
 */
void whist_wait_thread(WhistThread thread, int* ret);

/**
 * Set the priority for the calling thread.
 *
 * @param priority  The priority value to set.
 */
void whist_set_thread_priority(WhistThreadPriority priority);

/**
 * Create a new thread-local storage entry.
 *
 * @return  The thread-local storage key for the new entry.
 */
WhistThreadLocalStorageKey whist_create_thread_local_storage(void);

/**
 * Store data in a thread-local storage entry.
 *
 * @param key           The thread-local storage key in which to store data.
 * @param data          The data to store.
 * @param destructor    The cleanup function to call when destroying the
 *                      thread-local storage data.
 */
void whist_set_thread_local_storage(WhistThreadLocalStorageKey key, void* data,
                                    WhistThreadLocalStorageDestructor destructor);

/**
 * Retrieve data from a thread-local storage entry.
 *
 * @param key  The thread-local storage key from which to retrieve data.
 *
 * @return  The data stored in the thread-local storage entry.
 */
void* whist_get_thread_local_storage(WhistThreadLocalStorageKey key);

/**
 * Sleep for at least a given number of milliseconds.
 *
 * This will not return early if interrupted.
 *
 * @param ms  Minimum number of milliseconds to sleep for.
 */
void whist_sleep(uint32_t ms);

/**
 * Try to sleep for at least a given number of microseconds.
 *
 * This can return early if interrupted.
 *
 * Note that small values are unlikely to be efficient due to context
 * switch and timer overhead.  It is recommended to use whist_sleep()
 * instead if possible.
 *
 * @param us  Minimum number of microseconds to sleep for.
 */
void whist_usleep(uint32_t us);

/**
 * Create a mutex.
 *
 * @return  New mutex object.
 */
WhistMutex whist_create_mutex(void);

/**
 * Lock a mutex.
 *
 * This will wait for the mutex to become available if it is currently
 * locked by another thread.
 *
 * @param mutex  Mutex to lock.
 */
void whist_lock_mutex(WhistMutex mutex);

/**
 * Attempt to lock a mutex.
 *
 * This will return immediately if the mutex is currently locked by
 * another thread.
 *
 * @param mutex  Mutex to attempt to lock.
 * @return  Zero if the mutex was locked, nonzero otherwise.
 */
int whist_try_lock_mutex(WhistMutex mutex);

/**
 * Unlock a mutex.
 *
 * This will abort if misused, for example by unlocking a mutex which
 * the calling thread did not lock.
 *
 * @param mutex  Mutex to unlock.
 */
void whist_unlock_mutex(WhistMutex mutex);

/**
 * Destroy a mutex.
 *
 * @param mutex  Mutex to destroy.
 */
void whist_destroy_mutex(WhistMutex mutex);

/**
 * Create a condition variable.
 *
 * @return  New condition variable object.
 */
WhistCondition whist_create_cond(void);

/**
 * Wait for a condition to change.
 *
 * This atomically unlocks the mutex and starts waiting for a change in
 * the condition to the signalled.  When it receives such a signal, it
 * re-locks the mutex and returns.
 *
 * It can also return at any time for any reason - it must be called in
 * a loop which checks whether the condition has actually changed.
 *
 * @param cond   Condition variable to wait for.
 * @param mutex  Mutex protecting the condition, which must be held by
 *               the calling thread on entry.
 */
void whist_wait_cond(WhistCondition cond, WhistMutex mutex);

/**
 * Same as 'whist_wait_cond', but with a timeout
 *
 * @param cond   Condition variable to wait for.
 * @param mutex  Mutex protecting the condition, which must be held by
 *               the calling thread on entry.
 * @param timeout_ms The number of ms to wait for.
 *
 * @returns True if the conditional variable was woken up by a signal,
 *          False if the timeout was simply exceeded
 */
bool whist_timedwait_cond(WhistCondition cond, WhistMutex mutex, uint32_t timeout_ms);

/**
 * @brief Signal to all waiters that a condition may have changed.
 * The cond's mutex must be locked when either changing the predicate of any waiting conds, or
 * during this broadcast. Not doing so can cause a broadcast to fail to wakeup its cond.
 *
 * @param cond  Condition variable to signal waiters for.
 */
void whist_broadcast_cond(WhistCondition cond);

void whist_signal_cond(WhistCondition cond);
/**
 * Destroy a condition variable.
 *
 * @param cond  Condition variable to destroy.
 */
void whist_destroy_cond(WhistCondition cond);

/**
 * Create a semaphore.
 *
 * @param initial_value  Initial value for the semaphore.
 * @return  New semaphore object.
 */
WhistSemaphore whist_create_semaphore(uint32_t initial_value);

/**
 * Post a semaphore.
 *
 * Increment the semaphore, and also wake anyone waiting for it if the
 * value was previously zero.
 *
 * @param semaphore  Semaphore to post.
 */
void whist_post_semaphore(WhistSemaphore semaphore);

/**
 * Wait for a semaphore.
 *
 * If the semaphore value is greater than zero then decrement it and
 * return immediately.  Otherwise, wait until another thread posts the
 * semaphore to increase its value and then decrement it and return.
 *
 * @param semaphore  Semaphore to wait for.
 */
void whist_wait_semaphore(WhistSemaphore semaphore);

/**
 * Wait for a semaphore.
 *
 * If the semaphore value is greater than zero then decrement it and
 * return immediately.  Otherwise, wait until another thread posts the
 * semaphore to increase its value and then decrement it and return.
 *
 * @param semaphore  Semaphore to wait for.
 * @param timeout_ms The number of ms to wait for.
 *
 * @returns True if the semaphore was woken up by a signal,
 *          False if the timeout was simply exceeded
 */
bool whist_wait_timeout_semaphore(WhistSemaphore semaphore, int timeout_ms);

/**
 * Value of a semaphore.
 *
 * @param semaphore  Semaphore to find value of.
 *
 * @return  Value of the semaphore.
 */
uint32_t whist_semaphore_value(WhistSemaphore semaphore);

/**
 * Destroy a semaphore.
 *
 * @param semaphore  Semaphore to destroy.
 */
void whist_destroy_semaphore(WhistSemaphore semaphore);

/** @} */

#endif /* WHIST_UTILS_THREADS_H */
