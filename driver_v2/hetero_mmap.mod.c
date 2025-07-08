#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xe4c970fb, "module_layout" },
	{ 0x260bbe45, "device_destroy" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x60e2055d, "cdev_del" },
	{ 0x201d0f70, "class_destroy" },
	{ 0x241c4110, "device_create" },
	{ 0xa640d12, "__class_create" },
	{ 0x47cb61f7, "cdev_add" },
	{  0x22a98, "cdev_init" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x37a0cba, "kfree" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0xc622a0d1, "kmem_cache_alloc_trace" },
	{ 0xe94c7a3a, "kmalloc_caches" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x56470118, "__warn_printk" },
	{ 0xb30f29d6, "remap_pfn_range" },
	{ 0xb665f56d, "__cachemode2pte_tbl" },
	{ 0xeb8461e2, "boot_cpu_data" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "046A6B79A4F1F694EF17B27");
