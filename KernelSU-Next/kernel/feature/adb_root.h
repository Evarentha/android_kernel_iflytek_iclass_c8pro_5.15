#ifndef __KSU_H_ADB_ROOT
#define __KSU_H_ADB_ROOT
#include <asm/ptrace.h>
struct filename;
struct user_arg_ptr;

int ksu_adb_root_handle_execveat_manual(struct filename **filename,
				      struct user_arg_ptr *envp);

long ksu_adb_root_handle_execve(struct pt_regs *regs);
long ksu_adb_root_handle_execveat(struct pt_regs *regs);

void ksu_adb_root_init(void);

void ksu_adb_root_exit(void);

#endif
