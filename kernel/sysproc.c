#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;

  // 原sbrk(n)系统调用将进程的内存大小增加n个字节，然后返回新分配区域的开始部分（即旧的大小）。
  // 为实现惰性分配，新的sbrk(n)应该只将进程的大小（myproc()->sz）增加n，然后返回旧的大小。它不应该分配内存

  // if(growproc(n) < 0)    // 第一步：删除sbrk(n)系统调用中的页面分配代码
  //   return -1;

  struct proc* p = myproc();

  if(n > 0)
    p->sz += n;   // 惰性分配，仅改变sz大小(进程大小)，不分配内存，
                  //  当需要用到这些物理内存时会触发页面错误，到时候再分配物理内存

  //如果是减少内存，还是要马上执行，当然要检查减去内存后是否大于0
  else if(p->sz + n > 0)
    p->sz = uvmdealloc(p->pagetable, p->sz, p->sz+n);
  else
    return -1;


  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
