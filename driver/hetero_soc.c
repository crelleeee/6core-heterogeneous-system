/*
 * hetero_soc_v2.c - 6核异构系统驱动 v2版本
 * 
 * 新增功能：
 * - ioctl接口：发送命令到小核
 * - 模拟邮箱通信
 * - 状态查询
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ioctl.h>  /* 新增：ioctl支持 */

#define DRIVER_NAME "hetero_soc"
#define DEVICE_NAME "hetero_soc"

/* ioctl命令定义 */
#define HETERO_IOC_MAGIC 'h'  /* 幻数，用于识别我们的ioctl */

/* 定义ioctl命令 */
#define HETERO_IOC_PING_CORE    _IOW(HETERO_IOC_MAGIC, 1, int)     /* 向指定核发送PING */
#define HETERO_IOC_GET_STATUS   _IOR(HETERO_IOC_MAGIC, 2, int)     /* 获取系统状态 */
#define HETERO_IOC_SEND_MSG     _IOW(HETERO_IOC_MAGIC, 3, struct hetero_msg)  /* 发送消息 */
#define HETERO_IOC_RESET        _IO(HETERO_IOC_MAGIC, 4)           /* 重置系统 */

/* 消息结构体 */
struct hetero_msg {
    int core_id;    /* 目标核心ID: 0=IO核, 1=RT核 */
    int cmd;        /* 命令 */
    int data;       /* 数据 */
};

/* 设备结构 - 添加了状态信息 */
struct hetero_device {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    
    /* 新增：设备状态 */
    int io_core_status;     /* IO核状态 */
    int rt_core_status;     /* RT核状态 */
    int msg_count;          /* 消息计数 */
    int last_cmd;           /* 最后的命令 */
};

static struct hetero_device *hdev;

/* 文件操作函数 */
static int hetero_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened by process %d (%s)\n", 
            DRIVER_NAME, current->pid, current->comm);
    return 0;
}

static int hetero_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", DRIVER_NAME);
    return 0;
}

static ssize_t hetero_read(struct file *file, char __user *buf, 
                          size_t count, loff_t *ppos)
{
    char info[512];
    int len;
    
    /* 构建状态信息 */
    len = snprintf(info, sizeof(info),
                   "=== 6-Core Heterogeneous RISC-V System ===\n"
                   "Architecture:\n"
                   "  - 4x Linux SMP cores\n"
                   "  - 1x IO processing core (status: %s)\n"
                   "  - 1x Real-time core (status: %s)\n"
                   "Communication:\n"
                   "  - 32 channel hardware mailbox\n"
                   "  - 32KB shared memory @ 0x80100000\n"
                   "Statistics:\n"
                   "  - Messages sent: %d\n"
                   "  - Last command: 0x%04x\n",
                   hdev->io_core_status ? "Online" : "Offline",
                   hdev->rt_core_status ? "Online" : "Offline",
                   hdev->msg_count,
                   hdev->last_cmd);
    
    if (*ppos >= len)
        return 0;
    
    if (count > len - *ppos)
        count = len - *ppos;
    
    if (copy_to_user(buf, info + *ppos, count))
        return -EFAULT;
    
    *ppos += count;
    return count;
}

static ssize_t hetero_write(struct file *file, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    pr_info("%s: write %zu bytes\n", DRIVER_NAME, count);
    /* 可以在这里解析用户写入的命令 */
    return count;
}

/* 新增：ioctl处理函数 */
static long hetero_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int core_id;
    struct hetero_msg msg;
    
    /* 检查命令类型 */
    if (_IOC_TYPE(cmd) != HETERO_IOC_MAGIC) {
        pr_err("%s: invalid ioctl magic number\n", DRIVER_NAME);
        return -ENOTTY;
    }
    
    switch (cmd) {
    case HETERO_IOC_PING_CORE:
        /* PING指定的核心 */
        if (copy_from_user(&core_id, (int __user *)arg, sizeof(int))) {
            return -EFAULT;
        }
        
        pr_info("%s: PING core %d\n", DRIVER_NAME, core_id);
        
        if (core_id == 0) {
            /* 模拟IO核响应 */
            hdev->io_core_status = 1;
            pr_info("%s: IO core responded to PING\n", DRIVER_NAME);
        } else if (core_id == 1) {
            /* 模拟RT核响应 */
            hdev->rt_core_status = 1;
            pr_info("%s: RT core responded to PING\n", DRIVER_NAME);
        } else {
            pr_err("%s: invalid core ID %d\n", DRIVER_NAME, core_id);
            return -EINVAL;
        }
        hdev->msg_count++;
        break;
        
    case HETERO_IOC_GET_STATUS:
        /* 返回系统状态 */
        ret = (hdev->io_core_status << 0) | (hdev->rt_core_status << 1);
        if (copy_to_user((int __user *)arg, &ret, sizeof(int))) {
            return -EFAULT;
        }
        pr_info("%s: status query, result=0x%x\n", DRIVER_NAME, ret);
        break;
        
    case HETERO_IOC_SEND_MSG:
        /* 发送消息到指定核心 */
        if (copy_from_user(&msg, (struct hetero_msg __user *)arg, sizeof(msg))) {
            return -EFAULT;
        }
        
        pr_info("%s: send message to core %d: cmd=0x%x, data=0x%x\n",
                DRIVER_NAME, msg.core_id, msg.cmd, msg.data);
        
        /* 模拟发送消息 */
        hdev->last_cmd = msg.cmd;
        hdev->msg_count++;
        
        /* 这里将来会真正操作硬件寄存器 */
        /* iowrite32(msg.data, MBOX_DATA_REG); */
        /* iowrite32(msg.cmd, MBOX_CMD_REG); */
        break;
        
    case HETERO_IOC_RESET:
        /* 重置系统状态 */
        pr_info("%s: system reset requested\n", DRIVER_NAME);
        hdev->io_core_status = 0;
        hdev->rt_core_status = 0;
        hdev->msg_count = 0;
        hdev->last_cmd = 0;
        break;
        
    default:
        pr_err("%s: unknown ioctl command 0x%x\n", DRIVER_NAME, cmd);
        return -ENOTTY;
    }
    
    return ret;
}

static struct file_operations hetero_fops = {
    .owner = THIS_MODULE,
    .open = hetero_open,
    .release = hetero_release,
    .read = hetero_read,
    .write = hetero_write,
    .unlocked_ioctl = hetero_ioctl,  /* 新增：ioctl处理 */
};

/* 模块初始化 */
static int __init hetero_init(void)
{
    int ret;
    
    pr_info("%s: Loading 6-core heterogeneous SoC driver v2\n", DRIVER_NAME);
    
    /* 分配设备结构 */
    hdev = kzalloc(sizeof(struct hetero_device), GFP_KERNEL);
    if (!hdev) {
        pr_err("%s: Failed to allocate memory\n", DRIVER_NAME);
        return -ENOMEM;
    }
    
    /* 初始化状态 */
    hdev->io_core_status = 0;
    hdev->rt_core_status = 0;
    hdev->msg_count = 0;
    hdev->last_cmd = 0;
    
    /* 分配设备号 */
    ret = alloc_chrdev_region(&hdev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: Failed to allocate device number\n", DRIVER_NAME);
        kfree(hdev);
        return ret;
    }
    pr_info("%s: Got major number %d\n", DRIVER_NAME, MAJOR(hdev->devno));
    
    /* 初始化字符设备 */
    cdev_init(&hdev->cdev, &hetero_fops);
    hdev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&hdev->cdev, hdev->devno, 1);
    if (ret) {
        pr_err("%s: Failed to add cdev\n", DRIVER_NAME);
        unregister_chrdev_region(hdev->devno, 1);
        kfree(hdev);
        return ret;
    }
    
    /* 创建设备类 */
    hdev->class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(hdev->class)) {
        pr_err("%s: Failed to create class\n", DRIVER_NAME);
        cdev_del(&hdev->cdev);
        unregister_chrdev_region(hdev->devno, 1);
        kfree(hdev);
        return PTR_ERR(hdev->class);
    }
    
    /* 创建设备 */
    hdev->device = device_create(hdev->class, NULL, hdev->devno, 
                                NULL, DEVICE_NAME);
    if (IS_ERR(hdev->device)) {
        pr_err("%s: Failed to create device\n", DRIVER_NAME);
        class_destroy(hdev->class);
        cdev_del(&hdev->cdev);
        unregister_chrdev_region(hdev->devno, 1);
        kfree(hdev);
        return PTR_ERR(hdev->device);
    }
    
    pr_info("%s: Driver loaded successfully (with ioctl support)\n", DRIVER_NAME);
    pr_info("%s: Device created at /dev/%s\n", DRIVER_NAME, DEVICE_NAME);
    
    return 0;
}

/* 模块卸载 */
static void __exit hetero_exit(void)
{
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
    
    device_destroy(hdev->class, hdev->devno);
    class_destroy(hdev->class);
    cdev_del(&hdev->cdev);
    unregister_chrdev_region(hdev->devno, 1);
    kfree(hdev);
    
    pr_info("%s: Driver unloaded\n", DRIVER_NAME);
}

module_init(hetero_init);
module_exit(hetero_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("6-Core Heterogeneous RISC-V SoC Driver v2");
MODULE_VERSION("0.2");
