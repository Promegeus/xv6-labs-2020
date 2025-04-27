// user/find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 从指定目录开始，递归查找并打印出所有匹配指定目标文件名的路径
// path当前待查找的目录或文件的路径
// target：目标文件名，其格式为 "/文件名"，用于匹配路径字符串末尾
// 递归查找函数，查找路径为 path 的目录下是否有目标文件 target
void find(char* path, char* target) 
{
	char buf[512], *p;  // char buf[512] 用于构造新路径
	int fd;             // 文件描述符
	struct dirent de;   // 保存目录项   eg:/home/user/file.txt 每个/分隔的部分对应一个目录项
	struct stat st;     // 保存文件或目录状态信息

    // 打开指定路径
    if((fd = open(path,0)) < 0)
    {
        // 如果不能打开，则输出错误信息并返回
        // (标准输出: 文件描述符为1,    标准错误输出: 文件描述符为2)
        fprintf(2, "find: cannot open %s\n", path);
		return;
    }

    // 获取文件/目录的状态信息
    if(fstat(fd, &st) < 0)
    {
        // 如果获取不到，则输出错误信息并返回
        fprintf(2, "find: cannot stat %s\n", path);
		close(fd);
		return;
    }

    // 按类型(文件/目录)分情况处理
    switch (st.type)
    {    
    // 如果是文件，检查文件名是否与目标文件名匹配
    case T_FILE:
        //  path 中末尾 strlen(target) 个字符，与 target 进行比较。如果相等则认为匹配成功
        if (strcmp(path+strlen(path)-strlen(target), target) == 0)
        {
            // 如果匹配成功，打印出完整路径
            printf("%s\n", path);
        }
        break;
    // 如果是目录
    case T_DIR:
        // 检查路径长度是否超出缓冲区大小 
        // (path + "/" + 文件名 + 字符串结束符 不能超过 buf 的大小)
        if (strlen(path)+1+DIRSIZ+1 > sizeof buf)
        {
            printf("find: path too long\n");
			break;
        }

        // path 复制到缓冲区 buf，然后在 buf 的末尾加上 "/"，p设为"/"下一个位置
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';

        // 读取目录项
        // sizeof(de): 每次读取字节数，等于整个结构体 struct dirent 的大小
        // read() 返回实际读取的字节数。如果返回值等于 sizeof(de) 表示读取到了一个完整的目录项
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            // 若目录项无效或者该位置未被使用，跳过本次循环，不对该目录项做进一步处理
            if (de.inum == 0)
                continue;
            // 将目录项中的名字拷贝到完整路径缓冲区的后半部分，拼接成新的完整路径
            memmove(p, de.name, DIRSIZ);    
            p[DIRSIZ] = 0;  //设置拷贝后的字符串的结束标志（null termination），确保后续像 strlen() 或 strcmp() 这类操作能正确获取字符串长度与内容
            //获取目录项状态信息
            if (stat(buf, &st) < 0)     //stat() 调用成功返回 0，失败则返回 -1
            {
                //如果无法获取状态信息，就跳过该目录项
                printf("find: cannot stat %s\n", buf);
				continue;
            }
            // 排除当前目录 “.” 和父目录 “..”，防止在递归调用时陷入无限循环
            if (strcmp(buf+strlen(buf)-2, "/.")!=0 && strcmp(buf+strlen(buf)-3, "/..")!=0)
            {
                find(buf, target);  //递归查找子目录
            }
        }
        break;
    }
    close(fd);  // 关闭文件/目录
}

int main(int argc, char *argv[])
{
    // 若参数不足，直接退出
    if(argc < 3)
        exit(0);    
    char target[512];
    target[0] = '/';    // 在要查找的文件名前添加 "/"
    strcpy(target+1, argv[2]);
    find(argv[1], target);
    exit(0);
}
