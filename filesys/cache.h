#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "devices/block.h"
#include <list.h>

#define MAX_SIZE 64
#define BUFFER_SIZE 64

/* List of the cache blocks */
struct list cache_list;

typedef struct CacheUnit_ {
    block_sector_t sector;
    bool dirty;
    bool accessed;
    int servicing;
    uint8_t block[BLOCK_SECTOR_SIZE];
} CacheUnit;

typedef struct CacheBuffer_ {
    CacheUnit * buffer[BUFFER_SIZE];
    Lock cache_lock;
} CacheBuffer;

CacheBuffer filesys_cache;

/* Cache size */
int cache_size;

/* Cache lock */
struct lock cache_lock;

/* Read ahead list */
struct list read_list;

/* Read ahead lock */
struct lock read_lock;

/* Read ahead condition */
struct condition read_not_empty;

/* Cache entry */
typedef struct cache_elem {
  uint8_t block[BLOCK_SECTOR_SIZE];
  block_sector_t sector;
  bool dirty;
  bool accessed;
  int servicing;
  struct list_elem c_elem;
};

void cache_init (void);
struct cache_elem* cache_get_elem (block_sector_t sector, bool writing);
struct cache_elem* cache_push (block_sector_t sector, bool writing);
struct cache_elem* cache_evict (void);
void cache_backup (bool shutdown);
void cache_read_ahead (void *sec);
void cache_ahead (block_sector_t sec);

#endif /* filesys/cache.h */
