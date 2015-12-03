#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include <list.h>

/* Read ahead sector */
struct read_elem
{
  block_sector_t sec;
  struct list_elem elem;
};

/* Initialize cache */
void cache_init (void)
{
  cache_size = 0;
  list_init (&cache_list);
  list_init (&read_list);
  lock_init (&cache_lock);
  lock_init (&read_lock);
  cond_init (&read_not_empty);
}

/* Get sector from cache, write it in cache if not already present */
struct cache_elem* cache_get_elem (block_sector_t sector, bool writing)
{
  struct cache_elem *c;
  struct list_elem *i;
  lock_acquire(&cache_lock);
  for (i = list_begin (&cache_list); i != list_end (&cache_list); i = list_next (i)){
    c = list_entry(i, struct cache_elem, c_elem);
    if (c->sector == sector){
      c->dirty |= writing;
      c->accessed = true;
      lock_release (&cache_lock);
      return c;
    }
  }
  c = cache_push (sector, writing);
  lock_release (&cache_lock);
  return c;
}

/* Write element into cache */
struct cache_elem* cache_push (block_sector_t sector, bool writing)
{
  struct cache_elem *c;
  if (cache_size < MAX_SIZE){
    cache_size++;
    c = malloc (sizeof (struct cache_elem));
    if (!c){
      return NULL;
    }
    list_push_back(&cache_list, &c->c_elem);
  }
  else{
    c = cache_evict ();
  }
  c->sector = sector;
  block_read (fs_device, c->sector, &c->block);
  c->dirty = writing;
  c->accessed = true;
  return c;
}

/* Eviction algorithm for cache */
struct cache_elem* cache_evict (void)
{
  struct cache_elem *c;
  struct list_elem *i;
  bool evicted = false;
  for (i = list_begin (&cache_list); i != list_end (&cache_list); i = list_next (i)){
    c = list_entry(i, struct cache_elem, c_elem);
    if (c->accessed){
      c->accessed = false;
    }
    else {
      if (c->dirty){
        block_write(fs_device, c->sector, &c->block);
      }
      evicted = true; 
      break;
    }
  }
  if (!evicted) {
    c = cache_evict ();
  }
  return c;
}

/* Copy content of the cache to disk */
void cache_backup (bool shutdown)
{
  lock_acquire(&cache_lock);
  struct list_elem *next;
  struct list_elem *i = list_begin (&cache_list);
  while (i != list_end (&cache_list)){
    next = list_next (i);
    struct cache_elem *c = list_entry (i, struct cache_elem, c_elem);
    if (c->dirty){
      block_write (fs_device, c->sector, &c->block);
      c->dirty = false;
    }
    if (shutdown){
      list_remove(&c->c_elem);
      free(c);
    }
    i = next;
  }
  lock_release (&cache_lock);
}

/* Cache read-ahead process */
void cache_read_ahead (void *sec UNUSED)
{
  while (true){
    lock_acquire (&read_lock);
    while (list_empty (&read_list)){
      cond_wait (&read_not_empty, &read_lock);
    }
    struct read_elem *read = list_entry (list_pop_front (&read_list), struct read_elem, elem);
    lock_release (&read_lock);
    struct cache_elem *c = cache_get_elem (read->sec, false);
    free (read);
  }
}

/* Schedule sector for read ahead */
void cache_ahead (block_sector_t sec)
{
  struct read_elem *read = (struct read_elem *) malloc (sizeof (struct read_elem));
  if (read == NULL)
    return;
  read->sec = sec;
  lock_acquire (&read_lock);
  list_push_back (&read_list, &read->elem);
  cond_signal (&read_not_empty, &read_lock);
  lock_release (&read_lock);
}
