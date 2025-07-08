/* hetero_mmap.c - Day 2: 添加mmap支持 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "hetero_mmap"
#define DEVICE_NAME "hetero_mmap"
#define SHARED_SIZE (32 * 1024)  /* 32KB 共享内存 */

struct hetero_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    
    /* 共享内存 */
    void *shared_mem;      /* 内核虚拟地址 */
    phys_addr_t phys_addr; /* 物理地址 */
};

static struct hetero_dev *hdev;

/* 打开设备 */
static int hetero_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened\n", DRIVER_NAME);
    file->private_data = hdev;
    return 0;
}

/* 关闭设备 */
static int hetero_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", DRIVER_NAME);
    return 0;
}

/* 读取 - 用于测试 */
static ssize_t hetero_read(struct file *file, char __user *buf, 
                          size_t count, loff_t *ppos)
{
    struct hetero_dev *dev = file->private_data;
    size_t len;
    
    if (*ppos >= SHARED_SIZE)
        return 0;
    
    len = min(count, (size_t)(SHARED_SIZE - *ppos));
    
    if (copy_to_user(buf, dev->shared_mem + *ppos, len))
        return -EFAULT;
    
    *ppos += len;
    pr_info("%s: read %zu bytes from offset %lld\n", 
            DRIVER_NAME, len, *ppos - len);
    
    return len;
}

/* 写入 - 用于测试 */
static ssize_t hetero_write(struct file *file, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct hetero_dev *dev = file->private_data;
    size_t len;
    
    if (*ppos >= SHARED_SIZE)
        return 0;
    
    len = min(count, (size_t)(SHARED_SIZE - *ppos));
    
    if (copy_from_user(dev->shared_mem + *ppos, buf, len))
        return -EFAULT;
    
    *ppos += len;
    pr_info("%s: wrote %zu bytes to offset %lld\n", 
            DRIVER_NAME, len, *ppos - len);
    
    return len;
}

/* mmap 实现 - 这是今天的重点！ */
static int hetero_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct hetero_dev *dev = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;
    int ret;
    
    pr_info("%s: mmap called, size=%lu\n", DRIVER_NAME, size);
    
    /* 检查映射大小 */
    if (size > SHARED_SIZE) {
        pr_err("%s: mmap size %lu exceeds limit %d\n", 
               DRIVER_NAME, size, SHARED_SIZE);
        return -EINVAL;
    }
    
    /* 获取物理页帧号 (PFN) */
    pfn = virt_to_phys(dev->shared_mem) >> PAGE_SHIFT;
    pr_info("%s: mapping pfn=0x%lx to user addr=0x%lx\n", 
            DRIVER_NAME, pfn, vma->vm_start);
    
    /* 对于与硬件共享的内存，通常需要禁用缓存 */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
    /* 将物理内存映射到用户空间 */
    ret = remap_pfn_range(vma, 
                         vma->vm_start,   /* 用户空间起始地址 */
                         pfn,             /* 物理页帧号 */
                         size,            /* 映射大小 */
                         vma->vm_page_prot); /* 页保护标志 */
    
    if (ret) {
        pr_err("%s: remap_pfn_range failed, ret=%d\n", DRIVER_NAME, ret);
        return ret;
    }
    
    pr_info("%s: mmap successful!\n", DRIVER_NAME);
    return 0;
}

static struct file_operations hetero_fops = {
    .owner = THIS_MODULE,
    .open = hetero_open,
    .release = hetero_release,
    .read = hetero_read,
    .write = hetero_write,
    .mmap = hetero_mmap,  /* 新增！ */
};

static int __init hetero_init(void)
{
    int ret;
    
    pr_info("%s: Loading driver with mmap support\n", DRIVER_NAME);
    
    /* 分配设备结构 */
    hdev = kzalloc(sizeof(struct hetero_dev), GFP_KERNEL);
    if (!hdev) {
        pr_err("%s: kzalloc failed\n", DRIVER_NAME);
        return -ENOMEM;
    }
    
    /* 分配共享内存 - 使用kmalloc保证物理连续 */
    hdev->shared_mem = kmalloc(SHARED_SIZE, GFP_KERNEL);
    if (!hdev->shared_mem) {
        pr_err("%s: kmalloc failed for shared memory\n", DRIVER_NAME);
        kfree(hdev);
        return -ENOMEM;
    }
    
    /* 初始化共享内存 */
    memset(hdev->shared_mem, 0, SHARED_SIZE);
    sprintf(hdev->shared_mem, "Hello from kernel! Time: %ld\n", jiffies);
    
    /* 记录物理地址 */
    hdev->phys_addr = virt_to_phys(hdev->shared_mem);
    pr_info("%s: Allocated shared memory - virt: %p, phys: 0x%llx\n",
            DRIVER_NAME, hdev->shared_mem, hdev->phys_addr);
    
    /* 注册字符设备 */
    ret = alloc_chrdev_region(&hdev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: alloc_chrdev_region failed\n", DRIVER_NAME);
        goto err_free_mem;
    }
    
    cdev_init(&hdev->cdev, &hetero_fops);
    hdev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&hdev->cdev, hdev->devno, 1);
    if (ret) {
        pr_err("%s: cdev_add failed\n", DRIVER_NAME);
        goto err_unreg;
    }
    
    /* 创建设备类 */
    hdev->class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(hdev->class)) {
        pr_err("%s: class_create failed\n", DRIVER_NAME);
        ret = PTR_ERR(hdev->class);
        goto err_cdev;
    }
    
    /* 创建设备节点 */
    hdev->device = device_create(hdev->class, NULL, hdev->devno,
                                NULL, DEVICE_NAME);
    if (IS_ERR(hdev->device)) {
        pr_err("%s: device_create failed\n", DRIVER_NAME);
        ret = PTR_ERR(hdev->device);
        goto err_class;
    }
    
    pr_info("%s: Driver loaded successfully!\n", DRIVER_NAME);
    pr_info("%s: Device created at /dev/%s\n", DRIVER_NAME, DEVICE_NAME);
    
    return 0;

err_class:
    class_destroy(hdev->class);
err_cdev:
    cdev_del(&hdev->cdev);
err_unreg:
    unregister_chrdev_region(hdev->devno, 1);
err_free_mem:
    kfree(hdev->shared_mem);
    kfree(hdev);
    return ret;
}

static void __exit hetero_exit(void)
{
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
    
    device_destroy(hdev->class, hdev->devno);
    class_destroy(hdev->class);
    cdev_del(&hdev->cdev);
    unregister_chrdev_region(hdev->devno, 1);
    
    /* 释放共享内存 */
    if (hdev->shared_mem) {
        pr_info("%s: Freeing shared memory\n", DRIVER_NAME);
        kfree(hdev->shared_mem);
    }
    
    kfree(hdev);
    pr_info("%s: Driver unloaded\n", DRIVER_NAME);
}

module_init(hetero_init);
module_exit(hetero_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("6-Core Heterogeneous System Driver - Day 2: mmap");
MODULE_VERSION("0.2");
