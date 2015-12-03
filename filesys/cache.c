/* Jun-Yi Lau
 * 49002253
 * PINTOS PROEJCT 4
 * CACHE.C
 * Functions that will serve as the boundary between the file system and the disk
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"

#define BLOCK_SIZE 512
#define CACHE_SIZE 64

/* #############################################################
 * ############           DATA STRUCTURES       ################
 * #############################################################
 */

struct CacheUnit_ {
    block_sector_t disk_sector_id;
    bool dirty;
    bool accessed;
    int servicing;
    uint8_t block[BLOCK_SIZE];
};


struct CacheBuffer_ {
    int capacity;
    CacheUnit *buffer[CACHE_SIZE];
    Lock buffer_lock;
};

/* ##############################################################
 * #############        PUBLIC FUNCTIONS      ###################
 * ##############################################################
 */

/* Reserve a buffer cache block to hold this sector, evict if needed :and return it */
CacheUnit * cache_get_block (block_sector_t sector, bool exclusion);

/* Release access of a cache block and write if necessary */
void cache_put_block (CacheUnit * block);

/* Return a pointer to the data held in the CacheUnit */
void * cache_read_block (CacheUnit * block); 

/* Fill a given cache block with zeros and return pointer to the data */
void * cache_zero_block (CacheUnit * block);

/* Mark a cache as dirty */ 
void cache_mark_block_dirty (CacheUnit * block);

/* A function called periodically to write all dirty caches to disk without eviction
   to save data from being randomly lost */
void cache_buffer_flush (void); 

/* When the process terminates free all allocated resources of the cache buffer */
void cache_shutdown (void);

/* Make the cache buffer */
CacheBuffer * cache_init (void);

/* #############################################################
 * #############        Local Function      ####################
 * #############################################################
 */

int run_eviction_algorithm(void);


/* #############################################################
 * #############        Definitions         ####################
 * #############################################################
 */

CacheBuffer * cache_init (){
    filesys_cache =  malloc (sizeof (CacheBuffer));
    filesys_cache->capacity = 0;
    int i = 0;
    while (i < CACHE_SIZE) {
        filesys_cache->buffer[i] = NULL;
        i++;
    }
    lock_init(&filesys_cache->buffer_lock);
    // CAN START A THREAD HERE WHICH WILL DO THE CACHEFLUSHING
    return filesys_cache;
}

CacheUnit * cache_get_block (block_sector_t sector, bool exclusion) {
    int i = 0; // See if it exists in the cache already
    while (i < CACHE_SIZE){
        if (filesys_cache->buffer[i]->disk_sector_id == sector) {
            // TODO: Do we need to consider servicing number here?
            return filesys_cache->buffer[i];
        }
        i++;
    }
    // Else we make a new one and add it to the buffer
    CacheUnit * cu = malloc (sizeof (CacheUnit));
    cu->disk_sector_id = sector;
    cu->dirty = false;
    cu->accessed = true;
    cu->servicing = 1;

    i = 0;
    lock_acquire(&filesys_cache->buffer_lock);  // Hold the lock while we search. We don't want two threads finding the same free spot
    while (i < CACHE_SIZE) {
        if (filesys_cache->buffer[i] == NULL){
            filesys_cache->buffer[i] = cu;
            break;
        }
        i++;
    }
    if (i == CACHE_SIZE) {
        i = run_eviction_algorithm();           // Run the eviction algorithm. We still have the lock. 
        filesys_cache->buffer[i] = cu;                  // We get given the index of free spot, so we fill it.
    }
    lock_release(&filesys_cache->buffer_lock);  // We are done now
    return cu;
}

/* Second Chance Algorithm */
int run_eviction_algorithm(){
    bool loop = true;
    CacheUnit * cu = NULL;
    int i = 0;
    while (loop) { // Keep looping until we find an entry to evict
        i = 0;
        while ( i < CACHE_SIZE){    
            cu = filesys_cache->buffer[i];
            if (cu->servicing > 0) {continue;}       // If the unit is servicing some threads, leave it
            if (cu->accessed) {                      // Change access to false if true
                cu->accessed = false;
            } else {
                if (cu->dirty) {                 // Otherwise if it is dirty then write. We can now use this space. 
                    block_write (fs_device, cu->disk_sector_id, &cu->block);
                }
                loop = false;
                break;
            }
            i++;
        }
    }
    free (filesys_cache->buffer[i]);
    filesys_cache->buffer[i] = NULL;
    return i;
}

void cache_shutdown (){
    cache_buffer_flush();                         // We iterate through the array once writing everything that is dirty
    int i = 0;
    lock_acquire(&filesys_cache->buffer_lock);
    while (i < CACHE_SIZE){                     // We go through and free everything now indiscriminately
        free(filesys_cache->buffer[i]);
        filesys_cache->buffer[i] = NULL;
        i++;
    }
    lock_release(&filesys_cache->buffer_lock);
    return;
}


void cache_buffer_flush(){
    lock_acquire(&filesys_cache->buffer_lock);
    int i = 0;
    CacheUnit * cu = filesys_cache->buffer[i];
    while (i < CACHE_SIZE) {
        if (cu != NULL){
            if (cu->dirty == true){
                block_write(fs_device, cu->disk_sector_id, &cu->block);
                cu->dirty = false;
            }
        }
        i++;
    }
    lock_release(&filesys_cache->buffer_lock);
    return;
}

void *cache_zero_block (CacheUnit * cu){
    int i = 0;
    while (i < BLOCK_SIZE){
        cu->block[i] = 0;
        i++;
    }
    return &cu->block;
}

void cache_mark_block_dirty (CacheUnit * cu){
    cu->dirty = true;
    return;
}

void cache_put_block (CacheUnit * cu){
    cu->servicing--;
    return;
}

void * cache_read_block (CacheUnit * cu){
    return &cu->block;
}
