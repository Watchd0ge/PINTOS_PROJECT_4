#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <list.h>


/* ####################################################
 * #############        PROTOTYPES      ###############
 * ####################################################
 */

CacheUnit *cache_push(block_sector_t sector, bool);
CacheUnit * cache_evict (void);
void specialised_cache_get_block (block_sector_t sector);
void cache_read_ahead_daemon (void *sector);

/* ####################################################
 * ############       FUNCTIONS         ###############
 * ####################################################
 */

/* Initialize cache */
void cache_init (void)
{
    /* BUFFERS */
    list_init (&cache_list);
    list_init (&read_list);

    /* BUFFER LOCKS */
    lock_init (&cache_lock);
    lock_init (&read_lock);

    /* CONDITION VARIABLES */
    cond_init (&read_not_empty);
    thread_create("read_ahead_daemon", 0, cache_read_ahead_daemon, NULL);
}


/* Get sector from cache, write it in cache if not already present */
CacheUnit *cache_get_block (block_sector_t sector, bool to_write)
{
    lock_acquire(&cache_lock);

    CacheUnit *cu;
    struct list_elem *unit;
    /* Check if the sector is already in the buffer */
    for (unit = list_begin (&cache_list); unit != list_end (&cache_list); unit = list_next (unit)){
        cu = list_entry(unit, CacheUnit, c_elem);
        if (cu->sector == sector){
            cu->dirty = !to_write;
            cu->accessed = true;
            cu->open_cnt++;
            lock_release (&cache_lock);
            return cu;
        }
    }
    /* Otherwise we are going to pull it from disk and add it to the buffer */
    cu = cache_push (sector, to_write);
    lock_release (&cache_lock);
    cache_read_ahead(sector + 1);
    return cu;
}

/* Pull sector from disk, allocate it to a buffer unit and add it to the buffer */
CacheUnit* cache_push (block_sector_t sector, bool to_write)
{
    CacheUnit *cu;
    /* Decision to make CacheUnit from scratch for evict */
    if (cache_size < CACHE_SIZE)
    {
        /* We have space in our cache still so we make our own*/
        cache_size++;
        cu = malloc (sizeof (CacheUnit));
        if (cu == NULL){ PANIC("NO MORE MEMORY FOR CACHE UNITS"); }
        else { list_push_back(&cache_list, &cu->c_elem);}

    } else {
        /* We dont' have space in our cache so we evict. Return a CacheUnit we can use.*/
        cu = cache_evict ();
    }

    /* Update our cacheunit */
    cu->sector = sector;
    cu->dirty = to_write;
    cu->accessed = true;
    cu->open_cnt = 1;
    block_read (fs_device, sector, &cu->block);
    return cu;
}

/* 2nd Chance Algorithm Eviction */
CacheUnit *cache_evict (void)
{
    CacheUnit *cu;
    struct list_elem *i;

    bool loop = true;
    while (loop) {
        for (i = list_begin (&cache_list); i != list_end (&cache_list); i = list_next (i)) {
            cu = list_entry(i, CacheUnit, c_elem);
            if (cu->accessed) {
                cu->accessed = false;
            } else {
                if (cu->dirty) {
                    block_write(fs_device, cu->sector, &cu->block);
                    cu->dirty = false;
                }
                loop = false;
                break;
            }
        }
    }
    return cu;
}

/* Flush the cache. If shutdown then free everything as well */
void cache_flush (bool shutdown)
{
    lock_acquire(&cache_lock);
    struct list_elem *i = NULL;
    for (i = list_begin (&cache_list) ;
         i != list_end (&cache_list);
         i = list_next (i))
    {
        CacheUnit *cu = list_entry (i, CacheUnit, c_elem);
        if (cu->dirty){
            block_write (fs_device, cu->sector, &cu->block);
            cu->dirty = false;
        }
    }

    if (shutdown == true) {
        struct list_elem * next = NULL;
        struct list_elem * i = list_begin (&cache_list);
        CacheUnit * cu = NULL;
        while (i != list_end(&cache_list)) {
            cu = list_entry(i, CacheUnit, c_elem);
            next = list_remove(i);
            free (cu);
            i = next;
        }
    }
    lock_release (&cache_lock);
}

/* Read ahead sector */
typedef struct read_elem
{
    block_sector_t sector;
    struct list_elem elem;
} ReadAheadUnit;

/* Cache Read Ahead scheduler */
void cache_read_ahead (block_sector_t sector)
{
    ReadAheadUnit *read = (ReadAheadUnit *) malloc (sizeof (ReadAheadUnit));

    if (read == NULL) { PANIC("NO MORE MEMORY FOR CACHE READ AHEAD"); }

    read->sector = sector;

    lock_acquire (&read_lock);

    list_push_back (&read_list, &read->elem);
    lock_release (&read_lock);
    // cond_signal (&read_not_empty, &read_lock);
}

/* Read Ahead Daemon : Constantly On */
void cache_read_ahead_daemon (void *sec UNUSED)
{
    while (true){
        while (list_empty (&read_list)){
            // lock_acquire (&read_lock);
            // cond_wait (&read_not_empty, &read_lock);
            // lock_release (&read_lock); // Need to hook this up to get cache
        }
        // PANIC("WE GOT IN");
        lock_acquire (&read_lock);
        ReadAheadUnit *read = list_entry (list_pop_front (&read_list), ReadAheadUnit, elem);
        lock_release (&read_lock); // Need to hook this up to get cache
        specialised_cache_get_block(read->sector);
        free(read);
    }
}

/* Get sector from cache, write it in cache if not already present */
void specialised_cache_get_block (block_sector_t sector)
{
    lock_acquire(&cache_lock);
    CacheUnit *cu;
    struct list_elem *unit;
    /* Check if the sector is already in the buffer */
    for (unit = list_begin (&cache_list); unit != list_end (&cache_list); unit = list_next (unit)){
        cu = list_entry(unit, CacheUnit, c_elem);
        if (cu->sector == sector){
            cu->accessed = false;
            lock_release (&cache_lock);
        }
    }
    /* Otherwise we are going to pull it from disk and add it to the buffer */
    cu = cache_push (sector, false);
    lock_release (&cache_lock);
}
