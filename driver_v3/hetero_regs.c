/* hetero_regs.c - Day 2下午: 硬件寄存器模拟 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#define DRIVER_NAME "hetero_regs"
#define DEVICE_NAME "hetero_regs"

/* 内存区域大小 */
#define REG_SPACE_SIZE   4096    /* 4KB 寄存器空间 */
#define SHARED_MEM_SIZE  (32*1024) /* 32KB 共享内存 */
#define TOTAL_SIZE       (REG_SPACE_SIZE + SHARED_MEM_SIZE)

/* 寄存器偏移量（基于你的真实硬件设计） */
#define IPI_STATUS_OFFSET    0x00   /* @ 0xf0002000 */
#define IPI_TRIGGER_OFFSET   0x04   /* @ 0xf0002004 */
#define IPI_CLEAR_OFFSET     0x08   /* @ 0xf0002008 */
#define IPI_ENABLE_OFFSET    0x0C   /* @ 0xf000200c */

#define MBOX_MAIN_TO_CORE0_CMD_OFFSET   0x10  /* @ 0xf0002010 */
#define MBOX_MAIN_TO_CORE0_DATA_OFFSET  0x14  /* @ 0xf0002014 */
#define MBOX_CORE0_TO_MAIN_RESP_OFFSET  0x1C  /* @ 0xf000201c */

#define MBOX_MAIN_TO_CORE1_CMD_OFFSET   0x20  /* @ 0xf0002020 */
#define MBOX_MAIN_TO_CORE1_DATA_OFFSET  0x24  /* @ 0xf0002024 */
#define MBOX_CORE1_TO_MAIN_RESP_OFFSET  0x2C  /* @ 0xf000202c */

#define HW_MUTEX_REQUEST_OFFSET  0x30  /* @ 0xf0002040 */
#define HW_MUTEX_STATUS_OFFSET   0x34  /* @ 0xf0002044 */
#define HW_MUTEX_RELEASE_OFFSET  0x38  /* @ 0xf0002048 */

/* ioctl命令定义 */
#define HETERO_IOC_MAGIC 'h'
#define HETERO_IOC_GET_INFO      _IOR(HETERO_IOC_MAGIC, 1, struct hetero_info)
#define HETERO_IOC_CORE_STATUS   _IOR(HETERO_IOC_MAGIC, 2, int)
#define HETERO_IOC_SEND_IPI      _IOW(HETERO_IOC_MAGIC, 3, int)
#define HETERO_IOC_RESET         _IO(HETERO_IOC_MAGIC, 4)

struct hetero_info {
    int num_cores;
    int reg_size;
    int shared_size;
    unsigned long reg_base;
    unsigned long shared_base;
};

/* 模拟的硬件寄存器结构 */
struct hetero_hw_regs {
    /* IPI寄存器 */
    volatile u32 ipi_status;
    volatile u32 ipi_trigger;
    volatile u32 ipi_clear;
    volatile u32 ipi_enable;
    
    /* 邮箱寄存器 - Core 0 (IO核) */
    volatile u32 mbox_main_to_core0_cmd;
    volatile u32 mbox_main_to_core0_data;
    volatile u32 mbox_core0_to_main_status;
    volatile u32 mbox_core0_to_main_resp;
    
    /* 邮箱寄存器 - Core 1 (RT核) */
    volatile u32 mbox_main_to_core1_cmd;
    volatile u32 mbox_main_to_core1_data;
    volatile u32 mbox_core1_to_main_status;
    volatile u32 mbox_core1_to_main_resp;
    
    /* 互斥锁寄存器 */
    volatile u32 hw_mutex_request;
    volatile u32 hw_mutex_status;
    volatile u32 hw_mutex_release;
    
    /* 填充到4KB */
    u8 padding[4096 - 0x4C];
} __attribute__((packed));

struct hetero_device {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    
    /* 内存映射 */
    void *mem_base;              /* 整个映射区域基地址 */
    struct hetero_hw_regs *regs; /* 寄存器区域 */
    void *shared_mem;            /* 共享内存区域 */
    
    /* 工作队列 - 模拟小核响应 */
    struct work_struct core0_work;
    struct work_struct core1_work;
    
    /* 统计 */
    atomic_t ipi_count;
    atomic_t msg_count;
};

static struct hetero_device *hdev;

/* 模拟IO核(Core 0)的响应 */
static void core0_response_work(struct work_struct *work)
{
    struct hetero_device *dev = container_of(work, struct hetero_device, core0_work);
    u32 cmd, data;
    
    /* 读取邮箱命令 */
    cmd = dev->regs->mbox_main_to_core0_cmd;
    data = dev->regs->mbox_main_to_core0_data;
    
    if (cmd != 0) {
        pr_info("%s: [IO Core] 收到命令: cmd=0x%04x, data=0x%08x\n", 
                DRIVER_NAME, cmd, data);
        
        /* 模拟处理延迟 */
        msleep(1);
        
        /* 发送响应 */
        switch (cmd) {
        case 0x0001:  /* PING命令 */
            dev->regs->mbox_core0_to_main_resp = 0x8001;  /* PONG响应 */
            break;
        case 0x0010:  /* 读取状态 */
            dev->regs->mbox_core0_to_main_resp = 0x8010 | (jiffies & 0xFF);
            break;
        default:
            dev->regs->mbox_core0_to_main_resp = 0xFFFF;  /* 未知命令 */
        }
        
        /* 清除命令，表示已处理 */
        dev->regs->mbox_main_to_core0_cmd = 0;
        
        /* 设置状态位，通知主核 */
        dev->regs->mbox_core0_to_main_status = 1;
        
        pr_info("%s: [IO Core] 发送响应: 0x%04x\n", 
                DRIVER_NAME, dev->regs->mbox_core0_to_main_resp);
    }
    
    /* 清除IPI */
    dev->regs->ipi_status &= ~0x01;
}

/* 模拟RT核(Core 1)的响应 */
static void core1_response_work(struct work_struct *work)
{
    struct hetero_device *dev = container_of(work, struct hetero_device, core1_work);
    
    pr_info("%s: [RT Core] 收到IPI中断\n", DRIVER_NAME);
    
    /* RT核的快速响应 */
    dev->regs->mbox_core1_to_main_resp = 0x5200 | (jiffies & 0xFF);
    dev->regs->mbox_core1_to_main_status = 1;
    
    /* 清除IPI */
    dev->regs->ipi_status &= ~0x02;
}

/* mmap实现 */
static int hetero_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct hetero_device *dev = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;
    int ret;
    
    pr_info("%s: mmap called, size=%lu, offset=%lu\n", 
            DRIVER_NAME, size, vma->vm_pgoff << PAGE_SHIFT);
    
    /* 检查大小 */
    if (size > TOTAL_SIZE) {
        pr_err("%s: mmap size too large\n", DRIVER_NAME);
        return -EINVAL;
    }
    
    /* 获取物理页帧号 */
    pfn = virt_to_phys(dev->mem_base) >> PAGE_SHIFT;
    
    /* 设置不可缓存（重要！硬件寄存器必须这样） */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
    /* 映射 */
    ret = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
    if (ret) {
        pr_err("%s: remap_pfn_range failed\n", DRIVER_NAME);
        return ret;
    }
    
    pr_info("%s: mmap successful, user_addr=0x%lx\n", 
            DRIVER_NAME, vma->vm_start);
    
    return 0;
}

/* ioctl实现 */
static long hetero_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct hetero_device *dev = file->private_data;
    struct hetero_info info;
    int core_id;
    int ret = 0;
    
    switch (cmd) {
    case HETERO_IOC_GET_INFO:
        info.num_cores = 6;  /* 4主核 + 2小核 */
        info.reg_size = REG_SPACE_SIZE;
        info.shared_size = SHARED_MEM_SIZE;
        info.reg_base = 0;  /* mmap偏移0 */
        info.shared_base = REG_SPACE_SIZE;  /* 寄存器后面 */
        
        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        break;
        
    case HETERO_IOC_SEND_IPI:
        if (copy_from_user(&core_id, (void __user *)arg, sizeof(int)))
            return -EFAULT;
            
        pr_info("%s: 发送IPI到核心%d\n", DRIVER_NAME, core_id);
        
        /* 设置IPI触发寄存器 */
        dev->regs->ipi_trigger = (1 << core_id);
        dev->regs->ipi_status |= (1 << core_id);
        atomic_inc(&dev->ipi_count);
        
        /* 调度工作队列模拟小核响应 */
        if (core_id == 0) {
            schedule_work(&dev->core0_work);
        } else if (core_id == 1) {
            schedule_work(&dev->core1_work);
        }
        break;
        
    case HETERO_IOC_RESET:
        pr_info("%s: 系统复位\n", DRIVER_NAME);
        memset(dev->regs, 0, sizeof(struct hetero_hw_regs));
        atomic_set(&dev->ipi_count, 0);
        atomic_set(&dev->msg_count, 0);
        break;
        
    default:
        ret = -EINVAL;
    }
    
    return ret;
}

/* 文件操作 */
static int hetero_open(struct inode *inode, struct file *file)
{
    file->private_data = hdev;
    pr_info("%s: device opened\n", DRIVER_NAME);
    return 0;
}

static int hetero_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", DRIVER_NAME);
    return 0;
}

static struct file_operations hetero_fops = {
    .owner = THIS_MODULE,
    .open = hetero_open,
    .release = hetero_release,
    .mmap = hetero_mmap,
    .unlocked_ioctl = hetero_ioctl,
};

static int __init hetero_init(void)
{
    int ret;
    
    pr_info("%s: Loading driver with hardware register simulation\n", DRIVER_NAME);
    
    /* 分配设备结构 */
    hdev = kzalloc(sizeof(struct hetero_device), GFP_KERNEL);
    if (!hdev)
        return -ENOMEM;
    
    /* 分配连续内存区域 */
    hdev->mem_base = kmalloc(TOTAL_SIZE, GFP_KERNEL);
    if (!hdev->mem_base) {
        pr_err("%s: Failed to allocate memory\n", DRIVER_NAME);
        kfree(hdev);
        return -ENOMEM;
    }
    
    /* 设置指针 */
    hdev->regs = (struct hetero_hw_regs *)hdev->mem_base;
    hdev->shared_mem = hdev->mem_base + REG_SPACE_SIZE;
    
    /* 初始化内存 */
    memset(hdev->mem_base, 0, TOTAL_SIZE);
    
    /* 初始化寄存器默认值 */
    hdev->regs->ipi_enable = 0x03;  /* 启用Core0和Core1的IPI */
    hdev->regs->hw_mutex_status = 0xFFFF;  /* 所有锁都可用 */

    pr_info("%s: Debug - After init:\n", DRIVER_NAME);
    pr_info("  hw_mutex_status value: 0x%04x\n", hdev->regs->hw_mutex_status);
    pr_info("  hw_mutex_status addr: %p\n", &hdev->regs->hw_mutex_status);
    pr_info("  offset of hw_mutex_status: %ld\n", offsetof(struct hetero_hw_regs, hw_mutex_status));
    
    /* 在共享内存写入标识 */
    sprintf(hdev->shared_mem, "6-Core Heterogeneous System Shared Memory\n");
    
    /* 初始化工作队列 */
    INIT_WORK(&hdev->core0_work, core0_response_work);
    INIT_WORK(&hdev->core1_work, core1_response_work);
    
    /* 初始化计数器 */
    atomic_set(&hdev->ipi_count, 0);
    atomic_set(&hdev->msg_count, 0);
    
    pr_info("%s: Memory layout:\n", DRIVER_NAME);
    pr_info("  Registers: %p (0x000-0xFFF)\n", hdev->regs);
    pr_info("  Shared Mem: %p (0x1000-0x8FFF)\n", hdev->shared_mem);
    
    /* 注册字符设备 */
    ret = alloc_chrdev_region(&hdev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: alloc_chrdev_region failed\n", DRIVER_NAME);
        goto err_free;
    }
    
    cdev_init(&hdev->cdev, &hetero_fops);
    ret = cdev_add(&hdev->cdev, hdev->devno, 1);
    if (ret) {
        pr_err("%s: cdev_add failed\n", DRIVER_NAME);
        goto err_unreg;
    }
    
    /* 创建设备类 */
    hdev->class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(hdev->class)) {
        ret = PTR_ERR(hdev->class);
        goto err_cdev;
    }
    
    /* 创建设备节点 */
    hdev->device = device_create(hdev->class, NULL, hdev->devno,
                                NULL, DEVICE_NAME);
    if (IS_ERR(hdev->device)) {
        ret = PTR_ERR(hdev->device);
        goto err_class;
    }
    
    pr_info("%s: Driver loaded successfully! Device at /dev/%s\n", 
            DRIVER_NAME, DEVICE_NAME);
    
    return 0;

err_class:
    class_destroy(hdev->class);
err_cdev:
    cdev_del(&hdev->cdev);
err_unreg:
    unregister_chrdev_region(hdev->devno, 1);
err_free:
    kfree(hdev->mem_base);
    kfree(hdev);
    return ret;
}

static void __exit hetero_exit(void)
{
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
    
    /* 取消工作队列 */
    cancel_work_sync(&hdev->core0_work);
    cancel_work_sync(&hdev->core1_work);
    
    device_destroy(hdev->class, hdev->devno);
    class_destroy(hdev->class);
    cdev_del(&hdev->cdev);
    unregister_chrdev_region(hdev->devno, 1);
    
    /* 打印统计 */
    pr_info("%s: Statistics:\n", DRIVER_NAME);
    pr_info("  IPI count: %d\n", atomic_read(&hdev->ipi_count));
    pr_info("  Message count: %d\n", atomic_read(&hdev->msg_count));
    
    kfree(hdev->mem_base);
    kfree(hdev);
}

module_init(hetero_init);
module_exit(hetero_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("6-Core Heterogeneous System - Hardware Register Simulation");
MODULE_VERSION("0.3");
