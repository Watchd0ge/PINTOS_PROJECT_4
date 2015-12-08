#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/cache.h"


#define INDIRECT_BLOCKS 128			/* Block of pointers */
#define INODE_BLOCKS 13				/* Top level pointers*/
#define DIRECT 4					/* Direct pointers */
#define INDIRECT 8					/* Indirect pointers */
#define DOUBLE_INDIRECT 1			/* Double indirect pointers */
#define DIRECT_NUMBER 0				/* Direct pointers position */
#define INDIRECT_NUMBER 4			/* Indirect pointers position */
#define DOUBLE_INDIRECT_NUMBER 12	/* Double indirect pointers position */
#define MAX_FILE 8388608			/* Max file size */
#define INODE_MAGIC 0x494e4f44		/* Identifies an inode. */

/* Indirect block of pointers */
struct indirect_block
  {
    block_sector_t iblocks[INDIRECT_BLOCKS];
  };

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               	/* First data sector. */
    off_t length;                       	/* File size in bytes. */
    unsigned magic;                     	/* Magic number. */
    bool is_dir;							/* Inode is a directory. */
    block_sector_t parent_inode;			/* Inode's parent inode. */
    uint32_t unused[107];               	/* Not used. */
    block_sector_t iblocks[INODE_BLOCKS];	/* Top level blocks. */
    uint32_t dir;							/* Pointer to last direct block. */
    uint32_t indir;							/* Pointer to last indirect block. */
    uint32_t dindir;						/* Pointer to last double indirect block. */
  };

bool inode_load (struct inode_disk *disk_inode);
off_t inode_extend (struct inode *inode, off_t len);
size_t inode_extend_dir (struct inode *inode, size_t diff);
size_t inode_extend_indir (struct inode *inode, size_t diff);
size_t inode_extend_dindir (struct inode *inode, size_t diff);
size_t inode_extend_dindir_deep (struct inode *inode, size_t diff, struct indirect_block* high_bl);
void inode_delete (struct inode *inode);

/* Returns the number of direct sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sec (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the number of indirect sectors to allocate for an inode SIZE
   bytes long. */
static size_t
bytes_to_indir (off_t size)
{
  if (size <= BLOCK_SECTOR_SIZE*DIRECT){
    return 0;
  }
  size -= BLOCK_SECTOR_SIZE * DIRECT;
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE * INDIRECT);
}

/* Returns the number of doube indirect sectors to allocate for an inode SIZE
   bytes long. */
static size_t
bytes_to_dindir (off_t size)
{
  if (size <= BLOCK_SECTOR_SIZE*(DIRECT + INDIRECT * INDIRECT_BLOCKS)){
    return 0;
  }
  return DOUBLE_INDIRECT;
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              	/* Element in inode list. */
    block_sector_t sector;              	/* Sector number of disk location. */
    int open_cnt;                       	/* Number of openers. */
    bool removed;                       	/* True if deleted, false otherwise. */
    int deny_write_cnt;                 	/* 0: writes ok, >0: deny writes. */
    bool is_dir;							/* Inode is a directory. */
    block_sector_t parent_inode;			/* Inode's parent inode. */
    off_t length;							/* Inode's length. */
    off_t read;								/* Inode's readable portion length. */
    block_sector_t iblocks[INODE_BLOCKS];	/* Top level blocks. */
    struct lock ilock;						/* Inode's lock. */
    size_t dir;								/* Pointer to last direct block. */
    size_t indir;							/* Pointer to last indirect block. */
    size_t dindir;							/* Pointer to last double indirect block. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t len, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < len){
    uint32_t i;
    uint32_t ind[INDIRECT_BLOCKS];
    if (pos < BLOCK_SECTOR_SIZE * DIRECT){
	  return inode->iblocks[pos / BLOCK_SECTOR_SIZE];
	}
    else if (pos < BLOCK_SECTOR_SIZE * (DIRECT + INDIRECT * INDIRECT_BLOCKS)){
	  pos -= BLOCK_SECTOR_SIZE * DIRECT;
	  i = pos / (BLOCK_SECTOR_SIZE * INDIRECT_BLOCKS) + DIRECT;
	  block_read (fs_device, inode->iblocks[i], &ind);
	  pos %= BLOCK_SECTOR_SIZE * INDIRECT_BLOCKS;
	  return ind[pos / BLOCK_SECTOR_SIZE];
	}
    else{
	  block_read(fs_device, inode->iblocks[DOUBLE_INDIRECT_NUMBER], &ind);
	  pos -= BLOCK_SECTOR_SIZE * (DIRECT + INDIRECT * INDIRECT_BLOCKS);
	  i = pos / (BLOCK_SECTOR_SIZE * INDIRECT_BLOCKS);
	  block_read (fs_device, ind[i], &ind);
	  pos %= BLOCK_SECTOR_SIZE * INDIRECT_BLOCKS;
	  return ind[pos / BLOCK_SECTOR_SIZE];
	}
  }
  else{
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Sets the flag IS_DIR if inode is directory.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
    disk_inode->length = length;
    if (disk_inode->length > MAX_FILE){
	  disk_inode->length = MAX_FILE;
	}
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_dir = is_dir;
    disk_inode->parent_inode = ROOT_DIR_SECTOR;
    if (inode_load(disk_inode)){
      block_write (fs_device, sector, disk_inode);
      success = true;
    }
    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->ilock);

  /* Read data from disk */
  struct inode_disk data;
  block_read(fs_device, inode->sector, &data);
  inode->length = data.length;
  inode->read = data.length;
  inode->is_dir = data.is_dir;
  inode->parent_inode = data.parent_inode;
  inode->dir = data.dir;
  inode->indir = data.indir;
  inode->dindir = data.dindir;
  memcpy(&inode->iblocks, &data.iblocks, INODE_BLOCKS * sizeof(block_sector_t));

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Add inode to directory associated with input sector. */
bool
inode_move (block_sector_t node, block_sector_t new_parent)
{
  struct inode* inode = inode_open (node);
  if (!inode)
    return false;
  inode->parent_inode = new_parent;
  inode_close (inode);
  return true;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
		  inode_delete (inode);
        }
	  /* Copy to disk otherwise */
      else {
        struct inode_disk disk_inode;
        disk_inode.length = inode->length;
	    disk_inode.magic = INODE_MAGIC;
	    disk_inode.is_dir = inode->is_dir;
	    disk_inode.parent_inode = inode->parent_inode;
	    disk_inode.dir = inode->dir;
	    disk_inode.indir = inode->indir;
	    disk_inode.dindir = inode->dindir;
	    memcpy(&disk_inode.iblocks, &inode->iblocks, INODE_BLOCKS*sizeof(block_sector_t));
	    block_write(fs_device, inode->sector, &disk_inode);
      }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  if (offset >= inode->read){
    return bytes_read;
  }

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, inode->read, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->read - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Load to cache and read sector. */
      CacheUnit *c = cache_get_block (sector_idx, false);
      memcpy (buffer + bytes_read, (uint8_t *)&c->block + sector_ofs, chunk_size);
      c->accessed = true;

      /* Read Ahead. */
      cache_read_ahead (sector_idx+1);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }


  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* Extend file if write exceeds file's length. */
  if (offset + size > inode->length){
    if (!inode->is_dir){
	  inode_acquire (inode);
	}
    inode->length = inode_extend(inode, offset + size);
    if (!inode->is_dir){
	  inode_release (inode);
	}
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, inode->length, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Load to cache and write sector. */
      CacheUnit *c = cache_get_block (sector_idx, true);
      memcpy ((uint8_t *)&c->block + sector_ofs, buffer + bytes_written, chunk_size);
      c->accessed = true;
      c->dirty = true;

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  inode->read = inode->length;
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Return the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}

/* Acquire inode's lock. */
void
inode_acquire (struct inode *inode)
{
  lock_acquire (&((struct inode *)inode)->ilock);
}

/* Release inode's lock. */
void
inode_release (struct inode *inode)
{
  lock_release (&((struct inode *)inode)->ilock);
}

/* Free memory allocation of an inode. */
void
inode_delete (struct inode *inode)
{
  size_t sec_to_del = bytes_to_sec (inode->length);
  size_t indir_to_del = bytes_to_indir (inode->length);
  size_t dindir_to_del = bytes_to_dindir (inode->length);
  unsigned int i = 0;
  while (sec_to_del && i < INDIRECT_NUMBER) { // direct blocks
    free_map_release (inode->iblocks[i], 1);
    sec_to_del--;
    i++;
  }
  while (sec_to_del && indir_to_del && i < DOUBLE_INDIRECT_NUMBER) { // indirect blocks
    struct indirect_block ind_bl;
    unsigned int j = 0;
    block_read(fs_device, inode->iblocks[i], &ind_bl);
    while (sec_to_del && j < INDIRECT_BLOCKS){
      free_map_release(ind_bl.iblocks[j], 1);
      sec_to_del--;
      j++;
    }
    free_map_release(inode->iblocks[i], 1);
    indir_to_del--;
    i++;
  }
  if (sec_to_del && dindir_to_del) { // double indirect blocks
    unsigned int z = 0;
    struct indirect_block high_bl;
    block_read(fs_device, inode->iblocks[i], &high_bl);
    while (z < indir_to_del){
      struct indirect_block low_bl;
      unsigned int w = 0;
      block_read(fs_device, high_bl.iblocks[z], &low_bl);
      while (sec_to_del && w < INDIRECT_BLOCKS){
        free_map_release(low_bl.iblocks[w], 1);
	sec_to_del--;
	w++;
      }
      free_map_release(high_bl.iblocks[z], 1);
      z++;
    }
  free_map_release(inode->iblocks[i], 1);
  }
}

/* Returns the parent inode of a given inode. */
struct inode *
inode_get_parent (struct inode *inode){
  block_sector_t *parent_inode = (block_sector_t*) inode->parent_inode;
  return (struct inode*) inode_open ((block_sector_t) parent_inode);
}

/* Returns true if inode is a directory. */
bool
inode_is_dir (const struct inode *inode){
  return inode->is_dir;
}

/* Returns the count of open instances of a given inode. */
int
inode_open_cnt (const struct inode *inode){
  return inode->open_cnt;
}

/* Extend inode beyond its current size. */
off_t
inode_extend (struct inode *inode, off_t len)
{
  size_t diff = bytes_to_sec (len) - bytes_to_sec (inode->length);
  if (diff == 0){
    return len;
  }
  if (bytes_to_dindir(len) != 0) { // inode's size up to double indirect blocks
    diff = inode_extend_dindir (inode, diff);
    return len - diff * BLOCK_SECTOR_SIZE;
  }
  else if (bytes_to_indir(len) != 0) { // inode's size up to indirect blocks
    diff = inode_extend_indir (inode, diff);
    return len;
  }
  else { // inode's size up to direct blocks
    diff = inode_extend_dir (inode, diff);
    return len;
  }
}

/* File extension up to direct blocks. */
size_t
inode_extend_dir (struct inode *inode, size_t diff)
{
  static char dummy[BLOCK_SECTOR_SIZE];
  while (inode->dir < INDIRECT_NUMBER){
    free_map_allocate (1, &inode->iblocks[inode->dir]);
    block_write (fs_device, inode->iblocks[inode->dir], dummy);
    inode->dir++;
    diff--;
    if (diff == 0){
      break;
    }
  }
  return diff;
}

/* File extension up to indirect blocks. */
size_t
inode_extend_indir (struct inode *inode, size_t diff)
{
  diff = inode_extend_dir (inode, diff);
  while (inode->dir < DOUBLE_INDIRECT_NUMBER){
    static char dummy[BLOCK_SECTOR_SIZE];
    struct indirect_block ind_bl;
    if (inode->indir == 0){
      free_map_allocate(1, &inode->iblocks[inode->dir]);
    }
    else {
      block_read(fs_device, inode->iblocks[inode->dir], &ind_bl);
    }
    while (inode->indir < INDIRECT_BLOCKS){
      free_map_allocate(1, &ind_bl.iblocks[inode->indir]);
      block_write(fs_device, ind_bl.iblocks[inode->indir], dummy);
      inode->indir++;
      diff--;
      if (diff == 0){
        break;
      }
    }
    block_write(fs_device, inode->iblocks[inode->dir], &ind_bl);
    if (inode->indir == INDIRECT_BLOCKS){
      inode->indir = 0;
      inode->dir++;
    }
  }
  return diff;
}

/* File extension up to double indirect blocks. */
size_t
inode_extend_dindir (struct inode *inode, size_t diff)
{
  diff = inode_extend_indir (inode, diff);
  struct indirect_block ind_bl;
  if (inode->dindir == 0 && inode->indir == 0){
    free_map_allocate(1, &inode->iblocks[inode->dir]);
  }
  else {
    block_read(fs_device, inode->iblocks[inode->dir], &ind_bl);
  }
  while (inode->indir < INDIRECT_BLOCKS){
    diff = inode_extend_dindir_deep (inode, diff, &ind_bl);
    if (diff == 0){
      break;
    }
  }
  block_write(fs_device, inode->iblocks[inode->dir], &ind_bl);
  return diff;
}

/* File extension up to double indirect blocks, second block layer. */
size_t
inode_extend_dindir_deep (struct inode *inode, size_t diff, struct indirect_block* high_bl)
{
  static char dummy[BLOCK_SECTOR_SIZE];
  struct indirect_block low_bl;
  if (inode->dindir == 0){
    free_map_allocate(1, &high_bl->iblocks[inode->indir]);
  }
  else{
    block_read(fs_device, high_bl->iblocks[inode->indir], &low_bl);
  }
  while (inode->dindir < INDIRECT_BLOCKS){
    free_map_allocate(1, &low_bl.iblocks[inode->dindir]);
    block_write(fs_device, low_bl.iblocks[inode->dindir], dummy);
    inode->dindir++;
    diff--;
    if (diff == 0){
      break;
    }
  }
  block_write(fs_device, high_bl->iblocks[inode->indir], &low_bl);
  if (inode->dindir == INDIRECT_BLOCKS){
    inode->dindir = 0;
    inode->indir++;
  }
  return diff;
}

/* Load new disk_inode to memory. */
bool
inode_load (struct inode_disk *disk_inode)
{
  struct inode inode;
  inode.length = 0;
  inode.dir = 0;
  inode.indir = 0;
  inode.dindir = 0;
  inode_extend(&inode, disk_inode->length);
  disk_inode->dir = inode.dir;
  disk_inode->dir = inode.indir;
  disk_inode->dir = inode.dindir;
  memcpy(&disk_inode->iblocks, &inode.iblocks, INODE_BLOCKS * sizeof (block_sector_t));
  return true;
}
