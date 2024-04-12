// SPDX-License-Identifier: GPL-2.0-only
/*
 * livepatch.c - arm-specific Kernel Live Patching Core
 *
 * Copyright (C) 2018  Huawei Technologies Co., Ltd.
 * Copyright (C) 2023  Huawei Technologies Co., Ltd.
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
#include <linux/sched/debug.h>
#include <asm/livepatch.h>
#include <asm/stacktrace.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <asm/traps.h>
#include <asm/insn.h>
#include <asm/patch.h>

#ifdef ARM_INSN_SIZE
#error "ARM_INSN_SIZE have been redefined, please check"
#else
#define ARM_INSN_SIZE	4
#endif

#define CHECK_JUMP_RANGE LJMP_INSN_SIZE

/*
 * The instruction set on arm is A32.
 * The instruction of BL is xxxx1011xxxxxxxxxxxxxxxxxxxxxxxx, and first four
 * bits could not be 1111.
 * The instruction of BLX(immediate) is 1111101xxxxxxxxxxxxxxxxxxxxxxxxx.
 * The instruction of BLX(register) is xxxx00010010xxxxxxxxxxxx0011xxxx, and
 * first four bits could not be 1111.
 */
static bool is_jump_insn(u32 insn)
{
	if (((insn & 0x0f000000) == 0x0b000000) &&
		((insn & 0xf0000000) != 0xf0000000))
		return true;
	if ((insn & 0xfe000000) == 0xfa000000)
		return true;
	if (((insn & 0x0ff000f0) == 0x01200030) &&
		((insn & 0xf0000000) != 0xf0000000))
		return true;
	return false;
}

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
	struct stackframe frame;

	if (t == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.lr = (unsigned long)__builtin_return_address(0);
		frame.pc = (unsigned long)check_task_calltrace;
	} else {
		frame.fp = thread_saved_fp(t);
		frame.sp = thread_saved_sp(t);
		frame.lr = 0;           /* recovered from the stack */
		frame.pc = thread_saved_pc(t);
	}
	walk_stackframe(&frame, fn, args);
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
		return (args->ret = -EBUSY);
	}
	return 0;
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
	u32 *addr = (u32 *)old_func;

	arch_data->saved_opcode = le32_to_cpu(*addr);
	patch_text(old_func, KLP_ARM_BREAKPOINT_INSTRUCTION);
	return 0;
}

void arch_klp_remove_breakpoint(struct arch_klp_data *arch_data, void *old_func)
{
	patch_text(old_func, arch_data->saved_opcode);
}

static int klp_trap_handler(struct pt_regs *regs, unsigned int instr)
{
	void *brk_func = NULL;
	unsigned long addr = regs->ARM_pc;

	brk_func = klp_get_brk_func((void *)addr);
	if (!brk_func) {
		pr_warn("Unrecoverable livepatch detected.\n");
		BUG();
	}

	regs->ARM_pc = (unsigned long)brk_func;
	return 0;
}

static struct undef_hook klp_arm_break_hook = {
	.instr_mask	= 0x0fffffff,
	.instr_val	= (KLP_ARM_BREAKPOINT_INSTRUCTION & 0x0fffffff),
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= SVC_MODE,
	.fn		= klp_trap_handler,
};

void arch_klp_init(void)
{
	register_undef_hook(&klp_arm_break_hook);
}

static inline bool offset_in_range(unsigned long pc, unsigned long addr,
				   long range)
{
	long offset = addr - pc;

	return (offset >= -range && offset < range);
}

long arm_insn_read(void *addr, u32 *insnp)
{
	long ret;
	u32 val;

	ret = copy_from_kernel_nofault(&val, addr, ARM_INSN_SIZE);
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

long arch_klp_save_old_code(struct arch_klp_data *arch_data, void *old_func)
{
	long ret;
	int i;

	for (i = 0; i < LJMP_INSN_SIZE; i++) {
		ret = arm_insn_read((u32 *)old_func + i, &arch_data->old_insns[i]);
		if (ret)
			break;
	}
	return ret;
}

static void klp_patch_text(u32 *dst, const u32 *src, int len)
{
	int i;

	if (len <= 0)
		return;
	/* skip breakpoint at first */
	for (i = 1; i < len; i++)
		__patch_text(dst + i, src[i]);
	/*
	 * Avoid compile optimization, make sure that instructions
	 * except first breakpoint has been patched.
	 */
	barrier();
	__patch_text(dst, src[0]);
}

static int do_patch(unsigned long pc, unsigned long new_addr)
{
	u32 insns[LJMP_INSN_SIZE];

	if (!offset_in_range(pc, new_addr, SZ_32M)) {
#ifdef CONFIG_ARM_MODULE_PLTS
		/*
		 * [0] LDR PC, [PC+8]
		 * [4] nop
		 * [8] new_addr_to_jump
		 */
		insns[0] = __opcode_to_mem_arm(0xe59ff000);
		insns[1] = __opcode_to_mem_arm(0xe320f000);
		insns[2] = new_addr;

		klp_patch_text((u32 *)pc, insns, LJMP_INSN_SIZE);
#else
		/*
		 * When offset from 'new_addr' to 'pc' is out of SZ_32M range but
		 * CONFIG_ARM_MODULE_PLTS not enabled, we should stop patching.
		 */
		pr_err("new address out of range\n");
		return -EFAULT;
#endif
	} else {
		insns[0] = arm_gen_branch(pc, new_addr);
		klp_patch_text((u32 *)pc, insns, 1);
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

	func_node = func->func_node;
	pc = (unsigned long)func_node->old_func;
	list_del_rcu(&func->stack_node);
	if (list_empty(&func_node->func_stack)) {
		klp_patch_text((u32 *)pc, func_node->arch_data.old_insns, LJMP_INSN_SIZE);
	} else {
		next_func = list_first_or_null_rcu(&func_node->func_stack,
					struct klp_func, stack_node);

		do_patch(pc, (unsigned long)next_func->new_func);
	}
}
