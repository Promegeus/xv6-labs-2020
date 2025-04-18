// user/sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    // 如果没有传入参数，提示用户需要提供睡眠时间
    if(argc < 2)
    {
        printf("usage: sleep <ticcks>\n");
    }
    // 如果传入参数，就转为int型，并调用sleep函数
    sleep(atoi(argv[1]));
    exit(0);
}