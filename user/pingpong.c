// user/pingpong.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    // 创建两个数组，用于创建管道
    int pp2c[2];    // parent-to-child 用于父进程到子进程的通信
    int pc2p[2];    // child-to-parent 用于子进程到父进程的通信

    // pipe 是一种系统调用，用于创建一个匿名管道
    // 调用 pipe(数组) 后，数组中会存放两个 文件描述符，下标为0的描述符用于读，下标为1的描述符用于写
    pipe(pp2c);
    pipe(pc2p);

    // fork 是一个系统调用，用于创建一个新的进程（子进程），这个新进程几乎是父进程的一个精确拷贝
    // 由于在复制时复制了父进程的堆栈段，所以两个进程都停留在fork函数中，等待返回
    //  因此fork函数会返回两次，一次是在父进程中返回，另一次是在子进程中返回，这两次的返回值是不一样的
    //  其中：父进程中 fork 返回的是子进程的进程标识（PID），而在子进程中，fork 的返回值为 0
    // 所以通过判断 fork 的返回值，可以区分当前进程是父进程还是子进程，从而实现双方不同的逻辑流程
    
    // 父进程
    if(fork()!=0)
    {
        // 父进程向子进程发送一个字符
        write(pp2c[1], ".", 1);     // 此处用 "." 表示任意数据
        // 管道的读取行为是基于“写端是否关闭”来判断是否达到了文件结束标志
        // 如果管道的写端没有close，那么管道中数据为空时对管道的读取将会阻塞，因此对于不需要的管道描述符，要尽可能早的关闭
        close(pp2c[1]);

        // 父进程从子进程读取一个字符
        char buf;
        read(pc2p[0], &buf, 1);
        printf("%d: received pong\n", getpid());

        // 等待子进程结束
        wait(0);
    }

    // 子进程
    else
    {
        // 子进程从父进程读取一个字符
        char buf;
        read(pp2c[0], &buf, 1);
        printf("%d: received ping\n", getpid());

        // 子进程向父进程发送一个字符
        write(pc2p[1], ".", 1);
        close(pc2p[1]);     // 即使关闭管道写端
    }

    // 关闭管道读端
    close(pp2c[0]);
    close(pc2p[0]);

    exit(0);
}