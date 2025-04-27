// user/xargs.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 从标准输入读取参数，并将这些参数传递给指定程序执行

// 运行指定的程序，接收参数
void run(char* program, char** args)
{
    // 创建子进程，在子进程中执行指定的程序
    if(fork() == 0)
    {
        exec(program, args);
        // exec() 成功的话不会返回；如果 exec() 返回，说明出错，于是子进程调用 exit(0) 退出
        exit(0);
    }
    // 父进程在 fork() 后不做等待，直接返回
    return;
}

int main(int argc, char *argv[])
{
    char buf[2048];         // 用于存储从标准输入读取的数据
    char* p = buf;          // 指向当前写入到 buf 位置的指针
    char* last_p = buf;     // 记录当前正在构造的参数的起始位置，也指向 buf 内部的位置
    char* argsbuf[128];     // 定义一个字符指针数组，用来存放完整参数列表中各个参数(字符串)的地址
    char** args = argsbuf;  // args 作为一个别名，初始指向 argsbuf 数组的起始位置
    //  当遇到分隔符（空格或换行）时，就用 last_p 到当前 p 之间形成一个 C风格字符串，并把这个字符串的地址保存到参数数组中

    // 从 argv[1] 开始，把所有传递给 xargs 的参数依次复制到 argsbuf 中
    for (int i = 1; i < argc; i++)
    {
        *args = argv[i];
        args++;
    }
    // 在此之后，args值就不变了，它标记了从哪里开始存放标准输入读取的参数

    // 记录当前参数的位置
    char** pa = args;   // pa指向argv[]中第一个空闲位置，即指向 argsbuf 数组中下一个可用的位置

    // 从标准输入读取数据，存储在缓冲区 buf 中
    // read(0, p, 1) 从标准输入（文件描述符 0）读取 1 个字节，存到 buf 中由指针 p 指向的位置
    // read返回实际读取的字节数，返回 0，说明已经读到 EOF（输入结束），退出循环
    while (read(0, p, 1) != 0)  
    {
        // 使用指针p遍历缓冲区，遇到空格或换行，说明一个参数已经结束，将其替换为 "\0"
        if (*p == ' ' || *p == '\n')
        {
            char delim_temp = *p; // 保存原始的分隔符
            *p = '\0';

            // 将参数添加到缓冲区 argsbuf 中
            *(pa++) = last_p;
            last_p = p+1;   //  更新 last_p 为下一个字符的位置，为下一个参数做准备

            // 每当遇到换行符 \n 时，表示一组参数读取完毕，调用 run 函数执行程序，传递参数
            if(delim_temp == '\n')
            {
                *pa = 0;    // 给参数数组最后加上一个空指针（*pa = 0）
                run(argv[1], argsbuf);
                pa = args;  // 在run完之后args后面的内容就没用了，pa回到args
            }
        }
        p++;    //继续读取数据
    }

    // 用同样的逻辑处理最后一行数据
    // 如果读入结束后，pa 没有等于 args，说明最后一行数据没有遇到换行符就结束了
    // 这时也完成参数追加、字符串终止，并调用 run 运行程序
    if (pa != args)     // args（在进入 while 前已确定位置）
    {
        *p = '\0';
        *(pa++) = last_p;
        *pa = 0;
        run(argv[1], argsbuf);
    }

    // 等待所有子进程结束
    // 父进程调用以等待任一子进程退出。
    // 参数为子进程退出状态的存储地址（传入 0 表示不关注退出状态）。 
    // 返回值：成功时返回子进程的 pid；如果无子进程则返回 -1。
    while (wait(0) != -1){};
    exit(0);
}