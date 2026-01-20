#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
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
	{ 0xa879d70c, "module_layout" },
	{ 0x336a5f22, "class_unregister" },
	{ 0x7adc5eab, "device_destroy" },
	{ 0x111bd0bd, "class_destroy" },
	{ 0x213ef32e, "device_create" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0x6c496836, "__class_create" },
	{ 0x316f58eb, "__register_chrdev" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "BA9D4E094A3CD9F90C7C074");
