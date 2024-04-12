// SPDX-License-Identifier: GPL-2.0
/*
 * livepatch.c - arm64-specific Kernel Live Patching Core
 *
 * Copyright (C) 2014 Li Bin <huawei.libin@huawei.com>
 * Copyright (C) 2023 Zheng Yejian <zhengyejian1@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/livepatch.h>
#include <asm/livepatch.h>
#include <asm/stacktrace.h>
#include <linux/slab.h>
#include <asm/insn.h>
#include <asm-generic/sections.h>
#include <asm/patching.h>
#include <asm/debug-monitors.h>
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>

#define CHECK_JUMP_RANGE LJMP_INSN_SIZE

static inline bool offset_in_range(unsigned long pc, unsigned long addr,
		long range)
{
	long offset = addr - pc;

	return (offset >= -range && offset < range);
}

/*
 * The instruction set on arm64 is A64.
 * The instruction of BLR is 1101011000111111000000xxxxx00000.
 * The instruction of BL is 100101xxxxxxxxxxxxxxxxxxxxxxxxxx.
 * The instruction of BLRAX is 1101011x0011111100001xxxxxxxxxxx.
 */
#define is_jump_insn(insn) (((le32_to_cpu(insn) & 0xfffffc1f) == 0xd63f0000) || \
		((le32_to_cpu(insn) & 0xfc000000) == 0x94000000) || \
		((le32_to_cpu(insn) & 0xfefff800) == 0xd63f0800))

bool arch_check_jump_insn(unsigned long func_addr)
{
	unsigned long i;
	u32 *insn = (u32 *)func_addr;

	for (i = 0; i < CHECK_JUMP_RANGE; i++) {
		if (is_jump_insn(*insn))
			return true;
		insn++;
	}
	return false;
}

static bool klp_check_jump_func(void *ws_args, unsigned long pc)
{
	struct walk_stackframe_args *args = ws_args;

	return args->check_func(args->data, &args->ret, pc);
}

static int check_task_calltrace(struct task_struct *t,
				struct walk_stackframe_args *args,
				bool (*fn)(void *, unsigned long))
{
	arch_stack_walk(fn, args, t, NULL);
	if (args->ret) {
		pr_info("PID: %d Comm: %.20s\n", t->pid, t->comm);
		show_stack(t, NULL, KERN_INFO);
		return args->ret;
	}
	return 0;
}

static int do_check_calltrace(struct walk_stackframe_args *args,
			      bool (*fn)(void *, unsigned long))
{
	int ret;
	struct task_struct *g, *t;
	unsigned int cpu;

	for_each_process_thread(g, t) {
		if (klp_is_migration_thread(t->comm))
			continue;
		if (klp_is_thread_dead(t))
			continue;
		ret = check_task_calltrace(t, args, fn);
		if (ret)
			return ret;
	}
	for_each_online_cpu(cpu) {
		ret = check_task_calltrace(idle_task(cpu), args, fn);
		if (ret)
			return ret;
	}
	return 0;
}

int arch_klp_check_calltrace(bool (*check_func)(void *, int *, unsigned long), void *data)
{
	struct walk_stackframe_args args = {
		.data = data,
		.ret = 0,
		.check_func = check_func,
	};

	return do_check_calltrace(&args, klp_check_jump_func);
}

static bool check_module_calltrace(void *ws_args, unsigned long pc)
{
	struct walk_stackframe_args *args = ws_args;
	struct module *mod = args->data;

	if (within_module_core(pc, mod)) {
		pr_err("module %s is in use!\n", mod->name);
		args->ret = -EBUSY;
		return false;
	}
	return true;
}

int arch_klp_module_check_calltrace(void *data)
{
	struct walk_stackframe_args args = {
		.data = data,
		.ret = 0
	};

	return do_check_calltrace(&args, check_module_calltrace);
}

int arch_klp_add_breakpoint(struct arch_klp_data *arch_data, void *old_func)
{
	u32 insn = BRK64_OPCODE_KLP;
	u32 *addr = (u32 *)old_func;

	arch_data->saved_opcode = le32_to_cpu(*addr);
	aarch64_insn_patch_text(&old_func, &insn, 1);
	return 0;
}

void arch_klp_remove_breakpoint(struct arch_klp_data *arch_data, void *old_func)
{
	aarch64_insn_patch_text(&old_func, &arch_data->saved_opcode, 1);
}

static int klp_breakpoint_handler(struct pt_regs *regs, unsigned long esr)
{
	void *brk_func = NULL;
	unsigned long addr = instruction_pointer(regs);

	brk_func = klp_get_brk_func((void *)addr);
	if (!brk_func) {
		pr_warn("Unrecoverable livepatch detected.\n");
		BUG();
	}

	instruction_pointer_set(regs, (unsigned long)brk_func);
	return 0;
}

static struct break_hook klp_break_hook = {
	.imm = KLP_BRK_IMM,
	.fn = klp_breakpoint_handler,
};

void arch_klp_init(void)
{
	register_kernel_break_hook(&klp_break_hook);
}

long arch_klp_save_old_code(struct arch_klp_data *arch_data, void *old_func)
{
	long ret;
	int i;

	for (i = 0; i < LJMP_INSN_SIZE; i++) {
		ret = aarch64_insn_read(((u32 *)old_func) + i,
					&arch_data->old_insns[i]);
		if (ret)
			break;
	}
	return ret;
}

static int klp_patch_text(u32 *dst, const u32 *src, int len)
{
	int i;
	int ret;

	if (len <= 0)
		return -EINVAL;
	/* skip breakpoint at first */
	for (i = 1; i < len; i++) {
		ret = aarch64_insn_patch_text_nosync(dst + i, src[i]);
		if (ret)
			return ret;
	}
	/*
	 * Avoid compile optimization, make sure that instructions
	 * except first breakpoint has been patched.
	 */
	barrier();
	return aarch64_insn_patch_text_nosync(dst, src[0]);
}

static int do_patch(unsigned long pc, unsigned long new_addr)
{
	u32 insns[LJMP_INSN_SIZE];
	int ret;

	if (offset_in_range(pc, new_addr, SZ_128M)) {
		insns[0] = aarch64_insn_gen_branch_imm(pc, new_addr,
						       AARCH64_INSN_BRANCH_NOLINK);
		ret = klp_patch_text((u32 *)pc, insns, 1);
		if (ret) {
			pr_err("patch instruction small range failed, ret=%d\n", ret);
			return -EPERM;
		}
	} else {
		/* movn x16, #0x....                    */
		/* movk x16, #0x...., lsl #16           */
		/* movk x16, #0x...., lsl #32           */
		/* br   x16                             */
		insns[0] = 0x92800010 | (((~new_addr) & 0xffff)) << 5;
		insns[1] = 0xf2a00010 | (((new_addr >> 16) & 0xffff)) << 5;
		insns[2] = 0xf2c00010 | (((new_addr >> 32) & 0xffff)) << 5;
		insns[3] = 0xd61f0200;
		ret = klp_patch_text((u32 *)pc, insns, LJMP_INSN_SIZE);
		if (ret) {
			pr_err("patch instruction large range failed, ret=%d\n", ret);
			return -EPERM;
		}
	}
	return 0;
}

int arch_klp_patch_func(struct klp_func *func)
{
	struct klp_func_node *func_node;
	int ret;

	func_node = func->func_node;
	list_add_rcu(&func->stack_node, &func_node->func_stack);
	ret = do_patch((unsigned long)func->old_func, (unsigned long)func->new_func);
	if (ret)
		list_del_rcu(&func->stack_node);
	return ret;
}

void arch_klp_unpatch_func(struct klp_func *func)
{
	struct klp_func_node *func_node;
	struct klp_func *next_func;
	unsigned long pc;
	int ret;

	func_node = func->func_node;
	pc = (unsigned long)func_node->old_func;
	list_del_rcu(&func->stack_node);
	if (list_empty(&func_node->func_stack)) {
		ret = klp_patch_text((u32 *)pc, func_node->arch_data.old_insns, LJMP_INSN_SIZE);
		if (ret) {
			pr_err("restore instruction failed, ret=%d\n", ret);
			return;
		}
	} else {
		next_func = list_first_or_null_rcu(&func_node->func_stack,
					struct klp_func, stack_node);
		if (WARN_ON(!next_func))
			return;
		do_patch(pc, (unsigned long)next_func->new_func);
	}
}
