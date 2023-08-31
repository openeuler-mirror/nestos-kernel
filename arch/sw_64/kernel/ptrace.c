// SPDX-License-Identifier: GPL-2.0
/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* mangled further by Bob Manson (manson@santafe.edu) */
/* more mutilation by David Mosberger (davidm@azstarnet.com) */

#include <linux/tracehook.h>
#include <linux/audit.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/sched/task_stack.h>

#include <asm/reg.h>
#include <asm/asm-offsets.h>

#include "proto.h"

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#define BREAKINST	0x00000080 /* sys_call bpt */

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Processes always block with the following stack-layout:
 *
 *  +================================+ <---- task + 2*PAGE_SIZE
 *  | HMcode saved frame (ps, pc,    | ^
 *  | gp, a0, a1, a2)		     | |
 *  +================================+ | struct pt_regs
 *  |				     | |
 *  | frame generated by SAVE_ALL    | |
 *  |				     | v
 *  +================================+
 */

/*
 * The following table maps a register index into the stack offset at
 * which the register is saved.  Register indices are 0-31 for integer
 * regs, 32-63 for fp regs, and 64 for the pc.  Notice that sp and
 * zero have no stack-slot and need to be treated specially (see
 * get_reg/put_reg below).
 */
#define R(x)	((size_t) &((struct pt_regs *)0)->x)

short regoffsets[32] = {
	R(r0), R(r1), R(r2), R(r3), R(r4), R(r5), R(r6), R(r7), R(r8),
	R(r9), R(r10), R(r11), R(r12), R(r13), R(r14), R(r15),
	R(r16), R(r17), R(r18),
	R(r19), R(r20), R(r21), R(r22), R(r23), R(r24), R(r25), R(r26),
	R(r27), R(r28), R(gp), 0, 0
};

#undef R

#define PCB_OFF(var)	offsetof(struct pcb_struct, var)

static int pcboff[] = {
	[USP] = PCB_OFF(usp),
	[TP] = PCB_OFF(tp),
	[DA_MATCH] = PCB_OFF(da_match),
	[DA_MASK] = PCB_OFF(da_mask),
	[DV_MATCH] = PCB_OFF(dv_match),
	[DV_MASK] = PCB_OFF(dv_mask),
	[DC_CTL] = PCB_OFF(dc_ctl)
};

static unsigned long zero;

/*
 * Get address of register REGNO in task TASK.
 */

static unsigned long *
get_reg_addr(struct task_struct *task, unsigned long regno)
{
	void *addr;
	int fno, vno;

	switch (regno) {
	case USP:
	case UNIQUE:
	case DA_MATCH:
	case DA_MASK:
	case DV_MATCH:
	case DV_MASK:
	case DC_CTL:
		addr = (void *)task_thread_info(task) + pcboff[regno];
		break;
	case REG_BASE ... REG_END:
		addr = (void *)task_pt_regs(task) + regoffsets[regno];
		break;
	case FPREG_BASE ... FPREG_END:
		fno = regno - FPREG_BASE;
		addr = &task->thread.fpstate.fp[fno].v[0];
		break;
	case VECREG_BASE ... VECREG_END:
		/*
		 * return addr for zero value if we catch vectors of f31
		 * v0 and v3 of f31 are not in this range so ignore them
		 */
		if (regno == F31_V1 || regno == F31_V2) {
			addr = &zero;
			break;
		}
		fno = (regno - VECREG_BASE) & 0x1f;
		vno = 1 + ((regno - VECREG_BASE) >> 5);
		addr = &task->thread.fpstate.fp[fno].v[vno];
		break;
	case FPCR:
		addr = &task->thread.fpstate.fpcr;
		break;
	case PC:
		addr = (void *)task_pt_regs(task) + PT_REGS_PC;
		break;
	default:
		addr = &zero;
	}

	return addr;
}

/*
 * Get contents of register REGNO in task TASK.
 */
unsigned long
get_reg(struct task_struct *task, unsigned long regno)
{
	return *get_reg_addr(task, regno);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static int
put_reg(struct task_struct *task, unsigned long regno, unsigned long data)
{
	*get_reg_addr(task, regno) = data;
	return 0;
}

static inline int
read_int(struct task_struct *task, unsigned long addr, int *data)
{
	int copied = access_process_vm(task, addr, data, sizeof(int), FOLL_FORCE);

	return (copied == sizeof(int)) ? 0 : -EIO;
}

static inline int
write_int(struct task_struct *task, unsigned long addr, int data)
{
	int copied = access_process_vm(task, addr, &data, sizeof(int),
			FOLL_FORCE | FOLL_WRITE);
	return (copied == sizeof(int)) ? 0 : -EIO;
}

/*
 * Set breakpoint.
 */
int
ptrace_set_bpt(struct task_struct *child)
{
	int displ, i, res, reg_b, nsaved = 0;
	unsigned int insn, op_code;
	unsigned long pc;

	pc = get_reg(child, PC);
	res = read_int(child, pc, (int *)&insn);
	if (res < 0)
		return res;

	op_code = insn >> 26;
	/* br bsr beq bne blt ble bgt bge blbc blbs fbeq fbne fblt fble fbgt fbge */
	if ((1UL << op_code) & 0x3fff000000000030UL) {
		/*
		 * It's a branch: instead of trying to figure out
		 * whether the branch will be taken or not, we'll put
		 * a breakpoint at either location.  This is simpler,
		 * more reliable, and probably not a whole lot slower
		 * than the alternative approach of emulating the
		 * branch (emulation can be tricky for fp branches).
		 */
		displ = ((s32)(insn << 11)) >> 9;
		task_thread_info(child)->bpt_addr[nsaved++] = pc + 4;
		if (displ) /* guard against unoptimized code */
			task_thread_info(child)->bpt_addr[nsaved++]
				= pc + 4 + displ;
		/*call ret jmp*/
	} else if (op_code >= 0x1 && op_code <= 0x3) {
		reg_b = (insn >> 16) & 0x1f;
		task_thread_info(child)->bpt_addr[nsaved++] = get_reg(child, reg_b);
	} else {
		task_thread_info(child)->bpt_addr[nsaved++] = pc + 4;
	}

	/* install breakpoints: */
	for (i = 0; i < nsaved; ++i) {
		res = read_int(child, task_thread_info(child)->bpt_addr[i],
				(int *)&insn);
		if (res < 0)
			return res;
		task_thread_info(child)->bpt_insn[i] = insn;
		res = write_int(child, task_thread_info(child)->bpt_addr[i],
				BREAKINST);
		if (res < 0)
			return res;
	}
	task_thread_info(child)->bpt_nsaved = nsaved;
	return 0;
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int
ptrace_cancel_bpt(struct task_struct *child)
{
	int i, nsaved = task_thread_info(child)->bpt_nsaved;

	task_thread_info(child)->bpt_nsaved = 0;

	if (nsaved > 2) {
		printk("%s: bogus nsaved: %d!\n", __func__, nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; ++i) {
		write_int(child, task_thread_info(child)->bpt_addr[i],
				task_thread_info(child)->bpt_insn[i]);
	}
	return (nsaved != 0);
}

void user_enable_single_step(struct task_struct *child)
{
	/* Mark single stepping.  */
	task_thread_info(child)->bpt_nsaved = -1;
}

void user_disable_single_step(struct task_struct *child)
{
	ptrace_cancel_bpt(child);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

static int gpr_get(struct task_struct *target,
			const struct user_regset *regset,
			struct membuf to)
{
	struct pt_regs *regs;
	struct user_pt_regs uregs;
	int i, ret;

	regs = task_pt_regs(target);
	for (i = 0; i < 30; i++)
		uregs.regs[i] = *(__u64 *)((void *)regs + regoffsets[i]);

	uregs.regs[30] = task_thread_info(target)->pcb.usp;
	uregs.pc = regs->pc;
	uregs.pstate = regs->ps;

	ret = membuf_write(&to, &uregs, sizeof(uregs));

	return ret;
}

static int gpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs;
	struct user_pt_regs uregs;
	int i, ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&uregs, 0, sizeof(uregs));
	if (ret)
		return ret;

	regs = task_pt_regs(target);
	for (i = 0; i < 30; i++)
		*(__u64 *)((void *)regs + regoffsets[i]) = uregs.regs[i];

	task_thread_info(target)->pcb.usp = uregs.regs[30];
	regs->pc = uregs.pc;
	regs->ps = uregs.pstate;

	return 0;
}

static int fpr_get(struct task_struct *target,
			const struct user_regset *regset,
			struct membuf to)
{

	return membuf_write(&to, &target->thread.fpstate,
			sizeof(struct user_fpsimd_state));
}

static int fpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.fpstate, 0,
				sizeof(struct user_fpsimd_state));
}

enum sw64_regset {
	REGSET_GPR,
	REGSET_FPR,
};

static const struct user_regset sw64_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = gpr_get,
		.set = gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_fpsimd_state) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.regset_get = fpr_get,
		.set = fpr_set
	},
};

static const struct user_regset_view user_sw64_view = {
	.name = "sw64", .e_machine = EM_SW64,
	.regsets = sw64_regsets, .n = ARRAY_SIZE(sw64_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sw64_view;
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	size_t copied;
	long ret;

	switch (request) {
	/* When I and D space are separate, these will need to be fixed.  */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), FOLL_FORCE);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;

		force_successful_syscall_return();
		ret = tmp;
		break;

	/* Read register number ADDR. */
	case PTRACE_PEEKUSR:
		force_successful_syscall_return();
		ret = get_reg(child, addr);
		break;

	/* When I and D space are separate, this will have to be fixed.  */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the specified register */
		ret = put_reg(child, addr, data);
		break;
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

asmlinkage unsigned long syscall_trace_enter(void)
{
	unsigned long ret = 0;
	struct pt_regs *regs = current_pt_regs();

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
		tracehook_report_syscall_entry(current_pt_regs()))
		ret = -1UL;

#ifdef CONFIG_SECCOMP
	/* Do seccomp after ptrace, to catch any tracer changes. */
	if (secure_computing() == -1)
		return -1;
#endif

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->r0);
	audit_syscall_entry(regs->r0, regs->r16, regs->r17, regs->r18, regs->r19);
	return ret ?: current_pt_regs()->r0;
}

asmlinkage void
syscall_trace_leave(void)
{
	struct pt_regs *regs = current_pt_regs();

	audit_syscall_exit(current_pt_regs());
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(current_pt_regs(), 0);
	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs_return_value(regs));
}

static long rwcsr(int rw, unsigned long csr, unsigned long value)
{
	register unsigned long __r0 __asm__("$0");
	register unsigned long __r16 __asm__("$16") = rw;
	register unsigned long __r17 __asm__("$17") = csr;
	register unsigned long __r18 __asm__("$18") = value;

	__asm__ __volatile__(
			"sys_call %4"
			: "=r"(__r0), "=r"(__r16), "=r"(__r17), "=r"(__r18)
			: "i"(HMC_rwreg), "1"(__r16), "2"(__r17), "3"(__r18)
			: "$1", "$22", "$23", "$24", "$25");

	return __r0;
}

#define RCSR 0
#define WCSR 1

#define CSR_DA_MATCH	0
#define CSR_DA_MASK	1
#define CSR_IA_MATCH	2
#define CSR_IA_MASK	3
#define CSR_IDA_MATCH	6
#define CSR_IDA_MASK	7
#define CSR_DC_CTL	11
#define CSR_DV_MATCH	15
#define CSR_DV_MASK	16

#define DV_MATCH_EN_S	19
#define DAV_MATCH_EN_S	20

int do_match(unsigned long address, unsigned long mmcsr, long cause, struct pt_regs *regs)
{
	unsigned long dc_ctl;
	unsigned long value;

	printk("%s: pid %d, name = %s,cause = %#lx, mmcsr = %#lx, address = %#lx, pc %#lx\n",
			__func__, current->pid, current->comm, cause, mmcsr, address, regs->pc);

	switch (mmcsr) {
	case MMCSR__DA_MATCH:
	case MMCSR__DV_MATCH:
	case MMCSR__DAV_MATCH:
		show_regs(regs);

		if (!(current->ptrace & PT_PTRACED)) {
			printk(" pid %d %s not be ptraced, return\n", current->pid, current->comm);
			if (mmcsr == MMCSR__DA_MATCH)
				rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			if (mmcsr == MMCSR__DV_MATCH) {
				value = rwcsr(RCSR, CSR_DV_MATCH, 0);
				printk("value is %#lx\n", value);
				value = rwcsr(RCSR, CSR_DV_MASK, 0);
				printk("value is %#lx\n", value);
				dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
				dc_ctl &= ~(0x1UL << DV_MATCH_EN_S);
				rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
			}
			if (mmcsr == MMCSR__DAV_MATCH) {
				dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
				dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
				rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
				rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			}
			task_thread_info(current)->pcb.da_match = 0;
			task_thread_info(current)->pcb.dv_match = 0;
			task_thread_info(current)->pcb.dc_ctl = 0;
			return 1;
		}

		if (mmcsr == MMCSR__DA_MATCH) {
			rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			task_thread_info(current)->pcb.da_match = 0;
		}
		if (mmcsr == MMCSR__DV_MATCH) {
			dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
			dc_ctl &= ~(0x1UL << DV_MATCH_EN_S);
			rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
		}
		if (mmcsr == MMCSR__DAV_MATCH) {
			dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
			dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
			rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
			rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
		}
		task_thread_info(current)->pcb.dv_match = 0;
		task_thread_info(current)->pcb.dc_ctl = 0;
		printk("do_page_fault: want to send SIGTRAP, pid = %d\n", current->pid);
		force_sig_fault(SIGTRAP, TRAP_HWBKPT, (void *) address, 0);
		return 1;

	case MMCSR__IA_MATCH:
		rwcsr(WCSR, CSR_IA_MATCH, 0);       //clear ia_match
		return 1;
	case MMCSR__IDA_MATCH:
		rwcsr(WCSR, CSR_IDA_MATCH, 0);       //clear ida_match
		return 1;
	}

	return 0;
}

void restore_da_match_after_sched(void)
{
	unsigned long dc_ctl_mode;
	unsigned long dc_ctl;
	struct pcb_struct *pcb = &task_thread_info(current)->pcb;

	rwcsr(WCSR, CSR_DA_MATCH, 0);
	rwcsr(WCSR, CSR_DA_MASK, pcb->da_mask);
	rwcsr(WCSR, CSR_DA_MATCH, pcb->da_match);
	dc_ctl_mode = pcb->dc_ctl;
	dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
	dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
	dc_ctl |= ((dc_ctl_mode << DV_MATCH_EN_S) & ((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S)));
	if (dc_ctl_mode & 0x1) {
		rwcsr(WCSR, CSR_DV_MATCH, pcb->dv_match);
		rwcsr(WCSR, CSR_DV_MASK, pcb->dv_mask);
		rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
	}
}

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {					\
	.name = #r,						\
	.offset = offsetof(struct pt_regs, r)			\
}

#define REG_OFFSET_END {					\
	.name = NULL,						\
	.offset = 0						\
}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(r0),
	REG_OFFSET_NAME(r1),
	REG_OFFSET_NAME(r2),
	REG_OFFSET_NAME(r3),
	REG_OFFSET_NAME(r4),
	REG_OFFSET_NAME(r5),
	REG_OFFSET_NAME(r6),
	REG_OFFSET_NAME(r7),
	REG_OFFSET_NAME(r8),
	REG_OFFSET_NAME(r9),
	REG_OFFSET_NAME(r10),
	REG_OFFSET_NAME(r11),
	REG_OFFSET_NAME(r12),
	REG_OFFSET_NAME(r13),
	REG_OFFSET_NAME(r14),
	REG_OFFSET_NAME(r15),
	REG_OFFSET_NAME(r19),
	REG_OFFSET_NAME(r20),
	REG_OFFSET_NAME(r21),
	REG_OFFSET_NAME(r22),
	REG_OFFSET_NAME(r23),
	REG_OFFSET_NAME(r24),
	REG_OFFSET_NAME(r25),
	REG_OFFSET_NAME(r26),
	REG_OFFSET_NAME(r27),
	REG_OFFSET_NAME(r28),
	REG_OFFSET_NAME(ps),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(gp),
	REG_OFFSET_NAME(r16),
	REG_OFFSET_NAME(r17),
	REG_OFFSET_NAME(r18),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:       the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

static int regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	unsigned long ksp = kernel_stack_pointer(regs);

	return (addr & ~(THREAD_SIZE - 1)) == (ksp & ~(THREAD_SIZE - 1));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:pt_regs which contains kernel stack pointer.
 * @n:stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specifined by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long addr;

	addr = kernel_stack_pointer(regs) + n * sizeof(long);
	if (!regs_within_kernel_stack(regs, addr))
		return 0;
	return *(unsigned long *)addr;
}