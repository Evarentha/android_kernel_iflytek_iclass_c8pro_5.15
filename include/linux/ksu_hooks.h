/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSU_HOOKS_H
#define _LINUX_KSU_HOOKS_H

#include <linux/compiler_types.h>

struct filename;

#ifdef CONFIG_KSU_MANUAL_HOOK
int ksu_handle_execveat(int *fd, struct filename **filename, void *argv,
		       void *envp, int *flags);
int ksu_handle_faccessat(int *dfd, const char __user **filename,
			int *mode, int *flags);
int ksu_handle_stat(int *dfd, const char __user **filename, int *flags);
void ksu_handle_sys_read(unsigned int fd);
int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd,
			  void __user **arg);
int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code,
				 int *value);
#endif

#endif
