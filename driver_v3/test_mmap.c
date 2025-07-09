/* test_mmap.c - 测试mmap功能 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>

#define DEVICE_PATH "/dev/hetero_mmap"
#define SHARED_SIZE (32 * 1024)

void print_banner(const char *msg)
{
    printf("\n=== %s ===\n", msg);
}

int main()
{
    int fd;
    char *mapped_mem;
    char buffer[256];
    
    print_banner("6核异构系统 - mmap测试");
    
    /* 打开设备 */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        printf("提示: 确保驱动已加载 (sudo insmod hetero_mmap.ko)\n");
        return -1;
    }
    printf("✓ 成功打开设备\n");
    
    /* 映射共享内存 */
    mapped_mem = mmap(NULL,                    /* 让内核选择地址 */
                     SHARED_SIZE,              /* 映射大小 */
                     PROT_READ | PROT_WRITE,   /* 读写权限 */
                     MAP_SHARED,               /* 共享映射 */
                     fd,                       /* 文件描述符 */
                     0);                       /* 偏移量 */
    
    if (mapped_mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    printf("✓ 成功映射共享内存到地址: %p\n", mapped_mem);
    
    print_banner("测试1: 读取内核数据");
    printf("前64字节内容: %.64s\n", mapped_mem);
    
    print_banner("测试2: 写入用户数据");
    sprintf(buffer, "用户写入的数据 - PID: %d, 时间: %ld", 
            getpid(), time(NULL));
    strcpy(mapped_mem + 100, buffer);
    printf("✓ 写入数据到偏移100: %s\n", buffer);
    
    print_banner("测试3: 验证数据持久性");
    printf("重新读取偏移100: %s\n", mapped_mem + 100);
    
    print_banner("测试4: 性能测试");
    printf("写入1MB数据...\n");
    clock_t start = clock();
    
    for (int i = 0; i < 32; i++) {
        memset(mapped_mem + i * 1024, i & 0xFF, 1024);//write 32KB
    }
    
    clock_t end = clock();
    double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("✓ 完成! 耗时: %.4f秒\n", cpu_time);
    printf("✓ 吞吐量: %.2f MB/s\n", 1.0 / cpu_time);
    
    print_banner("测试5: 边界测试");
    /* 测试最后一个字节 */
    mapped_mem[SHARED_SIZE - 1] = 'E';
    printf("✓ 写入最后一个字节: %c\n", mapped_mem[SHARED_SIZE - 1]);
    
    /* 清理 */
    print_banner("清理资源");
    munmap(mapped_mem, SHARED_SIZE);
    close(fd);
    printf("✓ 测试完成!\n\n");
    
    return 0;
}
