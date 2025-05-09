// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
//} kmem;
} kmem[NCPU];   // 每个CPU分配独立的freelist，多个CPU并发分配物理内存不会相互竞争

char* kmem_lock_names[] = 
{
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit()
{
  //initlock(&kmem.lock, "kmem");

  for(int i = 0; i < NCPU; i++)
  {
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);

  // 关闭中断
  push_off();

  // 获取cpu编号，中断关闭时调用cpuid才是安全的，所以上面用push_off关闭中断
  int cpu = cpuid();

  // 将释放的页插入当前CPU的freelist中
  acquire(&kmem[cpu].lock);   
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  // 重新打开中断
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // acquire(&kmem.lock);
  // r = kmem.freelist;    // 取出一个物理页，页表项本身就是物理页
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  // 关闭中断

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);

  // 如果当前cpu已经没有freelist的时候，去其他cpu偷内存页
  if (!kmem[cpu].freelist)
  {
    // 这里指定偷64个内存页
    int steal_left = 64;
    for (int i = 0; i < NCPU; i++)
    {
      // 跳过当前cpu
      if (i == cpu)
        continue;
      
      acquire(&kmem[i].lock);
      // 如果在想要偷页的cpu也没有freelist了，就释放锁跳过
      if(!kmem[i].freelist)
      {
        release(&kmem[i].lock);
        continue;
      }

      // 偷kmem[i]的freelist，直到偷够指定页数(64)了 或者 把kmem[i]的freelist搬空了
      struct run* rr = kmem[i].freelist;
      while (rr && steal_left)
      {
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu].freelist;
        kmem[cpu].freelist = rr;
        rr = kmem[i].freelist;
        steal_left--;
      }
      
      release(&kmem[i].lock);

      // 如果是因为偷够指定页数了就退出循环，否则就继续偷下一个cpu的freelist
      if (steal_left == 0)
        break;
    }
  }

  // 取出一个物理页，用于分配
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
