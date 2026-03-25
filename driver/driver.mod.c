#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
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
__used __section("__versions") = {
	{ 0x92997ed8, "_printk" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0xe97c4103, "ioremap" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xedc03953, "iounmap" },
	{ 0x11edec04, "cdev_init" },
	{ 0x927eea51, "cdev_add" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xb36a3b52, "class_create" },
	{ 0xa5811e8e, "cdev_del" },
	{ 0x65e7a2ed, "device_create" },
	{ 0x3872efb0, "class_destroy" },
	{ 0x822137e2, "arm_heavy_mb" },
	{ 0x5a0824e9, "device_destroy" },
	{ 0x5d8acd7b, "dma_alloc_attrs" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0x580fc92d, "dma_free_attrs" },
	{ 0x5f754e5a, "memset" },
	{ 0x526c3a6c, "jiffies" },
	{ 0x8e865d3c, "arm_delay_ops" },
	{ 0x6c5bee28, "module_layout" },
};

MODULE_INFO(depends, "");

