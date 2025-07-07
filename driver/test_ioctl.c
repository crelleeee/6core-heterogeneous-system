/*
 * test_ioctl.c - 测试6核异构系统的ioctl接口
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

/* 必须与驱动中的定义一致 */
#define HETERO_IOC_MAGIC 'h'

#define HETERO_IOC_PING_CORE    _IOW(HETERO_IOC_MAGIC, 1, int)
#define HETERO_IOC_GET_STATUS   _IOR(HETERO_IOC_MAGIC, 2, int)
#define HETERO_IOC_SEND_MSG     _IOW(HETERO_IOC_MAGIC, 3, struct hetero_msg)
#define HETERO_IOC_RESET        _IO(HETERO_IOC_MAGIC, 4)

struct hetero_msg {
    int core_id;
    int cmd;
    int data;
};

void print_menu(void)
{
    printf("\n=== 6-Core Heterogeneous System Control ===\n");
    printf("1. Ping IO Core (core 0)\n");
    printf("2. Ping RT Core (core 1)\n");
    printf("3. Get system status\n");
    printf("4. Send custom message\n");
    printf("5. Reset system\n");
    printf("6. Read device info\n");
    printf("0. Exit\n");
    printf("Select: ");
}

int main(void)
{
    int fd;
    int choice;
    int ret;
    int core_id;
    int status;
    struct hetero_msg msg;
    char buffer[1024];
    
    /* 打开设备 */
    fd = open("/dev/hetero_soc", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure:\n");
        printf("1. Driver is loaded: lsmod | grep hetero_soc\n");
        printf("2. Device exists: ls -l /dev/hetero_soc\n");
        printf("3. You have permission: sudo chmod 666 /dev/hetero_soc\n");
        return -1;
    }
    
    printf("Device opened successfully!\n");
    
    while (1) {
        print_menu();
        scanf("%d", &choice);
        
        switch (choice) {
        case 1:
            /* Ping IO Core */
            core_id = 0;
            ret = ioctl(fd, HETERO_IOC_PING_CORE, &core_id);
            if (ret < 0) {
                perror("PING_CORE ioctl failed");
            } else {
                printf("Successfully pinged IO core!\n");
            }
            break;
            
        case 2:
            /* Ping RT Core */
            core_id = 1;
            ret = ioctl(fd, HETERO_IOC_PING_CORE, &core_id);
            if (ret < 0) {
                perror("PING_CORE ioctl failed");
            } else {
                printf("Successfully pinged RT core!\n");
            }
            break;
            
        case 3:
            /* Get Status */
            ret = ioctl(fd, HETERO_IOC_GET_STATUS, &status);
            if (ret < 0) {
                perror("GET_STATUS ioctl failed");
            } else {
                printf("System status: 0x%x\n", status);
                printf("  IO Core: %s\n", (status & 0x1) ? "Online" : "Offline");
                printf("  RT Core: %s\n", (status & 0x2) ? "Online" : "Offline");
            }
            break;
            
        case 4:
            /* Send Message */
            printf("Enter core ID (0=IO, 1=RT): ");
            scanf("%d", &msg.core_id);
            printf("Enter command (hex): ");
            scanf("%x", &msg.cmd);
            printf("Enter data (hex): ");
            scanf("%x", &msg.data);
            
            ret = ioctl(fd, HETERO_IOC_SEND_MSG, &msg);
            if (ret < 0) {
                perror("SEND_MSG ioctl failed");
            } else {
                printf("Message sent successfully!\n");
            }
            break;
            
        case 5:
            /* Reset */
            ret = ioctl(fd, HETERO_IOC_RESET);
            if (ret < 0) {
                perror("RESET ioctl failed");
            } else {
                printf("System reset successfully!\n");
            }
            break;
            
        case 6:
            /* Read device info */
            lseek(fd, 0, SEEK_SET);
            ret = read(fd, buffer, sizeof(buffer) - 1);
            if (ret > 0) {
                buffer[ret] = '\0';
                printf("\nDevice info:\n%s", buffer);
            }
            break;
            
        case 0:
            /* Exit */
            printf("Exiting...\n");
            close(fd);
            return 0;
            
        default:
            printf("Invalid choice!\n");
        }
    }
    
    close(fd);
    return 0;
}
