// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;
  char lockname[16];

  for(int i = 0; i < NBUCKETS; i++) {
    snprintf(lockname, sizeof(lockname), "bcache%d", i);
    initlock(&bcache.lock[i], lockname);
    // Initialize hash bucket as empty circular list
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
  }

  // Add all buffers to the first hash bucket initially
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    // Add to first hash bucket
    b->next = bcache.hashbucket[0].next;
    b->prev = &bcache.hashbucket[0];
    bcache.hashbucket[0].next->prev = b;
    bcache.hashbucket[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bucket = blockno % NBUCKETS;

  acquire(&bcache.lock[bucket]);

  // Is the block already cached in this bucket?
  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached in this bucket, search for unused buffer in current bucket
  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->refcnt == 0) {
      // Found unused buffer in current bucket
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // No unused buffer in current bucket, need to search other buckets
  // But first release current bucket lock to avoid deadlock
  release(&bcache.lock[bucket]);

  // Search other buckets for unused buffer
  for(int i = 1; i < NBUCKETS; i++) {
    uint try_bucket = (bucket + i) % NBUCKETS;
    acquire(&bcache.lock[try_bucket]);
    
    // Search for unused buffer in this bucket
    for(b = bcache.hashbucket[try_bucket].next; b != &bcache.hashbucket[try_bucket]; b = b->next){
      if(b->refcnt == 0) {
        // Found unused buffer, remove from current bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        
        // Release current bucket lock and acquire target bucket lock
        release(&bcache.lock[try_bucket]);
        acquire(&bcache.lock[bucket]);
        
        // Setup buffer and add to target bucket
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        b->next = bcache.hashbucket[bucket].next;
        b->prev = &bcache.hashbucket[bucket];
        bcache.hashbucket[bucket].next->prev = b;
        bcache.hashbucket[bucket].next = b;
        
        release(&bcache.lock[bucket]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[try_bucket]);
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucket = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // Move to head of MRU list in this bucket
    // Remove from current position
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // Add to head
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
  }
  
  release(&bcache.lock[bucket]);
}

void
bpin(struct buf *b) {
  uint bucket = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucket]);
  b->refcnt++;
  release(&bcache.lock[bucket]);
}

void
bunpin(struct buf *b) {
  uint bucket = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  release(&bcache.lock[bucket]);
}