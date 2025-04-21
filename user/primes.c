// user/primes.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 本代码晒质数类似于 埃拉托斯特尼筛法，即从2开始，每一轮确定剩余元素中最小元素为质数，并筛掉最小元素倍数的数。

// 把一个进程的父进程称为左邻居，子进程称为右邻居

// 难点：
// 1.什么时候给右邻居传数据？
// 2.及时关闭管道用不到的写端和读端（vx6的文件描述符是有限的）

// 筛选质数的函数，接收一个管道作为参数
void sieve(int pleft[2])
{
    // 从左邻居读取第一个整数(即最小数，只要有就一定是质数)
    int p;
    read(pleft[0], &p, sizeof(p));
    //如果读到提前设置好的结束标志-1，表示结束，退出进程
    if(p == -1)
    {
        exit(0);
    }
    printf("prime %d\n", p);    //只要有第一个数，就一定是质数

    // 创建一个新管道，把p的倍数全筛掉，余下的发送到右邻居(子进程)
    int pright[2];
    pipe(pright);

    // 当前进程
    if(fork() != 0)
    {
        close(pright[0]);   //当前进程用不到这个管道的读端，关闭
        //从左邻居接收数字
        int buf;
        while(read(pleft[0], &buf, sizeof(buf)) && buf != -1)
        {
            // 只把非p倍数的数往下传
            if(buf % p != 0)
            {
                write(pright[1], &buf, sizeof(buf));
            }
        }

        //此时接收到左邻居传来的 结束标志-1，也要把-1传给右邻居，然后等右邻居退出后，当前进程退出
        buf = -1;
        write(pright[1], &buf, sizeof(buf));
        wait(0);
        exit(0);
    }

    //子进程
    else
    {
        close(pright[1]);   //子进程用不到这个管道的写端，关掉
        close(pleft[0]);    //子进程用不到这个管道的读端，关掉（在创建子进程之前，这个管道的写端已被当前进程关掉，自然不会复制给子进程）
        sieve(pright);      //递归调用筛质数函数
    }
}

int main(int argc, char **argv)
{
    // 创建初始管道
    int input_pipe[2];
    pipe(input_pipe);

    // 父进程
    if(fork() != 0)
    {
        close(input_pipe[0]);   // 父进程用不到读端，关掉

        // 向管道写入2~35的整数
        int i;
        for(i = 2; i <= 35; i++)
        {
            write(input_pipe[1], &i, sizeof(i));
        }
        // 写入结束标志
        i = -1;
        write(input_pipe[1], &i, sizeof(i));
    }
    //子进程
    else
    {
        close(input_pipe[1]);
        sieve(input_pipe);
        exit(0);
    }

    wait(0);
    exit(0);
}