#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

/* Jun-Yi Lau
 * 49002253
 * PINTOS PROJECT 4
 * CACHE.H
 * Interface functions that will allow the cache buffer to be the
 * only interface between files and disk. We will be using an opaque
 * struct cache_unit
 */

#include <stdio.h>
#include <stdlib.h>
#include "devices/block.h"
#include "filesys/filesys.h"

/* Global cache buffer */
typedef struct CacheBuffer_ CacheBuffer;

typedef struct CacheUnit_ CacheUnit;

CacheBuffer * filesys_cache;

/* Initalise our global cache buffer */
CacheBuffer * cache_buffer_init (void);

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
 * to save data from being randomly lost
 */
void cache_buffer_flush (void);

/* When the process terminates free all allocated resources of the cache buffer */
void cache_shutdown (void);


/* Setup global cache buffer */
CacheBuffer * cache_init (void);

#endif /* filesys/cache.h */
