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


// 哈希表中桶号索引（设置质数个桶可以降低哈希冲突的可能性）
#define NBUFMAP_BUCKET 13
// 哈希索引(根据给定的设备号（dev）和块号（blockno）计算哈希索引(桶号))
#define BUFMAP_HASH(dev, blockno) ((((dev) << 27) | (blockno)) % NBUFMAP_BUCKET)


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  //struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  //struct buf head;

  // 为了避免死锁，规定查询其他桶之前必须先释放当前的桶锁 
  // -> 这导致可能出现多个cpu检查同一个blockno，造成一个块产生多个缓存的现象，因此引入驱逐锁
  struct spinlock eviction_lock;    // 驱逐锁 (避免一个块产生多个缓存)


  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];   //桶锁

} bcache;

void
binit(void)
{
  // struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }


  // 初始化桶锁
  for(int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    initlock(&bcache.bufmap_locks[i], "bache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  for(int i = 0; i < NBUF; i++)
  {
    // 初始化缓存区块
    struct buf* b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;  // 引用次数

    // 将所有缓存区块 添加到bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }

  initlock(&bcache.eviction_lock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //struct buf *b;

  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");


  struct buf *b;

  // 哈希获取对应桶号
  uint key = BUFMAP_HASH(dev, blockno);

  acquire(&bcache.bufmap_locks[key]);

  // blockno的缓存区块是否已经在缓冲区中
  for(b = bcache.bufmap[key].next; b; b = b->next)
  {
    // 如果已经在缓存区了
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;    // 引用+1
      release(&bcache.bufmap_locks[key]); // 释放桶锁
      acquiresleep(&b->lock);
      return b; // 返回缓存区块(buf)
    }
  }

  // 不在缓存区(对应的桶)中：
  // 为了防止死锁，先释放当前桶锁
  release(&bcache.bufmap_locks[key]);
  // 为了防止blockno的缓存区块被重复创建，加上驱逐锁
  acquire(&bcache.eviction_lock);

  // 在释放桶锁 和 加上驱逐锁 的间隙 可能别的进程创建了blockno的缓存区块(并释放了驱逐锁)，因此还要再检查一次
  for(b = bcache.bufmap[key].next; b; b = b->next)
  {
    // 如果已经在缓存区了
    if(b->dev == dev && b->blockno == blockno)
    {
      acquire(&bcache.bufmap_locks[key]); // 添加引用次数时，必须再加上桶锁
      b->refcnt++;    // 引用+1
      release(&bcache.bufmap_locks[key]); // 释放桶锁
      release(&bcache.eviction_lock);     // 释放驱逐锁
      acquiresleep(&b->lock);
      return b; // 返回缓存区块(buf)
    }
  }

  // 仍不在缓存区(对应的桶)中：
  // 此时只持有驱逐锁，不持有任何桶锁。查询所有桶中的LRU-buf

  struct buf* before_least = 0;   // LRU-buf的前一个块
  uint holding_bucket = -1;       // 记录当前持有哪个桶锁

  // 循环查询所有的桶
  for(int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    // 获取当前遍历的桶的桶锁（在找到下一个LRU-buf或驱逐内存之前都不释放）
    acquire(&bcache.bufmap_locks[i]);

    int newfound = 0;   // 是否在当前桶找到新的LRU-buf

    for(b = &bcache.bufmap[i]; b->next; b = b->next)
    {
      // 要找一个为在使用者(被引用次数为0)，且(所有桶中)最久未使用的缓存区块(buf)
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse))
      {
        before_least = b;
        newfound = 1;
      }
    }

    // 如果没找到新的LRU-buf，就释放当前桶锁
    if(!newfound)
      release(&bcache.bufmap_locks[i]);
    // 如果找到了新的LRU-buf
    else
    {
      // 如果当前找到的不是第一个LRU-buf(holding_bucket!=-1)，要释放之前持有的桶锁
      if(holding_bucket != -1)
        release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
    }
  }

  // 如果没有找到任何一个LRU-buf，表示没有空闲缓存块了
  if(!before_least)
    panic("bget: no buffers");

  b = before_least->next;   // b == LRU-buf

  // 如果想偷的块如果不在key桶，就要把块从它所在的桶驱逐出来
  if(holding_bucket != key)
  {
    // 将LRU-buf从原桶中驱逐
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);

    // 将LRU-buf添加到key桶
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  // 设置 buf 的字段
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;

  // 释放相关锁
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);
  return b;
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

  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if(b->refcnt == 0)
    b->lastuse = ticks;

  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  // acquire(&bcache.lock);
  // b->refcnt++;
  // release(&bcache.lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  // acquire(&bcache.lock);
  // b->refcnt--;
  // release(&bcache.lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


