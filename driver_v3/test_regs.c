/* test_regs.c - 测试硬件寄存器 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdint.h>

#define DEVICE_PATH "/dev/hetero_regs"

/* 寄存器偏移量 */
#define IPI_STATUS    0x00
#define IPI_TRIGGER   0x04
#define IPI_CLEAR     0x08
#define IPI_ENABLE    0x0C

#define MBOX_M2C0_CMD  0x10
#define MBOX_M2C0_DATA 0x14
#define MBOX_C02M_STAT 0x18
#define MBOX_C02M_RESP 0x1C

#define HW_MUTEX_REQ   0x30
#define HW_MUTEX_STAT  0x34
#define HW_MUTEX_REL   0x38

/* ioctl命令 */
#define HETERO_IOC_MAGIC 'h'
#define HETERO_IOC_GET_INFO   _IOR(HETERO_IOC_MAGIC, 1, struct hetero_info)
#define HETERO_IOC_SEND_IPI   _IOW(HETERO_IOC_MAGIC, 3, int)
#define HETERO_IOC_RESET      _IO(HETERO_IOC_MAGIC, 4)

struct hetero_info {
    int num_cores;
    int reg_size;
    int shared_size;
    unsigned long reg_base;
    unsigned long shared_base;
};

/* 读写寄存器的辅助宏 */
#define REG_READ32(base, offset) (*(volatile uint32_t *)((char *)(base) + (offset)))
#define REG_WRITE32(base, offset, val) (*(volatile uint32_t *)((char *)(base) + (offset)) = (val))

void print_banner(const char *msg)
{
    printf("\n=== %s ===\n", msg);
}

void dump_registers(void *reg_base)
{
    printf("\n寄存器状态:\n");
    printf("  IPI_STATUS:  0x%08x\n", REG_READ32(reg_base, IPI_STATUS));
    printf("  IPI_ENABLE:  0x%08x\n", REG_READ32(reg_base, IPI_ENABLE));
    printf("  MBOX_CMD:    0x%08x\n", REG_READ32(reg_base, MBOX_M2C0_CMD));
    printf("  MBOX_DATA:   0x%08x\n", REG_READ32(reg_base, MBOX_M2C0_DATA));
    printf("  MBOX_RESP:   0x%08x\n", REG_READ32(reg_base, MBOX_C02M_RESP));
    printf("  MUTEX_STAT:  0x%08x\n", REG_READ32(reg_base, HW_MUTEX_STAT));
}

int main()
{
    int fd;
    void *mapped_mem;
    void *reg_base;
    void *shared_mem;
    struct hetero_info info;
    int ret;
    
    print_banner("6核异构系统 - 硬件寄存器测试");
    
    /* 打开设备 */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
    /* 获取系统信息 */
    ret = ioctl(fd, HETERO_IOC_GET_INFO, &info);
    if (ret < 0) {
        perror("ioctl GET_INFO");
        close(fd);
        return -1;
    }
    
    printf("\n系统信息:\n");
    printf("  核心数: %d\n", info.num_cores);
    printf("  寄存器空间: %d bytes\n", info.reg_size);
    printf("  共享内存: %d KB\n", info.shared_size / 1024);
    
    /* 映射内存 */
    int total_size = info.reg_size + info.shared_size;
    mapped_mem = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (mapped_mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    
    reg_base = mapped_mem;
    shared_mem = mapped_mem + info.reg_size;
    
    printf("\n内存映射:\n");
    printf("  基地址: %p\n", mapped_mem);
    printf("  寄存器: %p - %p\n", reg_base, reg_base + info.reg_size);
    printf("  共享内存: %p - %p\n", shared_mem, shared_mem + info.shared_size);
    
    /* 显示初始状态 */
    print_banner("测试1: 读取初始寄存器状态");
    dump_registers(reg_base);
    
    /* 测试IPI */
    print_banner("测试2: 发送IPI到IO核(Core 0)");
    int core_id = 0;
    ret = ioctl(fd, HETERO_IOC_SEND_IPI, &core_id);
    if (ret < 0) {
        perror("ioctl SEND_IPI");
    } else {
        printf("✓ IPI发送成功\n");
        usleep(10000);  /* 等待10ms */
        printf("IPI_STATUS: 0x%08x\n", REG_READ32(reg_base, IPI_STATUS));
    }
    
    /* 测试邮箱通信 */
    print_banner("测试3: 邮箱通信测试");
    printf("发送PING命令到IO核...\n");
    
    /* 发送命令 */
    REG_WRITE32(reg_base, MBOX_M2C0_DATA, 0x12345678);
    REG_WRITE32(reg_base, MBOX_M2C0_CMD, 0x0001);  /* PING命令 */
    
    /* 触发IPI通知小核 */
    ioctl(fd, HETERO_IOC_SEND_IPI, &core_id);
    
    /* 等待响应 */
    printf("等待响应...\n");
    int timeout = 100;  /* 100ms超时 */
    while (timeout-- > 0) {
        if (REG_READ32(reg_base, MBOX_C02M_STAT) != 0) {
            uint32_t resp = REG_READ32(reg_base, MBOX_C02M_RESP);
            printf("✓ 收到响应: 0x%04x\n", resp);
            if (resp == 0x8001) {
                printf("✓ PONG响应正确!\n");
            }
            /* 清除状态 */
            REG_WRITE32(reg_base, MBOX_C02M_STAT, 0);
            break;
        }
        usleep(1000);  /* 1ms */
    }
    
    if (timeout <= 0) {
        printf("✗ 响应超时!\n");
    }
    
    /* 测试互斥锁 */
    print_banner("测试4: 硬件互斥锁");
    uint32_t mutex_stat = REG_READ32(reg_base, HW_MUTEX_STAT);
    printf("互斥锁状态: 0x%04x (可用锁: %d个)\n", 
           mutex_stat, __builtin_popcount(mutex_stat));
    
    /* 请求锁0 */
    printf("请求锁0...\n");
    REG_WRITE32(reg_base, HW_MUTEX_REQ, 0x01);
    mutex_stat = REG_READ32(reg_base, HW_MUTEX_STAT);
    printf("新状态: 0x%04x\n", mutex_stat);
    
    /* 释放锁0 */
    printf("释放锁0...\n");
    REG_WRITE32(reg_base, HW_MUTEX_REL, 0x01);
    mutex_stat = REG_READ32(reg_base, HW_MUTEX_STAT);
    printf("新状态: 0x%04x\n", mutex_stat);
    
    /* 测试共享内存 */
    print_banner("测试5: 共享内存访问");
    printf("共享内存内容: %.50s\n", (char *)shared_mem);
    
    /* 性能测试 */
    print_banner("测试6: 寄存器访问性能");
    int ops = 100000;
    clock_t start = clock();
    
    for (int i = 0; i < ops; i++) {
        REG_WRITE32(reg_base, MBOX_M2C0_DATA, i);
        volatile uint32_t val = REG_READ32(reg_base, MBOX_M2C0_DATA);
        (void)val;  /* 防止优化 */
    }
    
    clock_t end = clock();
    double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("完成%d次读写操作\n", ops);
    printf("耗时: %.4f秒\n", cpu_time);
    printf("速率: %.0f ops/秒\n", ops / cpu_time);
    
    /* 清理 */
    print_banner("测试完成");
    dump_registers(reg_base);
    
    munmap(mapped_mem, total_size);
    close(fd);
    
    return 0;
}
