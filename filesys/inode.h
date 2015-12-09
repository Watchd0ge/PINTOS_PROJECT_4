#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

typedef struct inode iNode;

typedef struct inode_disk iDisk;

struct bitmap;

void            inode_init (void);
bool            inode_create (block_sector_t, off_t, bool);
iNode *         inode_open (block_sector_t);
iNode *         inode_reopen (iNode *);
block_sector_t  inode_get_inumber (const iNode *);
bool            inode_is_dir (const iNode *);
void            inode_close (iNode *);
void            inode_remove (iNode *);
off_t           inode_read_at (iNode *, void *, off_t size, off_t offset);
off_t           inode_write_at (iNode *, const void *, off_t size, off_t offset);
void            inode_deny_write (iNode *);
void            inode_allow_write (iNode *);

/* INODE SETTERS & GETTERS */
off_t           inode_length (iNode *);
int             inode_get_open_cnt (const iNode *inode);
block_sector_t  inode_get_parent_inode (const iNode *inode);
bool            inode_add_parent_inode (block_sector_t parent_sector, block_sector_t child_sector);
void            inode_lock (const iNode *inode);
void            inode_unlock (const iNode *inode);

#endif /* filesys/inode.h */
