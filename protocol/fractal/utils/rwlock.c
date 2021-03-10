/* A writer-preferred read-writer lock */

/*
 * num_writers includes any waiting writers and any active writer
 */
#include "rwlock.h"
#include <fractal/core/fractal.h>

void init_rw_lock(RWLock *rwlock) {
    rwlock->num_active_readers = 0;
    rwlock->num_writers = 0;
    rwlock->is_active_writer = false;

    rwlock->num_active_readers_lock = safe_SDL_CreateMutex();
    rwlock->num_writers_lock = safe_SDL_CreateMutex();
    rwlock->is_active_writer_lock = safe_SDL_CreateMutex();

    rwlock->no_active_readers_cond = SDL_CreateCond();
    rwlock->no_writers_cond = SDL_CreateCond();
    rwlock->no_active_writer_cond = SDL_CreateCond();

    if (!rwlock->no_active_readers_cond || !rwlock->no_writers_cond ||
        !rwlock->no_active_writer_cond) {
        LOG_FATAL("SDL_CreateCond failed! %s", SDL_GetError());
    }
}

void destroy_rw_lock(RWLock *rwlock) {
    SDL_DestroyMutex(rwlock->num_active_readers_lock);
    SDL_DestroyMutex(rwlock->num_writers_lock);
    SDL_DestroyMutex(rwlock->is_active_writer_lock);

    SDL_DestroyCond(rwlock->no_active_readers_cond);
    SDL_DestroyCond(rwlock->no_writers_cond);
    SDL_DestroyCond(rwlock->no_active_writer_cond);
}

void write_lock(RWLock *rwlock) {
    // Increment number of writers
    safe_SDL_LockMutex(rwlock->num_writers_lock);
    rwlock->num_writers++;
    safe_SDL_UnlockMutex(rwlock->num_writers_lock);

    // Wait for all of the active readers to go home
    safe_SDL_LockMutex(rwlock->num_active_readers_lock);
    while (rwlock->num_active_readers > 0) {
        safe_SDL_CondWait(rwlock->no_active_readers_cond, rwlock->num_active_readers_lock);
    }
    safe_SDL_UnlockMutex(rwlock->num_active_readers_lock);

    // Wait for the active writer to go home
    safe_SDL_LockMutex(rwlock->is_active_writer_lock);
    while (rwlock->is_active_writer) {
        safe_SDL_CondWait(rwlock->no_active_writer_cond, rwlock->is_active_writer_lock);
    }

    // Take the only active writer spot
    rwlock->is_active_writer = true;
    safe_SDL_UnlockMutex(rwlock->is_active_writer_lock);
}

void read_lock(RWLock *rwlock) {
    // Wait for all of the writers to go home
    safe_SDL_LockMutex(rwlock->num_writers_lock);
    while (rwlock->num_writers > 0) {
        safe_SDL_CondWait(rwlock->no_writers_cond, rwlock->num_writers_lock);
    }
    // Increment the number of active readers
    safe_SDL_LockMutex(rwlock->num_active_readers_lock);
    rwlock->num_active_readers++;
    safe_SDL_UnlockMutex(rwlock->num_active_readers_lock);

    safe_SDL_UnlockMutex(rwlock->num_writers_lock);
}

void write_unlock(RWLock *rwlock) {
    // Set is active writer to false
    safe_SDL_LockMutex(rwlock->is_active_writer_lock);
    rwlock->is_active_writer = false;
    safe_SDL_UnlockMutex(rwlock->is_active_writer_lock);
    // Wake up anyone waiting for a change in is active writer
    SDL_CondBroadcast(rwlock->no_active_writer_cond);

    // Decrement the number of writers
    safe_SDL_LockMutex(rwlock->num_writers_lock);
    rwlock->num_writers--;
    if (rwlock->num_writers == 0) {
        SDL_CondBroadcast(rwlock->no_writers_cond);
    }
    safe_SDL_UnlockMutex(rwlock->num_writers_lock);
}

void read_unlock(RWLock *rwlock) {
    // Decrement the number of active readers
    safe_SDL_LockMutex(rwlock->num_active_readers_lock);
    rwlock->num_active_readers--;
    // If there are no more active readers, wake up anyone waiting for there
    // to be no active readers
    if (rwlock->num_active_readers == 0) {
        SDL_CondBroadcast(rwlock->no_active_readers_cond);
    }
    safe_SDL_UnlockMutex(rwlock->num_active_readers_lock);
}
