/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_PERF_EVENT_H
#define __ASM_PERF_EVENT_H

#include <asm/stack_pointer.h>
#include <asm/ptrace.h>

#define	ARMV8_PMU_MAX_COUNTERS	32
#define	ARMV8_PMU_COUNTER_MASK	(ARMV8_PMU_MAX_COUNTERS - 1)

/*
 * Common architectural and microarchitectural event numbers.
 */
#define ARMV8_PMUV3_PERFCTR_SW_INCR				0x00
#define ARMV8_PMUV3_PERFCTR_L1I_CACHE_REFILL			0x01
#define ARMV8_PMUV3_PERFCTR_L1I_TLB_REFILL			0x02
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_REFILL			0x03
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE				0x04
#define ARMV8_PMUV3_PERFCTR_L1D_TLB_REFILL			0x05
#define ARMV8_PMUV3_PERFCTR_LD_RETIRED				0x06
#define ARMV8_PMUV3_PERFCTR_ST_RETIRED				0x07
#define ARMV8_PMUV3_PERFCTR_INST_RETIRED			0x08
#define ARMV8_PMUV3_PERFCTR_EXC_TAKEN				0x09
#define ARMV8_PMUV3_PERFCTR_EXC_RETURN				0x0A
#define ARMV8_PMUV3_PERFCTR_CID_WRITE_RETIRED			0x0B
#define ARMV8_PMUV3_PERFCTR_PC_WRITE_RETIRED			0x0C
#define ARMV8_PMUV3_PERFCTR_BR_IMMED_RETIRED			0x0D
#define ARMV8_PMUV3_PERFCTR_BR_RETURN_RETIRED			0x0E
#define ARMV8_PMUV3_PERFCTR_UNALIGNED_LDST_RETIRED		0x0F
#define ARMV8_PMUV3_PERFCTR_BR_MIS_PRED				0x10
#define ARMV8_PMUV3_PERFCTR_CPU_CYCLES				0x11
#define ARMV8_PMUV3_PERFCTR_BR_PRED				0x12
#define ARMV8_PMUV3_PERFCTR_MEM_ACCESS				0x13
#define ARMV8_PMUV3_PERFCTR_L1I_CACHE				0x14
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_WB			0x15
#define ARMV8_PMUV3_PERFCTR_L2D_CACHE				0x16
#define ARMV8_PMUV3_PERFCTR_L2D_CACHE_REFILL			0x17
#define ARMV8_PMUV3_PERFCTR_L2D_CACHE_WB			0x18
#define ARMV8_PMUV3_PERFCTR_BUS_ACCESS				0x19
#define ARMV8_PMUV3_PERFCTR_MEMORY_ERROR			0x1A
#define ARMV8_PMUV3_PERFCTR_INST_SPEC				0x1B
#define ARMV8_PMUV3_PERFCTR_TTBR_WRITE_RETIRED			0x1C
#define ARMV8_PMUV3_PERFCTR_BUS_CYCLES				0x1D
#define ARMV8_PMUV3_PERFCTR_CHAIN				0x1E
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_ALLOCATE			0x1F
#define ARMV8_PMUV3_PERFCTR_L2D_CACHE_ALLOCATE			0x20
#define ARMV8_PMUV3_PERFCTR_BR_RETIRED				0x21
#define ARMV8_PMUV3_PERFCTR_BR_MIS_PRED_RETIRED			0x22
#define ARMV8_PMUV3_PERFCTR_STALL_FRONTEND			0x23
#define ARMV8_PMUV3_PERFCTR_STALL_BACKEND			0x24
#define ARMV8_PMUV3_PERFCTR_L1D_TLB				0x25
#define ARMV8_PMUV3_PERFCTR_L1I_TLB				0x26
#define ARMV8_PMUV3_PERFCTR_L2I_CACHE				0x27
#define ARMV8_PMUV3_PERFCTR_L2I_CACHE_REFILL			0x28
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_ALLOCATE			0x29
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_REFILL			0x2A
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE				0x2B
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_WB			0x2C
#define ARMV8_PMUV3_PERFCTR_L2D_TLB_REFILL			0x2D
#define ARMV8_PMUV3_PERFCTR_L2I_TLB_REFILL			0x2E
#define ARMV8_PMUV3_PERFCTR_L2D_TLB				0x2F
#define ARMV8_PMUV3_PERFCTR_L2I_TLB				0x30
#define ARMV8_PMUV3_PERFCTR_REMOTE_ACCESS			0x31
#define ARMV8_PMUV3_PERFCTR_LL_CACHE				0x32
#define ARMV8_PMUV3_PERFCTR_LL_CACHE_MISS			0x33
#define ARMV8_PMUV3_PERFCTR_DTLB_WALK				0x34
#define ARMV8_PMUV3_PERFCTR_ITLB_WALK				0x35
#define ARMV8_PMUV3_PERFCTR_LL_CACHE_RD				0x36
#define ARMV8_PMUV3_PERFCTR_LL_CACHE_MISS_RD			0x37
#define ARMV8_PMUV3_PERFCTR_REMOTE_ACCESS_RD			0x38
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_LMISS_RD			0x39
#define ARMV8_PMUV3_PERFCTR_OP_RETIRED				0x3A
#define ARMV8_PMUV3_PERFCTR_OP_SPEC				0x3B
#define ARMV8_PMUV3_PERFCTR_STALL				0x3C
#define ARMV8_PMUV3_PERFCTR_STALL_SLOT_BACKEND			0x3D
#define ARMV8_PMUV3_PERFCTR_STALL_SLOT_FRONTEND			0x3E
#define ARMV8_PMUV3_PERFCTR_STALL_SLOT				0x3F

/* Statistical profiling extension microarchitectural events */
#define	ARMV8_SPE_PERFCTR_SAMPLE_POP				0x4000
#define	ARMV8_SPE_PERFCTR_SAMPLE_FEED				0x4001
#define	ARMV8_SPE_PERFCTR_SAMPLE_FILTRATE			0x4002
#define	ARMV8_SPE_PERFCTR_SAMPLE_COLLISION			0x4003

/* AMUv1 architecture events */
#define	ARMV8_AMU_PERFCTR_CNT_CYCLES				0x4004
#define	ARMV8_AMU_PERFCTR_STALL_BACKEND_MEM			0x4005

/* long-latency read miss events */
#define	ARMV8_PMUV3_PERFCTR_L1I_CACHE_LMISS			0x4006
#define	ARMV8_PMUV3_PERFCTR_L2D_CACHE_LMISS_RD			0x4009
#define	ARMV8_PMUV3_PERFCTR_L2I_CACHE_LMISS			0x400A
#define	ARMV8_PMUV3_PERFCTR_L3D_CACHE_LMISS_RD			0x400B

/* additional latency from alignment events */
#define	ARMV8_PMUV3_PERFCTR_LDST_ALIGN_LAT			0x4020
#define	ARMV8_PMUV3_PERFCTR_LD_ALIGN_LAT			0x4021
#define	ARMV8_PMUV3_PERFCTR_ST_ALIGN_LAT			0x4022

/* Armv8.5 Memory Tagging Extension events */
#define	ARMV8_MTE_PERFCTR_MEM_ACCESS_CHECKED			0x4024
#define	ARMV8_MTE_PERFCTR_MEM_ACCESS_CHECKED_RD			0x4025
#define	ARMV8_MTE_PERFCTR_MEM_ACCESS_CHECKED_WR			0x4026

/* ARMv8 recommended implementation defined event types */
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_RD			0x40
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_WR			0x41
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_REFILL_RD		0x42
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_REFILL_WR		0x43
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_REFILL_INNER		0x44
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_REFILL_OUTER		0x45
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_WB_VICTIM		0x46
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_WB_CLEAN			0x47
#define ARMV8_IMPDEF_PERFCTR_L1D_CACHE_INVAL			0x48

#define ARMV8_IMPDEF_PERFCTR_L1D_TLB_REFILL_RD			0x4C
#define ARMV8_IMPDEF_PERFCTR_L1D_TLB_REFILL_WR			0x4D
#define ARMV8_IMPDEF_PERFCTR_L1D_TLB_RD				0x4E
#define ARMV8_IMPDEF_PERFCTR_L1D_TLB_WR				0x4F
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_RD			0x50
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_WR			0x51
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_REFILL_RD		0x52
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_REFILL_WR		0x53

#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_WB_VICTIM		0x56
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_WB_CLEAN			0x57
#define ARMV8_IMPDEF_PERFCTR_L2D_CACHE_INVAL			0x58

#define ARMV8_IMPDEF_PERFCTR_L2D_TLB_REFILL_RD			0x5C
#define ARMV8_IMPDEF_PERFCTR_L2D_TLB_REFILL_WR			0x5D
#define ARMV8_IMPDEF_PERFCTR_L2D_TLB_RD				0x5E
#define ARMV8_IMPDEF_PERFCTR_L2D_TLB_WR				0x5F
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_RD			0x60
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_WR			0x61
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_SHARED			0x62
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_NOT_SHARED		0x63
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_NORMAL			0x64
#define ARMV8_IMPDEF_PERFCTR_BUS_ACCESS_PERIPH			0x65
#define ARMV8_IMPDEF_PERFCTR_MEM_ACCESS_RD			0x66
#define ARMV8_IMPDEF_PERFCTR_MEM_ACCESS_WR			0x67
#define ARMV8_IMPDEF_PERFCTR_UNALIGNED_LD_SPEC			0x68
#define ARMV8_IMPDEF_PERFCTR_UNALIGNED_ST_SPEC			0x69
#define ARMV8_IMPDEF_PERFCTR_UNALIGNED_LDST_SPEC		0x6A

#define ARMV8_IMPDEF_PERFCTR_LDREX_SPEC				0x6C
#define ARMV8_IMPDEF_PERFCTR_STREX_PASS_SPEC			0x6D
#define ARMV8_IMPDEF_PERFCTR_STREX_FAIL_SPEC			0x6E
#define ARMV8_IMPDEF_PERFCTR_STREX_SPEC				0x6F
#define ARMV8_IMPDEF_PERFCTR_LD_SPEC				0x70
#define ARMV8_IMPDEF_PERFCTR_ST_SPEC				0x71
#define ARMV8_IMPDEF_PERFCTR_LDST_SPEC				0x72
#define ARMV8_IMPDEF_PERFCTR_DP_SPEC				0x73
#define ARMV8_IMPDEF_PERFCTR_ASE_SPEC				0x74
#define ARMV8_IMPDEF_PERFCTR_VFP_SPEC				0x75
#define ARMV8_IMPDEF_PERFCTR_PC_WRITE_SPEC			0x76
#define ARMV8_IMPDEF_PERFCTR_CRYPTO_SPEC			0x77
#define ARMV8_IMPDEF_PERFCTR_BR_IMMED_SPEC			0x78
#define ARMV8_IMPDEF_PERFCTR_BR_RETURN_SPEC			0x79
#define ARMV8_IMPDEF_PERFCTR_BR_INDIRECT_SPEC			0x7A

#define ARMV8_IMPDEF_PERFCTR_ISB_SPEC				0x7C
#define ARMV8_IMPDEF_PERFCTR_DSB_SPEC				0x7D
#define ARMV8_IMPDEF_PERFCTR_DMB_SPEC				0x7E

#define ARMV8_IMPDEF_PERFCTR_EXC_UNDEF				0x81
#define ARMV8_IMPDEF_PERFCTR_EXC_SVC				0x82
#define ARMV8_IMPDEF_PERFCTR_EXC_PABORT				0x83
#define ARMV8_IMPDEF_PERFCTR_EXC_DABORT				0x84

#define ARMV8_IMPDEF_PERFCTR_EXC_IRQ				0x86
#define ARMV8_IMPDEF_PERFCTR_EXC_FIQ				0x87
#define ARMV8_IMPDEF_PERFCTR_EXC_SMC				0x88

#define ARMV8_IMPDEF_PERFCTR_EXC_HVC				0x8A
#define ARMV8_IMPDEF_PERFCTR_EXC_TRAP_PABORT			0x8B
#define ARMV8_IMPDEF_PERFCTR_EXC_TRAP_DABORT			0x8C
#define ARMV8_IMPDEF_PERFCTR_EXC_TRAP_OTHER			0x8D
#define ARMV8_IMPDEF_PERFCTR_EXC_TRAP_IRQ			0x8E
#define ARMV8_IMPDEF_PERFCTR_EXC_TRAP_FIQ			0x8F
#define ARMV8_IMPDEF_PERFCTR_RC_LD_SPEC				0x90
#define ARMV8_IMPDEF_PERFCTR_RC_ST_SPEC				0x91

#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_RD			0xA0
#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_WR			0xA1
#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_REFILL_RD		0xA2
#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_REFILL_WR		0xA3

#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_WB_VICTIM		0xA6
#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_WB_CLEAN			0xA7
#define ARMV8_IMPDEF_PERFCTR_L3D_CACHE_INVAL			0xA8

/*
 * Per-CPU PMCR: config reg
 */
#define ARMV8_PMU_PMCR_E	(1 << 0) /* Enable all counters */
#define ARMV8_PMU_PMCR_P	(1 << 1) /* Reset all counters */
#define ARMV8_PMU_PMCR_C	(1 << 2) /* Cycle counter reset */
#define ARMV8_PMU_PMCR_D	(1 << 3) /* CCNT counts every 64th cpu cycle */
#define ARMV8_PMU_PMCR_X	(1 << 4) /* Export to ETM */
#define ARMV8_PMU_PMCR_DP	(1 << 5) /* Disable CCNT if non-invasive debug*/
#define ARMV8_PMU_PMCR_LC	(1 << 6) /* Overflow on 64 bit cycle counter */
#define ARMV8_PMU_PMCR_LP	(1 << 7) /* Long event counter enable */
#define	ARMV8_PMU_PMCR_N_SHIFT	11	 /* Number of counters supported */
#define	ARMV8_PMU_PMCR_N_MASK	0x1f
#define	ARMV8_PMU_PMCR_MASK	0xff	 /* Mask for writable bits */

/*
 * PMOVSR: counters overflow flag status reg
 */
#define	ARMV8_PMU_OVSR_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV8_PMU_OVERFLOWED_MASK	ARMV8_PMU_OVSR_MASK

/*
 * PMXEVTYPER: Event selection reg
 */
#define	ARMV8_PMU_EVTYPE_MASK	0xc800ffff	/* Mask for writable bits */
#define	ARMV8_PMU_EVTYPE_EVENT	0xffff		/* Mask for EVENT bits */

/*
 * Event filters for PMUv3
 */
#define	ARMV8_PMU_EXCLUDE_EL1	(1U << 31)
#define	ARMV8_PMU_EXCLUDE_EL0	(1U << 30)
#define	ARMV8_PMU_INCLUDE_EL2	(1U << 27)

/*
 * PMUSERENR: user enable reg
 */
#define ARMV8_PMU_USERENR_MASK	0xf		/* Mask for writable bits */
#define ARMV8_PMU_USERENR_EN	(1 << 0) /* PMU regs can be accessed at EL0 */
#define ARMV8_PMU_USERENR_SW	(1 << 1) /* PMSWINC can be written at EL0 */
#define ARMV8_PMU_USERENR_CR	(1 << 2) /* Cycle counter can be read at EL0 */
#define ARMV8_PMU_USERENR_ER	(1 << 3) /* Event counter can be read at EL0 */

/* PMMIR_EL1.SLOTS mask */
#define ARMV8_PMU_SLOTS_MASK	0xff

struct pmu_hw_events;
struct arm_pmu;
struct perf_event;

#ifdef CONFIG_PERF_EVENTS
struct pt_regs;
extern unsigned long perf_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_misc_flags(struct pt_regs *regs);
#define perf_misc_flags(regs)	perf_misc_flags(regs)
#define perf_arch_bpf_user_pt_regs(regs) &regs->user_regs

#ifdef CONFIG_ARM64_BRBE
void armv8pmu_branch_reset(void);
void armv8pmu_branch_probe(struct arm_pmu *arm_pmu);
bool armv8pmu_branch_attr_valid(struct perf_event *event);
void armv8pmu_branch_enable(struct perf_event *event);
void armv8pmu_branch_disable(struct perf_event *event);
void armv8pmu_branch_read(struct pmu_hw_events *cpuc,
			  struct perf_event *event);
void armv8pmu_branch_save(struct arm_pmu *arm_pmu, void *ctx);
int armv8pmu_task_ctx_cache_alloc(struct arm_pmu *arm_pmu);
void armv8pmu_task_ctx_cache_free(struct arm_pmu *arm_pmu);
#else  /* !CONFIG_ARM64_BRBE */
static inline void armv8pmu_branch_reset(void)
{
}

static inline void armv8pmu_branch_probe(struct arm_pmu *arm_pmu)
{
}

static inline bool armv8pmu_branch_attr_valid(struct perf_event *event)
{
	return false;
}

static inline void armv8pmu_branch_enable(struct perf_event *event)
{
}

static inline void armv8pmu_branch_disable(struct perf_event *event)
{
}

static inline void armv8pmu_branch_read(struct pmu_hw_events *cpuc,
					struct perf_event *event)
{
}

static inline void armv8pmu_branch_save(struct arm_pmu *arm_pmu, void *ctx)
{
}

static inline int armv8pmu_task_ctx_cache_alloc(struct arm_pmu *arm_pmu)
{
	return 0;
}

static inline void armv8pmu_task_ctx_cache_free(struct arm_pmu *arm_pmu)
{
}
#endif /* CONFIG_ARM64_BRBE */
#endif

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->pc = (__ip);    \
	(regs)->regs[29] = (unsigned long) __builtin_frame_address(0); \
	(regs)->sp = current_stack_pointer; \
	(regs)->pstate = PSR_MODE_EL1h;	\
}

#endif
