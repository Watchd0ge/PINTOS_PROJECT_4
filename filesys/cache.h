#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "devices/block.h"
#include <list.h>

#define MAX_SIZE 64
#define BUFFER_SIZE 64
#define CACHE_SIZE 64

/* #########################################################
 * ############     CACHE BUFFER ATTRIBUTES     ############
 * #########################################################
 */


/* List of the cache blocks */
struct list cache_list;

/* Cache entry */
typedef struct cache_elem {
   uint8_t block[BLOCK_SECTOR_SIZE];
   block_sector_t sector;
   bool dirty;
   bool accessed;
   int servicing;
  struct list_elem c_elem;
} CacheUnit;

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

/* ##########################################################
 * #############        PUBLIC FUNCTIONS        #############
 * ##########################################################
 */

/* Initialise the cache buffer list and the locks */
void        cache_init (void);

/* Fetch a block from the cache. If it is not in there then it will fetch from disk
 * Cache acts as the abstraction layer between filesystem and disk
 */
CacheUnit * cache_get_block (block_sector_t sector, bool writing);

void        cache_flush (bool shutdown);
void        cache_read_ahead (block_sector_t sec);

#endif /* filesys/cache.h */
