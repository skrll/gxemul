/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: cpu_arm_instr.c,v 1.45 2005-11-05 23:04:28 debug Exp $
 *
 *  ARM instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


#include "arm_quick_pc_to_pointers.h"

/*  #define GATHER_BDT_STATISTICS  */


#ifdef GATHER_BDT_STATISTICS
/*
 *  update_bdt_statistics():
 *
 *  Gathers statistics about load/store multiple instructions.
 *
 *  NOTE/TODO: Perhaps it would be more memory efficient to swap the high
 *  and low parts of the instruction word, so that the lllllll bits become
 *  the high bits; this would cause fewer host pages to be used. Anyway, the
 *  current implementation works on hosts with lots of RAM.
 *
 *  The resulting file, bdt_statistics.txt, should then be processed like
 *  this to give a new cpu_arm_multi.txt:
 *
 *  uniq -c bdt_statistics.txt|sort -nr|head -256|cut -f 2 > cpu_arm_multi.txt
 */
static void update_bdt_statistics(uint32_t iw)
{
	static FILE *f = NULL;
	static long long *counts;
	static char *counts_used;
	static long long n = 0;

	if (f == NULL) {
		size_t s = (1 << 24) * sizeof(long long);
		f = fopen("bdt_statistics.txt", "w");
		if (f == NULL) {
			fprintf(stderr, "update_bdt_statistics(): :-(\n");
			exit(1);
		}
		counts = zeroed_alloc(s);
		counts_used = zeroed_alloc(65536);
	}

	/*  Drop the s-bit: xxxx100P USWLnnnn llllllll llllllll  */
	iw = ((iw & 0x01800000) >> 1) | (iw & 0x003fffff);

	counts_used[iw & 0xffff] = 1;
	counts[iw] ++;

	n ++;
	if ((n % 500000) == 0) {
		int i;
		long long j;
		fatal("[ update_bdt_statistics(): n = %lli ]\n", (long long) n);
		fseek(f, 0, SEEK_SET);
		for (i=0; i<0x1000000; i++)
			if (counts_used[i & 0xffff] && counts[i] != 0) {
				/*  Recreate the opcode:  */
				uint32_t opcode = ((i & 0x00c00000) << 1)
				    | (i & 0x003fffff) | 0x08000000;
				for (j=0; j<counts[i]; j++)
					fprintf(f, "0x%08x\n", opcode);
			}
		fflush(f);
	}
}
#endif


/*****************************************************************************/


/*
 *  Helper definitions:
 *
 *  Each instruction is defined like this:
 *
 *	X(foo)
 *	{
 *		code for foo;
 *	}
 *	Y(foo)
 *
 *  The Y macro defines 14 copies of the instruction, one for each possible
 *  condition code. (The NV condition code is not included, and the AL code
 *  uses the main foo function.)  Y also defines an array with pointers to
 *  all of these functions.
 *
 *  If the compiler is good enough (i.e. allows long enough code sequences
 *  to be inlined), then the Y functions will be compiled as full (inlined)
 *  functions, otherwise they will simply call the X function.
 */

#define Y(n) void arm_instr_ ## n ## __eq(struct cpu *cpu,		\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ne(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __mi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_N)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __pl(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_N))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_V)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_V))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __hi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C &&				\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ls(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z ||				\
		!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ge(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __lt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __gt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&		\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __le(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) ||		\
		(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void (*arm_cond_instr_ ## n  [16])(struct cpu *,		\
			struct arm_instr_call *) = {			\
		arm_instr_ ## n ## __eq, arm_instr_ ## n ## __ne,	\
		arm_instr_ ## n ## __cs, arm_instr_ ## n ## __cc,	\
		arm_instr_ ## n ## __mi, arm_instr_ ## n ## __pl,	\
		arm_instr_ ## n ## __vs, arm_instr_ ## n ## __vc,	\
		arm_instr_ ## n ## __hi, arm_instr_ ## n ## __ls,	\
		arm_instr_ ## n ## __ge, arm_instr_ ## n ## __lt,	\
		arm_instr_ ## n ## __gt, arm_instr_ ## n ## __le,	\
		arm_instr_ ## n , arm_instr_nop };

#define cond_instr(n)	( arm_cond_instr_ ## n  [condition_code] )


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 *  invalid:  Invalid instructions end up here.
 */
X(nop) { }
X(invalid) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	fatal("Invalid ARM instruction: pc=0x%08x\n", (int)cpu->pc);

	cpu->running = 0;
	cpu->running_translated = 0;
	cpu->n_translated_instrs --;
	cpu->cd.arm.next_ic = &nothing_call;
}


/*
 *  b:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset
 */
X(b)
{
	uint32_t pc = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[1];
	cpu->pc = cpu->cd.arm.r[ARM_PC] = pc + (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(b)


/*
 *  b_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new arm_instr_call
 */
X(b_samepage)
{
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(b_samepage)


/*
 *  bx:  Branch, potentially exchanging Thumb/ARM encoding
 *
 *  arg[0] = ptr to rm
 */
X(bx)
{
	cpu->pc = cpu->cd.arm.r[ARM_PC] = reg(ic->arg[0]);
	if (cpu->pc & 1) {
		fatal("thumb: TODO\n");
		exit(1);
	}
	cpu->pc &= ~3;

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(bx)


/*
 *  bx_trace:  As bx, but with trace enabled, arg[0] = the link register.
 *
 *  arg[0] = ignored
 */
X(bx_trace)
{
	cpu->pc = cpu->cd.arm.r[ARM_PC] = cpu->cd.arm.r[ARM_LR];
	if (cpu->pc & 1) {
		fatal("thumb: TODO\n");
		exit(1);
	}
	cpu->pc &= ~3;

	cpu_functioncall_trace_return(cpu);

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(bx_trace)


/*
 *  bl:  Branch and Link (to a different translated page)
 *
 *  arg[0] = relative address
 */
X(bl)
{
	uint32_t pc = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[1];
	cpu->cd.arm.r[ARM_LR] = pc + 4;

	/*  Calculate new PC from this instruction + arg[0]  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = pc + (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(bl)


/*
 *  blx:  Branch and Link, potentially exchanging Thumb/ARM encoding
 *
 *  arg[0] = ptr to rm
 */
X(blx)
{
	uint32_t lr = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[2];
	cpu->cd.arm.r[ARM_LR] = lr;
	cpu->pc = cpu->cd.arm.r[ARM_PC] = reg(ic->arg[0]);
	if (cpu->pc & 1) {
		fatal("thumb: TODO\n");
		exit(1);
	}
	cpu->pc &= ~3;

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(blx)


/*
 *  bl_trace:  Branch and Link (to a different translated page), with trace
 *
 *  Same as for bl.
 */
X(bl_trace)
{
	uint32_t pc = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[1];
	cpu->cd.arm.r[ARM_LR] = pc + 4;

	/*  Calculate new PC from this instruction + arg[0]  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = pc + (int32_t)ic->arg[0];

	cpu_functioncall_trace(cpu, cpu->pc);

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}
Y(bl_trace)


/*
 *  bl_samepage:  A branch + link within the same page
 *
 *  arg[0] = pointer to new arm_instr_call
 */
X(bl_samepage)
{
	uint32_t lr = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[2];
	cpu->cd.arm.r[ARM_LR] = lr;
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];
}
Y(bl_samepage)


/*
 *  bl_samepage_trace:  Branch and Link (to the same page), with trace
 *
 *  Same as for bl_samepage.
 */
X(bl_samepage_trace)
{
	uint32_t lr = (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[2];

	/*  Link and branch:  */
	cpu->cd.arm.r[ARM_LR] = lr;
	cpu->cd.arm.next_ic = (struct arm_instr_call *) ic->arg[0];

	/*  ... and show trace:  */
	cpu_functioncall_trace(cpu, lr - 4);
}
Y(bl_samepage_trace)


/*
 *  clz: Count leading zeroes.
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rd
 */
X(clz)
{
	uint32_t rm = reg(ic->arg[0]);
	int i = 32, n = 0, j;
	while (i>0) {
		if (rm & 0xff000000) {
			for (j=0; j<8; j++) {
				if (rm & 0x80000000)
					break;
				n ++;
				rm <<= 1;
			}
			break;
		} else {
			rm <<= 8;
			i -= 8;
			n += 8;
		}
	}
	reg(ic->arg[1]) = n;
}
Y(clz)


/*
 *  mul: Multiplication
 *
 *  arg[0] = ptr to rd
 *  arg[1] = ptr to rm
 *  arg[2] = ptr to rs
 */
X(mul)
{
	reg(ic->arg[0]) = reg(ic->arg[1]) * reg(ic->arg[2]);
}
Y(mul)
X(muls)
{
	uint32_t result;
	result = reg(ic->arg[1]) * reg(ic->arg[2]);
	cpu->cd.arm.cpsr &= ~(ARM_FLAG_Z | ARM_FLAG_N);
	if (result == 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;
	if (result & 0x80000000)
		cpu->cd.arm.cpsr |= ARM_FLAG_N;
	reg(ic->arg[0]) = result;
}
Y(muls)


/*
 *  mla: Multiplication with addition
 *
 *  arg[0] = copy of instruction word
 */
X(mla)
{
	/*  xxxx0000 00ASdddd nnnnssss 1001mmmm (Rd,Rm,Rs[,Rn])  */
	uint32_t iw = ic->arg[0];
	int rd, rs, rn, rm;
	rd = (iw >> 16) & 15; rn = (iw >> 12) & 15,
	rs = (iw >> 8) & 15;  rm = iw & 15;
	cpu->cd.arm.r[rd] = cpu->cd.arm.r[rm] * cpu->cd.arm.r[rs]
	    + cpu->cd.arm.r[rn];
}
Y(mla)
X(mlas)
{
	/*  xxxx0000 00ASdddd nnnnssss 1001mmmm (Rd,Rm,Rs[,Rn])  */
	uint32_t iw = ic->arg[0];
	int rd, rs, rn, rm;
	rd = (iw >> 16) & 15; rn = (iw >> 12) & 15,
	rs = (iw >> 8) & 15;  rm = iw & 15;
	cpu->cd.arm.r[rd] = cpu->cd.arm.r[rm] * cpu->cd.arm.r[rs]
	    + cpu->cd.arm.r[rn];
	cpu->cd.arm.cpsr &= ~(ARM_FLAG_Z | ARM_FLAG_N);
	if (cpu->cd.arm.r[rd] == 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;
	if (cpu->cd.arm.r[rd] & 0x80000000)
		cpu->cd.arm.cpsr |= ARM_FLAG_N;
}
Y(mlas)


/*
 *  mull: Long multiplication
 *
 *  arg[0] = copy of instruction word
 */
X(mull)
{
	/*  xxxx0000 1UAShhhh llllssss 1001mmmm  */
	uint32_t iw; uint64_t tmp; int u_bit, a_bit;
	iw = ic->arg[0];
	u_bit = iw & 0x00400000; a_bit = iw & 0x00200000;
	tmp = cpu->cd.arm.r[iw & 15];
	if (u_bit)
		tmp = (int64_t)(int32_t)tmp
		    * (int64_t)(int32_t)cpu->cd.arm.r[(iw >> 8) & 15];
	else
		tmp *= (uint64_t)cpu->cd.arm.r[(iw >> 8) & 15];
	if (a_bit) {
		uint64_t x = ((uint64_t)cpu->cd.arm.r[(iw >> 16) & 15] << 32)
		    | cpu->cd.arm.r[(iw >> 12) & 15];
		x += tmp;
		cpu->cd.arm.r[(iw >> 16) & 15] = (x >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] = x;
	} else {
		cpu->cd.arm.r[(iw >> 16) & 15] = (tmp >> 32);
		cpu->cd.arm.r[(iw >> 12) & 15] = tmp;
	}
}
Y(mull)


/*
 *  mov_reg_reg:  Move a register to another.
 *
 *  arg[0] = ptr to source register
 *  arg[1] = ptr to destination register
 */
X(mov_reg_reg)
{
	reg(ic->arg[1]) = reg(ic->arg[0]);
}
Y(mov_reg_reg)


/*
 *  ret_trace:  "mov pc,lr" with trace enabled
 *  ret:  "mov pc,lr" without trace enabled
 *
 *  arg[0] = ignored
 */
X(ret_trace)
{
	uint32_t old_pc, mask_within_page;
	old_pc = cpu->cd.arm.r[ARM_PC];
	mask_within_page = ((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT) |
	    ((1 << ARM_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Update the PC register:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] = cpu->cd.arm.r[ARM_LR];

	cpu_functioncall_trace_return(cpu);

	/*
	 *  Is this a return to code within the same page? Then there is no
	 *  need to update all pointers, just next_ic.
	 */
	if ((old_pc & ~mask_within_page) == (cpu->pc & ~mask_within_page)) {
		cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +
		    ((cpu->pc & mask_within_page) >> ARM_INSTR_ALIGNMENT_SHIFT);
	} else {
		/*  Find the new physical page and update pointers:  */
		quick_pc_to_pointers(cpu);
	}
}
Y(ret_trace)
X(ret)
{
	cpu->pc = cpu->cd.arm.r[ARM_PC] = cpu->cd.arm.r[ARM_LR];
	quick_pc_to_pointers(cpu);
}
Y(ret)


/*
 *  msr: Move to status register from a normal register or immediate value.
 *
 *  arg[0] = immediate value
 *  arg[1] = mask
 *  arg[2] = pointer to rm
 *
 *  msr_imm and msr_imm_spsr use arg[1] and arg[0].
 *  msr and msr_spsr use arg[1] and arg[2].
 */
X(msr_imm)
{
	uint32_t mask = ic->arg[1];
	int switch_register_banks = (mask & ARM_FLAG_MODE) &&
	    ((cpu->cd.arm.cpsr & ARM_FLAG_MODE) !=
	    (ic->arg[0] & ARM_FLAG_MODE));
	uint32_t new_value = ic->arg[0];

	if (switch_register_banks)
		arm_save_register_bank(cpu);

	cpu->cd.arm.cpsr &= ~mask;
	cpu->cd.arm.cpsr |= (new_value & mask);

	if (switch_register_banks)
		arm_load_register_bank(cpu);
}
Y(msr_imm)
X(msr)
{
	ic->arg[0] = reg(ic->arg[2]);
	instr(msr_imm)(cpu, ic);
}
Y(msr)
X(msr_imm_spsr)
{
	uint32_t mask = ic->arg[1];
	uint32_t new_value = ic->arg[0];
	switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
	case ARM_MODE_FIQ32:
		cpu->cd.arm.spsr_fiq &= ~mask;
		cpu->cd.arm.spsr_fiq |= (new_value & mask);
		break;
	case ARM_MODE_ABT32:
		cpu->cd.arm.spsr_abt &= ~mask;
		cpu->cd.arm.spsr_abt |= (new_value & mask);
		break;
	case ARM_MODE_UND32:
		cpu->cd.arm.spsr_und &= ~mask;
		cpu->cd.arm.spsr_und |= (new_value & mask);
		break;
	case ARM_MODE_IRQ32:
		cpu->cd.arm.spsr_irq &= ~mask;
		cpu->cd.arm.spsr_irq |= (new_value & mask);
		break;
	case ARM_MODE_SVC32:
		cpu->cd.arm.spsr_svc &= ~mask;
		cpu->cd.arm.spsr_svc |= (new_value & mask);
		break;
	default:fatal("msr_spsr: unimplemented mode %i\n",
		    cpu->cd.arm.cpsr & ARM_FLAG_MODE);
{
	/*  Synchronize the program counter:  */
	uint32_t old_pc, low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	old_pc = cpu->pc = cpu->cd.arm.r[ARM_PC];
printf("msr_spsr: old pc = 0x%08x\n", old_pc);
}
		exit(1);
	}
}
Y(msr_imm_spsr)
X(msr_spsr)
{
	ic->arg[0] = reg(ic->arg[2]);
	instr(msr_imm_spsr)(cpu, ic);
}
Y(msr_spsr)


/*
 *  mrs: Move from status/flag register to a normal register.
 *
 *  arg[0] = pointer to rd
 */
X(mrs)
{
	reg(ic->arg[0]) = cpu->cd.arm.cpsr;
}
Y(mrs)


/*
 *  mrs: Move from status/flag register to a normal register.
 *
 *  arg[0] = pointer to rd
 */
X(mrs_spsr)
{
	switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
	case ARM_MODE_FIQ32: reg(ic->arg[0]) = cpu->cd.arm.spsr_fiq; break;
	case ARM_MODE_ABT32: reg(ic->arg[0]) = cpu->cd.arm.spsr_abt; break;
	case ARM_MODE_UND32: reg(ic->arg[0]) = cpu->cd.arm.spsr_und; break;
	case ARM_MODE_IRQ32: reg(ic->arg[0]) = cpu->cd.arm.spsr_irq; break;
	case ARM_MODE_SVC32: reg(ic->arg[0]) = cpu->cd.arm.spsr_svc; break;
	case ARM_MODE_USR32:
	case ARM_MODE_SYS32: reg(ic->arg[0]) = 0; break;
	default:fatal("mrs_spsr: unimplemented mode %i\n",
		    cpu->cd.arm.cpsr & ARM_FLAG_MODE);
		exit(1);
	}
}
Y(mrs_spsr)


/*
 *  mcr_mrc:  Coprocessor move
 *  cdp:      Coprocessor operation
 *
 *  arg[0] = copy of the instruction word
 */
X(mcr_mrc) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];
	arm_mcr_mrc(cpu, ic->arg[0]);
}
Y(mcr_mrc)
X(cdp) {
	uint32_t low_pc;
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];
	arm_cdp(cpu, ic->arg[0]);
}
Y(cdp)


/*
 *  openfirmware:
 */
X(openfirmware)
{
	of_emul(cpu);
	cpu->pc = cpu->cd.arm.r[ARM_PC] = cpu->cd.arm.r[ARM_LR];
	if (cpu->machine->show_trace_tree)
		cpu_functioncall_trace_return(cpu);
	quick_pc_to_pointers(cpu);
}


/*
 *  swi_useremul: Syscall.
 *
 *  arg[0] = swi number
 */
X(swi_useremul)
{
	/*  Synchronize the program counter:  */
	uint32_t old_pc, low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	old_pc = cpu->pc = cpu->cd.arm.r[ARM_PC];

	useremul_syscall(cpu, ic->arg[0]);

	if (!cpu->running) {
		cpu->running_translated = 0;
		cpu->n_translated_instrs --;
		cpu->cd.arm.next_ic = &nothing_call;
	} else if (cpu->pc != old_pc) {
		/*  PC was changed by the SWI call. Find the new physical
		    page and update the translation pointers:  */
		quick_pc_to_pointers(cpu);
	}
}
Y(swi_useremul)


/*
 *  swi:  Software interrupt.
 */
X(swi)
{
	/*  Synchronize the program counter first:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] =
	    (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[0];
	arm_exception(cpu, ARM_EXCEPTION_SWI);
}
Y(swi)


/*
 *  und:  Undefined instruction.
 */
X(und)
{
	/*  Synchronize the program counter first:  */
	cpu->pc = cpu->cd.arm.r[ARM_PC] =
	    (cpu->cd.arm.r[ARM_PC] & 0xfffff000) + ic->arg[0];
	arm_exception(cpu, ARM_EXCEPTION_UND);
}
Y(und)


/*
 *  swp, swpb:  Swap (word or byte).
 *
 *  arg[0] = ptr to rd
 *  arg[1] = ptr to rm
 *  arg[2] = ptr to rn
 */
X(swp)
{
	uint32_t addr = reg(ic->arg[2]), data, data2;
	unsigned char d[4];
	/*  Synchronize the program counter:  */
	uint32_t low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	if (!cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d), MEM_READ,
	    CACHE_DATA)) {
		fatal("swp: load failed\n");
		return;
	}
	data = d[0] + (d[1] << 8) + (d[2] << 16) + (d[3] << 24);
	data2 = reg(ic->arg[1]);
	d[0] = data2; d[1] = data2 >> 8; d[2] = data2 >> 16; d[3] = data2 >> 24;
	if (!cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d), MEM_WRITE,
	    CACHE_DATA)) {
		fatal("swp: store failed\n");
		return;
	}
	reg(ic->arg[0]) = data;
}
Y(swp)
X(swpb)
{
	uint32_t addr = reg(ic->arg[2]), data;
	unsigned char d[1];
	/*  Synchronize the program counter:  */
	uint32_t low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	if (!cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d), MEM_READ,
	    CACHE_DATA)) {
		fatal("swp: load failed\n");
		return;
	}
	data = d[0];
	d[0] = reg(ic->arg[1]);
	if (!cpu->memory_rw(cpu, cpu->mem, addr, d, sizeof(d), MEM_WRITE,
	    CACHE_DATA)) {
		fatal("swp: store failed\n");
		return;
	}
	reg(ic->arg[0]) = data;
}
Y(swpb)


extern void (*arm_load_store_instr[1024])(struct cpu *,
	struct arm_instr_call *);
X(store_w1_word_u1_p0_imm);
X(store_w0_byte_u1_p0_imm);
X(store_w0_word_u1_p0_imm);
X(load_w1_word_u1_p0_imm);
X(load_w0_word_u1_p0_imm);
X(load_w0_byte_u1_p1_imm);
X(load_w0_byte_u1_p1_reg);

extern void (*arm_load_store_instr_pc[1024])(struct cpu *,
	struct arm_instr_call *);

extern void (*arm_load_store_instr_3[2048])(struct cpu *,
	struct arm_instr_call *);

extern void (*arm_load_store_instr_3_pc[2048])(struct cpu *,
	struct arm_instr_call *);

extern uint32_t (*arm_r[8192])(struct cpu *, struct arm_instr_call *);
extern void arm_r_r3_t0_c0(void);

extern void (*arm_dpi_instr[2 * 2 * 2 * 16 * 16])(struct cpu *,
	struct arm_instr_call *);
X(cmps);
X(sub);
X(add);
X(subs);


#include "cpu_arm_instr_misc.c"


/*
 *  bdt_load:  Block Data Transfer, Load
 *
 *  arg[0] = pointer to uint32_t in host memory, pointing to the base register
 *  arg[1] = 32-bit instruction word. Most bits are read from this.
 */
X(bdt_load)
{
	unsigned char data[4];
	uint32_t *np = (uint32_t *)ic->arg[0];
	uint32_t addr = *np, low_pc;
	unsigned char *page;
	uint32_t iw = ic->arg[1];  /*  xxxx100P USWLnnnn llllllll llllllll  */
	int p_bit = iw & 0x01000000;
	int u_bit = iw & 0x00800000;
	int s_bit = iw & 0x00400000;
	int w_bit = iw & 0x00200000;
	int i, return_flag = 0;
	uint32_t new_values[16];

#ifdef GATHER_BDT_STATISTICS
	if (!s_bit)
		update_bdt_statistics(iw);
#endif

	/*  Synchronize the program counter:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	if (s_bit) {
		/*  Load to USR registers:  */
		if ((cpu->cd.arm.cpsr & ARM_FLAG_MODE) == ARM_MODE_USR32) {
			fatal("[ bdt_load: s-bit: in usermode? ]\n");
			s_bit = 0;
		}
		if (iw & 0x8000) {
			s_bit = 0;
			return_flag = 1;
		}
	}

	for (i=(u_bit? 0 : 15); i>=0 && i<=15; i+=(u_bit? 1 : -1)) {
		uint32_t value;

		if (!((iw >> i) & 1)) {
			/*  Skip register i:  */
			continue;
		}

		if (p_bit) {
			if (u_bit)
				addr += sizeof(uint32_t);
			else
				addr -= sizeof(uint32_t);
		}

		page = cpu->cd.arm.host_load[addr >> 12];
		if (page != NULL) {
			uint32_t *p32 = (uint32_t *) page;
			value = p32[(addr & 0xfff) >> 2];
			/*  Change byte order of value if
			    host and emulated endianness differ:  */
#ifdef HOST_LITTLE_ENDIAN
			if (cpu->byte_order == EMUL_BIG_ENDIAN)
#else
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
#endif
				value = ((value & 0xff) << 24) |
				    ((value & 0xff00) << 8) |
				    ((value & 0xff0000) >> 8) |
				    ((value & 0xff000000) >> 24);
		} else {
			if (!cpu->memory_rw(cpu, cpu->mem, addr, data,
			    sizeof(data), MEM_READ, CACHE_DATA)) {
				/*  load failed  */
				return;
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				value = data[0] +
				    (data[1] << 8) + (data[2] << 16)
				    + (data[3] << 24);
			} else {
				value = data[3] +
				    (data[2] << 8) + (data[1] << 16)
				    + (data[0] << 24);
			}
		}

		new_values[i] = value;

		if (!p_bit) {
			if (u_bit)
				addr += sizeof(uint32_t);
			else
				addr -= sizeof(uint32_t);
		}
	}

	for (i=(u_bit? 0 : 15); i>=0 && i<=15; i+=(u_bit? 1 : -1)) {
		if (!((iw >> i) & 1)) {
			/*  Skip register i:  */
			continue;
		}

		if (!s_bit) {
			cpu->cd.arm.r[i] = new_values[i];
		} else {
			switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
			case ARM_MODE_USR32:
			case ARM_MODE_SYS32:
				cpu->cd.arm.r[i] = new_values[i];
				break;
			case ARM_MODE_FIQ32:
				if (i >= 8 && i <= 14)
					cpu->cd.arm.default_r8_r14[i-8] =
					    new_values[i];
				else
					cpu->cd.arm.r[i] = new_values[i];
				break;
			case ARM_MODE_SVC32:
			case ARM_MODE_ABT32:
			case ARM_MODE_UND32:
			case ARM_MODE_IRQ32:
				if (i >= 13 && i <= 14)
					cpu->cd.arm.default_r8_r14[i-8] =
					    new_values[i];
				else
					cpu->cd.arm.r[i] = new_values[i];
				break;
			}
		}
	}

	if (w_bit)
		*np = addr;

	if (return_flag) {
		uint32_t new_cpsr;
		int switch_register_banks;

		switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
		case ARM_MODE_FIQ32:
			new_cpsr = cpu->cd.arm.spsr_fiq; break;
		case ARM_MODE_ABT32:
			new_cpsr = cpu->cd.arm.spsr_abt; break;
		case ARM_MODE_UND32:
			new_cpsr = cpu->cd.arm.spsr_und; break;
		case ARM_MODE_IRQ32:
			new_cpsr = cpu->cd.arm.spsr_irq; break;
		case ARM_MODE_SVC32:
			new_cpsr = cpu->cd.arm.spsr_svc; break;
		default:fatal("bdt_load: unimplemented mode %i\n",
			    cpu->cd.arm.cpsr & ARM_FLAG_MODE);
			exit(1);
		}

		switch_register_banks = (cpu->cd.arm.cpsr & ARM_FLAG_MODE) !=
		    (new_cpsr & ARM_FLAG_MODE);

		if (switch_register_banks)
			arm_save_register_bank(cpu);

		cpu->cd.arm.cpsr = new_cpsr;

		if (switch_register_banks)
			arm_load_register_bank(cpu);
	}

	/*  NOTE: Special case: Loading the PC  */
	if (iw & 0x8000) {
		cpu->cd.arm.r[ARM_PC] &= ~3;
		cpu->pc = cpu->cd.arm.r[ARM_PC];
		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace_return(cpu);
		/*  TODO: There is no need to update the
		    pointers if this is a return to the
		    same page!  */
		/*  Find the new physical page and update the
		    translation pointers:  */
		quick_pc_to_pointers(cpu);
	}
}
Y(bdt_load)


/*
 *  bdt_store:  Block Data Transfer, Store
 *
 *  arg[0] = pointer to uint32_t in host memory, pointing to the base register
 *  arg[1] = 32-bit instruction word. Most bits are read from this.
 */
X(bdt_store)
{
	unsigned char data[4];
	uint32_t *np = (uint32_t *)ic->arg[0];
	uint32_t low_pc, value, addr = *np;
	uint32_t iw = ic->arg[1];  /*  xxxx100P USWLnnnn llllllll llllllll  */
	unsigned char *page;
	int p_bit = iw & 0x01000000;
	int u_bit = iw & 0x00800000;
	int s_bit = iw & 0x00400000;
	int w_bit = iw & 0x00200000;
	int i;

#ifdef GATHER_BDT_STATISTICS
	if (!s_bit)
		update_bdt_statistics(iw);
#endif

	/*  Synchronize the program counter:  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.arm.cur_ic_page) / sizeof(struct arm_instr_call);
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	for (i=(u_bit? 0 : 15); i>=0 && i<=15; i+=(u_bit? 1 : -1)) {
		if (!((iw >> i) & 1)) {
			/*  Skip register i:  */
			continue;
		}

		value = cpu->cd.arm.r[i];

		if (s_bit) {
			switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
			case ARM_MODE_FIQ32:
				if (i >= 8 && i <= 14)
					value = cpu->cd.arm.default_r8_r14[i-8];
				break;
			case ARM_MODE_ABT32:
			case ARM_MODE_UND32:
			case ARM_MODE_IRQ32:
			case ARM_MODE_SVC32:
				if (i >= 13 && i <= 14)
					value = cpu->cd.arm.default_r8_r14[i-8];
				break;
			case ARM_MODE_USR32:
			case ARM_MODE_SYS32:
				break;
			}
		}

		if (i == ARM_PC)
			value += 12;	/*  NOTE/TODO: 8 on some ARMs  */

		if (p_bit) {
			if (u_bit)
				addr += sizeof(uint32_t);
			else
				addr -= sizeof(uint32_t);
		}

		page = cpu->cd.arm.host_store[addr >> 12];
		if (page != NULL) {
			uint32_t *p32 = (uint32_t *) page;
			/*  Change byte order of value if
			    host and emulated endianness differ:  */
#ifdef HOST_LITTLE_ENDIAN
			if (cpu->byte_order == EMUL_BIG_ENDIAN)
#else
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
#endif
				value = ((value & 0xff) << 24) |
				    ((value & 0xff00) << 8) |
				    ((value & 0xff0000) >> 8) |
				    ((value & 0xff000000) >> 24);
			p32[(addr & 0xfff) >> 2] = value;
		} else {
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				data[0] = value;
				data[1] = value >> 8;
				data[2] = value >> 16;
				data[3] = value >> 24;
			} else {
				data[0] = value >> 24;
				data[1] = value >> 16;
				data[2] = value >> 8;
				data[3] = value;
			}
			if (!cpu->memory_rw(cpu, cpu->mem, addr, data,
			    sizeof(data), MEM_WRITE, CACHE_DATA)) {
				/*  store failed  */
				return;
			}
		}

		if (!p_bit) {
			if (u_bit)
				addr += sizeof(uint32_t);
			else
				addr -= sizeof(uint32_t);
		}
	}

	if (w_bit)
		*np = addr;
}
Y(bdt_store)


/*  Various load/store multiple instructions:  */
uint32_t *multi_opcode[256];
void (**multi_opcode_f[256])(struct cpu *, struct arm_instr_call *);
X(multi_0x08b15018);
X(multi_0x08ac000c__ge);
X(multi_0x08a05018);


/*****************************************************************************/


/*
 *  fill_loop_test:
 *
 *  A byte-fill loop. Fills at most one page at a time. If the page was not
 *  in the host_store table, then the original sequence (beginning with
 *  cmps rZ,#0) is executed instead.
 *
 *  L: cmps rZ,#0		ic[0]
 *     strb rX,[rY],#1		ic[1]
 *     sub  rZ,rZ,#1		ic[2]
 *     bgt  L			ic[3]
 *
 *  A maximum of 4 pages are filled before returning.
 */
X(fill_loop_test)
{
	int max_pages_left = 4;
	uint32_t addr, a, n, ofs, maxlen;
	uint32_t *rzp = (uint32_t *)(size_t)ic[0].arg[0];
	unsigned char *page;

restart_loop:
	addr = reg(ic[1].arg[0]);
	page = cpu->cd.arm.host_store[addr >> 12];
	if (page == NULL) {
		instr(cmps)(cpu, ic);
		return;
	}

	n = reg(rzp) + 1;
	ofs = addr & 0xfff;
	maxlen = 4096 - ofs;
	if (n > maxlen)
		n = maxlen;

	/*  printf("x = %x, n = %i\n", reg(ic[1].arg[2]), n);  */
	memset(page + ofs, reg(ic[1].arg[2]), n);

	reg(ic[1].arg[0]) = addr + n;

	reg(rzp) -= n;
	cpu->n_translated_instrs += (4 * n);

	a = reg(rzp);

	cpu->cd.arm.cpsr &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (a != 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_C;
	else
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;
	if ((int32_t)a < 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_N;

	if (max_pages_left-- > 0 && (int32_t)a > 0)
		goto restart_loop;

	cpu->n_translated_instrs --;

	if ((int32_t)a > 0)
		cpu->cd.arm.next_ic = ic;
	else
		cpu->cd.arm.next_ic = &ic[4];
}


/*
 *  fill_loop_test2:
 *
 *  A word-fill loop. Fills at most one page at a time. If the page was not
 *  in the host_store table, then the original sequence (beginning with
 *  cmps rZ,#0) is executed instead.
 *
 *	L: str     rX,[rY],#4		ic[0]
 *	   subs    rZ,rZ,#4		ic[1]
 *	   bgt     L			ic[2]
 *
 *  A maximum of 5 pages are filled before returning.
 */
X(fill_loop_test2)
{
	int max_pages_left = 5;
	unsigned char x1,x2,x3,x4;
	uint32_t addr, a, n, x, ofs, maxlen;
	uint32_t *rzp = (uint32_t *)(size_t)ic[1].arg[0];
	unsigned char *page;

	x = reg(ic[0].arg[2]);
	x1 = x; x2 = x >> 8; x3 = x >> 16; x4 = x >> 24;
	if (x1 != x2 || x1 != x3 || x1 != x4) {
		instr(store_w0_word_u1_p0_imm)(cpu, ic);
		return;
	}

restart_loop:
	addr = reg(ic[0].arg[0]);
	page = cpu->cd.arm.host_store[addr >> 12];
	if (page == NULL || (addr & 3) != 0) {
		instr(store_w0_word_u1_p0_imm)(cpu, ic);
		return;
	}

	/*  printf("addr = 0x%08x, page = %p\n", addr, page);
	    printf("*rzp = 0x%08x\n", reg(rzp));  */

	n = reg(rzp) / 4;
	if (n == 0)
		n++;
	/*  n = nr of _words_  */
	ofs = addr & 0xfff;
	maxlen = 4096 - ofs;
	if (n*4 > maxlen)
		n = maxlen / 4;

	/*  printf("x = %x, n = %i\n", x1, n);  */
	memset(page + ofs, x1, n * 4);

	reg(ic[0].arg[0]) = addr + n * 4;

	reg(rzp) -= (n * 4);
	cpu->n_translated_instrs += (3 * n);

	a = reg(rzp);

	cpu->cd.arm.cpsr &=
	    ~(ARM_FLAG_Z | ARM_FLAG_N | ARM_FLAG_V | ARM_FLAG_C);
	if (a != 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_C;
	else
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;
	if ((int32_t)a < 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_N;

	if (max_pages_left-- > 0 && (int32_t)a > 0)
		goto restart_loop;

	cpu->n_translated_instrs --;

	if ((int32_t)a > 0)
		cpu->cd.arm.next_ic = ic;
	else
		cpu->cd.arm.next_ic = &ic[3];
}


/*
 *  netbsd_memset:
 *
 *  The core of a NetBSD/arm memset.
 *
 *  f01bc420:  e25XX080     subs    rX,rX,#0x80
 *  f01bc424:  a8ac000c     stmgeia ip!,{r2,r3}   (16 of these)
 *  ..
 *  f01bc464:  caffffed     bgt     0xf01bc420      <memset+0x38>
 */
X(netbsd_memset)
{
	unsigned char *page;
	uint32_t addr;

	do {
		addr = cpu->cd.arm.r[ARM_IP];

		instr(subs)(cpu, ic);

		if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=
		    ((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0)) {
			cpu->n_translated_instrs += 16;
			/*  Skip the store multiples:  */
			cpu->cd.arm.next_ic = &ic[17];
			return;
		}

		/*  Crossing a page boundary? Then continue non-combined.  */
		if ((addr & 0xfff) + 128 > 0x1000)
			return;

		/*  R2/R3 non-zero? Not allowed here.  */
		if (cpu->cd.arm.r[2] != 0 || cpu->cd.arm.r[3] != 0)
			return;

		/*  printf("addr = 0x%08x\n", addr);  */

		page = cpu->cd.arm.host_store[addr >> 12];
		/*  No page translation? Continue non-combined.  */
		if (page == NULL)
			return;

		/*  Clear:  */
		memset(page + (addr & 0xfff), 0, 128);
		cpu->cd.arm.r[ARM_IP] = addr + 128;
		cpu->n_translated_instrs += 16;

		/*  Branch back if greater:  */
		cpu->n_translated_instrs += 1;
	} while (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
	    ((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&
	    !(cpu->cd.arm.cpsr & ARM_FLAG_Z));

	/*  Continue at the instruction after the bgt:  */
	cpu->cd.arm.next_ic = &ic[18];
}


/*
 *  netbsd_memcpy:
 *
 *  The core of a NetBSD/arm memcpy.
 *
 *  f01bc530:  e8b15018     ldmia   r1!,{r3,r4,ip,lr}
 *  f01bc534:  e8a05018     stmia   r0!,{r3,r4,ip,lr}
 *  f01bc538:  e8b15018     ldmia   r1!,{r3,r4,ip,lr}
 *  f01bc53c:  e8a05018     stmia   r0!,{r3,r4,ip,lr}
 *  f01bc540:  e2522020     subs    r2,r2,#0x20
 *  f01bc544:  aafffff9     bge     0xf01bc530
 */
X(netbsd_memcpy)
{
	unsigned char *page_0, *page_1;
	uint32_t addr_r0, addr_r1;

	do {
		addr_r0 = cpu->cd.arm.r[0];
		addr_r1 = cpu->cd.arm.r[1];

		/*  printf("addr_r0 = %08x  r1 = %08x\n", addr_r0, addr_r1);  */

		/*  Crossing a page boundary? Then continue non-combined.  */
		if ((addr_r0 & 0xfff) + 32 > 0x1000 ||
		    (addr_r1 & 0xfff) + 32 > 0x1000) {
			instr(multi_0x08b15018)(cpu, ic);
			return;
		}

		page_0 = cpu->cd.arm.host_store[addr_r0 >> 12];
		page_1 = cpu->cd.arm.host_store[addr_r1 >> 12];

		/*  No page translations? Continue non-combined.  */
		if (page_0 == NULL || page_1 == NULL) {
			instr(multi_0x08b15018)(cpu, ic);
			return;
		}

		memcpy(page_0 + (addr_r0 & 0xfff),
		    page_1 + (addr_r1 & 0xfff), 32);
		cpu->cd.arm.r[0] = addr_r0 + 32;
		cpu->cd.arm.r[1] = addr_r1 + 32;

		cpu->n_translated_instrs += 4;

		instr(subs)(cpu, ic + 4);
		cpu->n_translated_instrs ++;

		/*  Loop while greater or equal:  */
		cpu->n_translated_instrs ++;
	} while (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==
	    ((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0));

	/*  Continue at the instruction after the bge:  */
	cpu->cd.arm.next_ic = &ic[6];
	cpu->n_translated_instrs --;
}


/*
 *  netbsd_cacheclean:
 *
 *  The core of a NetBSD/arm cache clean routine, variant 1:
 *
 *  f015f88c:  e4902020     ldr     r2,[r0],#32
 *  f015f890:  e2511020     subs    r1,r1,#0x20
 *  f015f894:  1afffffc     bne     0xf015f88c
 *  f015f898:  ee070f9a     mcr     15,0,r0,cr7,cr10,4
 */
X(netbsd_cacheclean)
{
	uint32_t r1 = cpu->cd.arm.r[1];
	cpu->n_translated_instrs += ((r1 >> 5) * 3);
	cpu->cd.arm.r[0] += r1;
	cpu->cd.arm.r[1] = 0;
	cpu->cd.arm.next_ic = &ic[4];
}


/*
 *  netbsd_cacheclean2:
 *
 *  The core of a NetBSD/arm cache clean routine, variant 2:
 *
 *  f015f93c:  ee070f3a     mcr     15,0,r0,cr7,cr10,1
 *  f015f940:  ee070f36     mcr     15,0,r0,cr7,cr6,1
 *  f015f944:  e2800020     add     r0,r0,#0x20
 *  f015f948:  e2511020     subs    r1,r1,#0x20
 *  f015f94c:  8afffffa     bhi     0xf015f93c
 */
X(netbsd_cacheclean2)
{
	cpu->n_translated_instrs += ((cpu->cd.arm.r[1] >> 5) * 5) - 1;
	cpu->cd.arm.next_ic = &ic[5];
}


/*
 *  netbsd_scanc:
 *
 *	NOTE/TODO:  This implementation is buggy!  (Unknown why.)
 *
 *  f01bccbc:  e5d13000     ldrb    r3,[r1]
 *  f01bccc0:  e7d23003     ldrb    r3,[r2,r3]
 *  f01bccc4:  e113000c     tsts    r3,ip
 */
X(netbsd_scanc)
{
	unsigned char *page = cpu->cd.arm.host_load[cpu->cd.arm.r[1] >> 12];
	uint32_t t;

	if (page == NULL) {
		instr(load_w0_byte_u1_p1_imm)(cpu, ic);
		return;
	}

	t = page[cpu->cd.arm.r[1] & 0xfff];
	t += cpu->cd.arm.r[2];
	page = cpu->cd.arm.host_load[t >> 12];

	if (page == NULL) {
		instr(load_w0_byte_u1_p1_imm)(cpu, ic);
		return;
	}

	cpu->cd.arm.r[3] = page[t & 0xfff];

	t = cpu->cd.arm.r[3] & cpu->cd.arm.r[ARM_IP];
	cpu->cd.arm.cpsr &= ~(ARM_FLAG_Z | ARM_FLAG_N);
	if (t == 0)
		cpu->cd.arm.cpsr |= ARM_FLAG_Z;

	cpu->n_translated_instrs += 2;
	cpu->cd.arm.next_ic = &ic[3];
}


/*
 *  netbsd_copyin:
 *
 *  e4b0a004     ldrt    sl,[r0],#4
 *  e4b0b004     ldrt    fp,[r0],#4
 *  e4b06004     ldrt    r6,[r0],#4
 *  e4b07004     ldrt    r7,[r0],#4
 *  e4b08004     ldrt    r8,[r0],#4
 *  e4b09004     ldrt    r9,[r0],#4
 */
X(netbsd_copyin)
{
	uint32_t r0 = cpu->cd.arm.r[0], ofs = (r0 & 0xffc), index = r0 >> 12;
	unsigned char *p = cpu->cd.arm.host_load[index];
	uint32_t *p32 = (uint32_t *) p, *q32;
	int ok = cpu->cd.arm.is_userpage[index >> 5] & (1 << (index & 31));

	if (ofs > 0x1000 - 6*4 || !ok || p == NULL) {
		instr(load_w1_word_u1_p0_imm)(cpu, ic);
		return;
	}
	q32 = &cpu->cd.arm.r[6];
	ofs >>= 2;
	q32[0] = p32[ofs+2];
	q32[1] = p32[ofs+3];
	q32[2] = p32[ofs+4];
	q32[3] = p32[ofs+5];
	q32[4] = p32[ofs+0];
	q32[5] = p32[ofs+1];
	cpu->cd.arm.r[0] = r0 + 24;
	cpu->n_translated_instrs += 5;
	cpu->cd.arm.next_ic = &ic[6];
}


/*
 *  netbsd_copyout:
 *
 *  e4a18004     strt    r8,[r1],#4
 *  e4a19004     strt    r9,[r1],#4
 *  e4a1a004     strt    sl,[r1],#4
 *  e4a1b004     strt    fp,[r1],#4
 *  e4a16004     strt    r6,[r1],#4
 *  e4a17004     strt    r7,[r1],#4
 */
X(netbsd_copyout)
{
	uint32_t r1 = cpu->cd.arm.r[1], ofs = (r1 & 0xffc), index = r1 >> 12;
	unsigned char *p = cpu->cd.arm.host_store[index];
	uint32_t *p32 = (uint32_t *) p, *q32;
	int ok = cpu->cd.arm.is_userpage[index >> 5] & (1 << (index & 31));

	if (ofs > 0x1000 - 6*4 || !ok || p == NULL) {
		instr(store_w1_word_u1_p0_imm)(cpu, ic);
		return;
	}
	q32 = &cpu->cd.arm.r[6];
	ofs >>= 2;
	p32[ofs  ] = q32[2];
	p32[ofs+1] = q32[3];
	p32[ofs+2] = q32[4];
	p32[ofs+3] = q32[5];
	p32[ofs+4] = q32[0];
	p32[ofs+5] = q32[1];
	cpu->cd.arm.r[1] = r1 + 24;
	cpu->n_translated_instrs += 5;
	cpu->cd.arm.next_ic = &ic[6];
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->cd.arm.r[ARM_PC] &= ~((ARM_IC_ENTRIES_PER_PAGE-1)
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.arm.r[ARM_PC] += (ARM_IC_ENTRIES_PER_PAGE
	    << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC];

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  arm_combine_netbsd_memset():
 *
 *  Check for the core of a NetBSD/arm memset; large memsets use a sequence
 *  of 16 store-multiple instructions, each storing 2 registers at a time.
 */
void arm_combine_netbsd_memset(struct cpu *cpu, void *v, int low_addr)
{
#ifdef HOST_LITTLE_ENDIAN
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 17) {
		int i;
		for (i=-16; i<=-1; i++)
			if (ic[i].f != instr(multi_0x08ac000c__ge))
				return;
		if (ic[-17].f == instr(subs) &&
		    ic[-17].arg[0]==ic[-17].arg[2] && ic[-17].arg[1] == 128 &&
		    ic[ 0].f == instr(b_samepage__gt) &&
		    ic[ 0].arg[0] == (size_t)&ic[-17]) {
			ic[-17].f = instr(netbsd_memset);
			combined;
		}
	}
#endif
}


/*
 *  arm_combine_netbsd_memcpy():
 *
 *  Check for the core of a NetBSD/arm memcpy; large memcpys use a
 *  sequence of ldmia instructions.
 */
void arm_combine_netbsd_memcpy(struct cpu *cpu, void *v, int low_addr)
{
#ifdef HOST_LITTLE_ENDIAN
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 5) {
		if (ic[-5].f==instr(multi_0x08b15018) &&
		    ic[-4].f==instr(multi_0x08a05018) &&
		    ic[-3].f==instr(multi_0x08b15018) &&
		    ic[-2].f==instr(multi_0x08a05018) &&
		    ic[-1].f == instr(subs) &&
		    ic[-1].arg[0]==ic[-1].arg[2] && ic[-1].arg[1] == 0x20 &&
		    ic[ 0].f == instr(b_samepage__ge) &&
		    ic[ 0].arg[0] == (size_t)&ic[-5]) {
			ic[-5].f = instr(netbsd_memcpy);
			combined;
		}
	}
#endif
}


/*
 *  arm_combine_netbsd_cacheclean():
 *
 *  Check for the core of a NetBSD/arm cache clean. (There are two variants.)
 */
void arm_combine_netbsd_cacheclean(struct cpu *cpu, void *v, int low_addr)
{
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 3) {
		if (ic[-3].f==instr(load_w0_word_u1_p0_imm) &&
		    ic[-2].f == instr(subs) &&
		    ic[-2].arg[0]==ic[-2].arg[2] && ic[-2].arg[1] == 0x20 &&
		    ic[-1].f == instr(b_samepage__ne) &&
		    ic[-1].arg[0] == (size_t)&ic[-3]) {
			ic[-3].f = instr(netbsd_cacheclean);
			combined;
		}
	}
}


/*
 *  arm_combine_netbsd_cacheclean2():
 *
 *  Check for the core of a NetBSD/arm cache clean. (Second variant.)
 */
void arm_combine_netbsd_cacheclean2(struct cpu *cpu, void *v, int low_addr)
{
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 4) {
		if (ic[-4].f == instr(mcr_mrc) && ic[-4].arg[0] == 0xee070f3a &&
		    ic[-3].f == instr(mcr_mrc) && ic[-3].arg[0] == 0xee070f36 &&
		    ic[-2].f == instr(add) &&
		    ic[-2].arg[0]==ic[-2].arg[2] && ic[-2].arg[1] == 0x20 &&
		    ic[-1].f == instr(subs) &&
		    ic[-1].arg[0]==ic[-1].arg[2] && ic[-1].arg[1] == 0x20) {
			ic[-4].f = instr(netbsd_cacheclean2);
			combined;
		}
	}
}


/*
 *  arm_combine_netbsd_scanc():
 */
void arm_combine_netbsd_scanc(struct cpu *cpu, void *v, int low_addr)
{
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

return;

	if (n_back >= 2) {
		if (ic[-2].f == instr(load_w0_byte_u1_p1_imm) &&
		    ic[-2].arg[0] == (size_t)(&cpu->cd.arm.r[1]) &&
		    ic[-2].arg[1] == 0 &&
		    ic[-2].arg[2] == (size_t)(&cpu->cd.arm.r[3]) &&
		    ic[-1].f == instr(load_w0_byte_u1_p1_reg) &&
		    ic[-1].arg[0] == (size_t)(&cpu->cd.arm.r[2]) &&
		    ic[-1].arg[1] == (size_t)arm_r_r3_t0_c0 &&
		    ic[-1].arg[2] == (size_t)(&cpu->cd.arm.r[3])) {
			ic[-2].f = instr(netbsd_scanc);
			combined;
		}
	}
}


/*
 *  arm_combine_netbsd_copyin():
 */
void arm_combine_netbsd_copyin(struct cpu *cpu, void *v, int low_addr)
{
#ifdef HOST_LITTLE_ENDIAN
	struct arm_instr_call *ic = v;
	int i, n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back < 5)
		return;

	for (i=-5; i<0; i++) {
		if (ic[i].f != instr(load_w1_word_u1_p0_imm) ||
		    ic[i].arg[0] != (size_t)(&cpu->cd.arm.r[0]) ||
		    ic[i].arg[1] != 4)
			return;
	}

	if (ic[-5].arg[2] == (size_t)(&cpu->cd.arm.r[10]) &&
	    ic[-4].arg[2] == (size_t)(&cpu->cd.arm.r[11]) &&
	    ic[-3].arg[2] == (size_t)(&cpu->cd.arm.r[6]) &&
	    ic[-2].arg[2] == (size_t)(&cpu->cd.arm.r[7]) &&
	    ic[-1].arg[2] == (size_t)(&cpu->cd.arm.r[8])) {
		ic[-5].f = instr(netbsd_copyin);
		combined;
	}
#endif
}


/*
 *  arm_combine_netbsd_copyout():
 */
void arm_combine_netbsd_copyout(struct cpu *cpu, void *v, int low_addr)
{
#ifdef HOST_LITTLE_ENDIAN
	struct arm_instr_call *ic = v;
	int i, n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back < 5)
		return;

	for (i=-5; i<0; i++) {
		if (ic[i].f != instr(store_w1_word_u1_p0_imm) ||
		    ic[i].arg[0] != (size_t)(&cpu->cd.arm.r[1]) ||
		    ic[i].arg[1] != 4)
			return;
	}

	if (ic[-5].arg[2] == (size_t)(&cpu->cd.arm.r[8]) &&
	    ic[-4].arg[2] == (size_t)(&cpu->cd.arm.r[9]) &&
	    ic[-3].arg[2] == (size_t)(&cpu->cd.arm.r[10]) &&
	    ic[-2].arg[2] == (size_t)(&cpu->cd.arm.r[11]) &&
	    ic[-1].arg[2] == (size_t)(&cpu->cd.arm.r[6])) {
		ic[-5].f = instr(netbsd_copyout);
		combined;
	}
#endif
}


/*
 *  arm_combine_test2():
 */
void arm_combine_test2(struct cpu *cpu, void *v, int low_addr)
{
	struct arm_instr_call *ic = v;
	int n_back = (low_addr >> ARM_INSTR_ALIGNMENT_SHIFT)
	    & (ARM_IC_ENTRIES_PER_PAGE-1);

	if (n_back >= 2) {
		if (ic[-2].f == instr(store_w0_word_u1_p0_imm) &&
		    ic[-2].arg[1] == 4 &&
		    ic[-1].f == instr(subs) &&
		    ic[-1].arg[0] == ic[-1].arg[2] && ic[-1].arg[1] == 4 &&
		    ic[ 0].f == instr(b_samepage__gt) &&
		    ic[ 0].arg[0] == (size_t)&ic[-2]) {
			ic[-2].f = instr(fill_loop_test2);
printf("YO test2\n");
			combined;
		}
	}
}


#if 0
	/*  TODO: This is another test hack.  */

	if (n_back >= 3) {
		if (ic[-3].f == instr(cmps) &&
		    ic[-3].arg[0] == ic[-1].arg[0] &&
		    ic[-3].arg[1] == 0 &&
		    ic[-2].f == instr(store_w0_byte_u1_p0_imm) &&
		    ic[-2].arg[1] == 1 &&
		    ic[-1].f == instr(sub) &&
		    ic[-1].arg[0] == ic[-1].arg[2] && ic[-1].arg[1] == 1 &&
		    ic[ 0].f == instr(b_samepage__gt) &&
		    ic[ 0].arg[0] == (size_t)&ic[-3]) {
			ic[-3].f = instr(fill_loop_test);
			combined;
		}
	}
	/*  TODO: Combine forward as well  */
#endif


/*****************************************************************************/


static void arm_switch_clear(struct arm_instr_call *ic, int rd,
	int condition_code)
{
	switch (rd) {
	case  0: ic->f = cond_instr(clear_r0); break;
	case  1: ic->f = cond_instr(clear_r1); break;
	case  2: ic->f = cond_instr(clear_r2); break;
	case  3: ic->f = cond_instr(clear_r3); break;
	case  4: ic->f = cond_instr(clear_r4); break;
	case  5: ic->f = cond_instr(clear_r5); break;
	case  6: ic->f = cond_instr(clear_r6); break;
	case  7: ic->f = cond_instr(clear_r7); break;
	case  8: ic->f = cond_instr(clear_r8); break;
	case  9: ic->f = cond_instr(clear_r9); break;
	case 10: ic->f = cond_instr(clear_r10); break;
	case 11: ic->f = cond_instr(clear_r11); break;
	case 12: ic->f = cond_instr(clear_r12); break;
	case 13: ic->f = cond_instr(clear_r13); break;
	case 14: ic->f = cond_instr(clear_r14); break;
	}
}


static void arm_switch_mov1(struct arm_instr_call *ic, int rd,
	int condition_code)
{
	switch (rd) {
	case  0: ic->f = cond_instr(mov1_r0); break;
	case  1: ic->f = cond_instr(mov1_r1); break;
	case  2: ic->f = cond_instr(mov1_r2); break;
	case  3: ic->f = cond_instr(mov1_r3); break;
	case  4: ic->f = cond_instr(mov1_r4); break;
	case  5: ic->f = cond_instr(mov1_r5); break;
	case  6: ic->f = cond_instr(mov1_r6); break;
	case  7: ic->f = cond_instr(mov1_r7); break;
	case  8: ic->f = cond_instr(mov1_r8); break;
	case  9: ic->f = cond_instr(mov1_r9); break;
	case 10: ic->f = cond_instr(mov1_r10); break;
	case 11: ic->f = cond_instr(mov1_r11); break;
	case 12: ic->f = cond_instr(mov1_r12); break;
	case 13: ic->f = cond_instr(mov1_r13); break;
	case 14: ic->f = cond_instr(mov1_r14); break;
	}
}


static void arm_switch_add1(struct arm_instr_call *ic, int rd,
	int condition_code)
{
	switch (rd) {
	case  0: ic->f = cond_instr(add1_r0); break;
	case  1: ic->f = cond_instr(add1_r1); break;
	case  2: ic->f = cond_instr(add1_r2); break;
	case  3: ic->f = cond_instr(add1_r3); break;
	case  4: ic->f = cond_instr(add1_r4); break;
	case  5: ic->f = cond_instr(add1_r5); break;
	case  6: ic->f = cond_instr(add1_r6); break;
	case  7: ic->f = cond_instr(add1_r7); break;
	case  8: ic->f = cond_instr(add1_r8); break;
	case  9: ic->f = cond_instr(add1_r9); break;
	case 10: ic->f = cond_instr(add1_r10); break;
	case 11: ic->f = cond_instr(add1_r11); break;
	case 12: ic->f = cond_instr(add1_r12); break;
	case 13: ic->f = cond_instr(add1_r13); break;
	case 14: ic->f = cond_instr(add1_r14); break;
	}
}


static void arm_switch_teqs0(struct arm_instr_call *ic, int rn,
	int condition_code)
{
	switch (rn) {
	case  0: ic->f = cond_instr(teqs0_r0); break;
	case  1: ic->f = cond_instr(teqs0_r1); break;
	case  2: ic->f = cond_instr(teqs0_r2); break;
	case  3: ic->f = cond_instr(teqs0_r3); break;
	case  4: ic->f = cond_instr(teqs0_r4); break;
	case  5: ic->f = cond_instr(teqs0_r5); break;
	case  6: ic->f = cond_instr(teqs0_r6); break;
	case  7: ic->f = cond_instr(teqs0_r7); break;
	case  8: ic->f = cond_instr(teqs0_r8); break;
	case  9: ic->f = cond_instr(teqs0_r9); break;
	case 10: ic->f = cond_instr(teqs0_r10); break;
	case 11: ic->f = cond_instr(teqs0_r11); break;
	case 12: ic->f = cond_instr(teqs0_r12); break;
	case 13: ic->f = cond_instr(teqs0_r13); break;
	case 14: ic->f = cond_instr(teqs0_r14); break;
	}
}


/*****************************************************************************/


/*
 *  arm_instr_to_be_translated():
 *
 *  Translate an instruction word into an arm_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc, iword, imm = 0;
	unsigned char *page;
	unsigned char ib[4];
	int condition_code, main_opcode, secondary_opcode, s_bit, rn, rd, r8;
	int p_bit, u_bit, w_bit, l_bit, regform, rm, c, t, any_pc_reg;
	void (*samepage_function)(struct cpu *, struct arm_instr_call *);

	/*  Figure out the address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.arm.cur_ic_page)
	    / sizeof(struct arm_instr_call);
	addr = cpu->cd.arm.r[ARM_PC] & ~((ARM_IC_ENTRIES_PER_PAGE-1) <<
	    ARM_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << ARM_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = cpu->cd.arm.r[ARM_PC] = addr;
	addr &= ~((1 << ARM_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.arm.host_load[addr >> 12];
	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			return;
		}
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iword = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*  The idea of taking bits 27..24 was found here:
	    http://armphetamine.sourceforge.net/oldinfo.html  */
	condition_code = iword >> 28;
	main_opcode = (iword >> 24) & 15;
	secondary_opcode = (iword >> 21) & 15;
	u_bit = iword & 0x00800000;
	w_bit = iword & 0x00200000;
	s_bit = l_bit = iword & 0x00100000;
	rn    = (iword >> 16) & 15;
	rd    = (iword >> 12) & 15;
	r8    = (iword >> 8) & 15;
	c     = (iword >> 7) & 31;
	t     = (iword >> 4) & 7;
	rm    = iword & 15;

	if (condition_code == 0xf) {
		if ((iword & 0xfc70f000) == 0xf450f000) {
			/*  Preload:  TODO.  Treat as NOP for now.  */
			ic->f = instr(nop);
			goto okay;
		}

		fatal("TODO: ARM condition code 0x%x\n",
		    condition_code);
		goto bad;
	}


	/*
	 *  Translate the instruction:
	 */

	switch (main_opcode) {

	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*  Check special cases first:  */
		if ((iword & 0x0fc000f0) == 0x00000090) {
			/*
			 *  Multiplication:
			 *  xxxx0000 00ASdddd nnnnssss 1001mmmm (Rd,Rm,Rs[,Rn])
			 */
			if (iword & 0x00200000) {
				if (s_bit)
					ic->f = cond_instr(mlas);
				else
					ic->f = cond_instr(mla);
				ic->arg[0] = iword;
			} else {
				if (s_bit)
					ic->f = cond_instr(muls);
				else
					ic->f = cond_instr(mul);
				/*  NOTE: rn means rd in this case:  */
				ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
				ic->arg[1] = (size_t)(&cpu->cd.arm.r[rm]);
				ic->arg[2] = (size_t)(&cpu->cd.arm.r[r8]);
			}
			break;
		}
		if ((iword & 0x0f8000f0) == 0x00800090) {
			/*  Long multiplication:  */
			if (s_bit) {
				fatal("TODO: sbit mull\n");
				goto bad;
			}
			ic->f = cond_instr(mull);
			ic->arg[0] = iword;
			break;
		}
		if ((iword & 0x0ff000d0) == 0x01200010) {
			/*  bx or blx  */
			if (iword & 0x20)
				ic->f = cond_instr(blx);
			else {
				if (cpu->machine->show_trace_tree &&
				    rm == ARM_LR)
					ic->f = cond_instr(bx_trace);
				else
					ic->f = cond_instr(bx);
			}
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rm]);
                        break;
                }
		if ((iword & 0x0fb00ff0) == 0x1000090) {
			if (iword & 0x00400000)
				ic->f = cond_instr(swpb);
			else
				ic->f = cond_instr(swp);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
			ic->arg[1] = (size_t)(&cpu->cd.arm.r[rm]);
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[rn]);
			break;
		}
		if ((iword & 0x0fff0ff0) == 0x016f0f10) {
			ic->f = cond_instr(clz);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rm]);
			ic->arg[1] = (size_t)(&cpu->cd.arm.r[rd]);
			break;
		}
		if ((iword & 0x0ff00090) == 0x01000080) {
			/*  TODO: smlaXX  */
			goto bad;
		}
		if ((iword & 0x0ff00090) == 0x01400080) {
			/*  TODO: smlalY  */
			goto bad;
		}
		if ((iword & 0x0ff000b0) == 0x01200080) {
			/*  TODO: smlawY  */
			goto bad;
		}
		if ((iword & 0x0ff0f090) == 0x01600080) {
			/*  TODO: smulXY  */
			goto bad;
		}
		if ((iword & 0x0ff0f0b0) == 0x012000a0) {
			/*  TODO: smulwY  */
			goto bad;
		}
		if ((iword & 0x0fb0fff0) == 0x0120f000 ||
		    (iword & 0x0fb0f000) == 0x0320f000) {
			/*  msr: move to [S|C]PSR from a register or
			    immediate value  */
			if (rm == ARM_PC) {
				fatal("msr PC?\n");
				goto bad;
			}
			if (iword & 0x02000000) {
				if (iword & 0x00400000)
					ic->f = cond_instr(msr_imm_spsr);
				else
					ic->f = cond_instr(msr_imm);
			} else {
				if (iword & 0x00400000)
					ic->f = cond_instr(msr_spsr);
				else
					ic->f = cond_instr(msr);
			}
			imm = iword & 0xff;
			while (r8-- > 0)
				imm = (imm >> 2) | ((imm & 3) << 30);
			ic->arg[0] = imm;
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[rm]);
			switch ((iword >> 16) & 15) {
			case 1:	ic->arg[1] = 0x000000ff; break;
			case 8:	ic->arg[1] = 0xff000000; break;
			case 9:	ic->arg[1] = 0xff0000ff; break;
			default:fatal("unimpl a: msr regform\n");
				goto bad;
			}
			break;
		}
		if ((iword & 0x0fbf0fff) == 0x010f0000) {
			/*  mrs: move from CPSR/SPSR to a register:  */
			if (rd == ARM_PC) {
				fatal("mrs PC?\n");
				goto bad;
			}
			if (iword & 0x00400000)
				ic->f = cond_instr(mrs_spsr);
			else
				ic->f = cond_instr(mrs);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rd]);
			break;
		}
		if ((iword & 0x0e000090) == 0x00000090) {
			int imm = ((iword >> 4) & 0xf0) | (iword & 0xf);
			int regform = !(iword & 0x00400000);
			p_bit = main_opcode & 1;
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
			ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
			if (rd == ARM_PC || rn == ARM_PC) {
				ic->f = arm_load_store_instr_3_pc[
				    condition_code + (l_bit? 16 : 0)
				    + (iword & 0x40? 32 : 0)
				    + (w_bit? 64 : 0)
				    + (iword & 0x20? 128 : 0)
				    + (u_bit? 256 : 0) + (p_bit? 512 : 0)
				    + (regform? 1024 : 0)];
				if (rn == ARM_PC)
					ic->arg[0] = (size_t)
					    (&cpu->cd.arm.tmp_pc);
				if (!l_bit && rd == ARM_PC)
					ic->arg[2] = (size_t)
					    (&cpu->cd.arm.tmp_pc);
			} else
				ic->f = arm_load_store_instr_3[
				    condition_code + (l_bit? 16 : 0)
				    + (iword & 0x40? 32 : 0)
				    + (w_bit? 64 : 0)
				    + (iword & 0x20? 128 : 0)
				    + (u_bit? 256 : 0) + (p_bit? 512 : 0)
				    + (regform? 1024 : 0)];
			if (regform)
				ic->arg[1] = (size_t)(void *)arm_r[iword & 0xf];
			else
				ic->arg[1] = imm;
			break;
		}

		if (iword & 0x80 && !(main_opcode & 2) && iword & 0x10) {
			fatal("reg form blah blah\n");
			goto bad;
		}

		/*  "mov pc,lr":  */
		if ((iword & 0x0fffffff) == 0x01a0f00e) {
			if (cpu->machine->show_trace_tree)
				ic->f = cond_instr(ret_trace);
			else
				ic->f = cond_instr(ret);
			break;
		}

		/*  "mov reg,reg":  */
		if ((iword & 0x0fff0ff0) == 0x01a00000 &&
		    (iword&15) != ARM_PC && rd != ARM_PC) {
			ic->f = cond_instr(mov_reg_reg);
			ic->arg[0] = (size_t)(&cpu->cd.arm.r[rm]);
			ic->arg[1] = (size_t)(&cpu->cd.arm.r[rd]);
			break;
		}

		/*  "mov reg,#0":  */
		if ((iword & 0x0fff0fff) == 0x03a00000 && rd != ARM_PC) {
			arm_switch_clear(ic, rd, condition_code);
			break;
		}

		/*  "mov reg,#1":  */
		if ((iword & 0x0fff0fff) == 0x03a00001 && rd != ARM_PC) {
			arm_switch_mov1(ic, rd, condition_code);
			break;
		}

		/*  "add reg,reg,#1":  */
		if ((iword & 0x0ff00fff) == 0x02800001 && rd != ARM_PC
		    && rn == rd) {
			arm_switch_add1(ic, rd, condition_code);
			break;
		}

		/*  "teqs reg,#0":  */
		if ((iword & 0x0ff0ffff) == 0x03300000 && rn != ARM_PC) {
			arm_switch_teqs0(ic, rn, condition_code);
			break;
		}

		/*
		 *  Generic Data Processing Instructions:
		 */
		if ((main_opcode & 2) == 0)
			regform = 1;
		else
			regform = 0;

		if (regform) {
			/*  0x1000 signifies Carry bit update on rotation,
			    which is not necessary for add,adc,sub,sbc,
			    rsb,rsc,cmp, or cmn, because they update the
			    Carry bit manually anyway.  */
			int q = 0x1000;
			if (s_bit == 0)
				q = 0;
			if ((secondary_opcode >= 2 && secondary_opcode <= 7)
			    || secondary_opcode==0xa || secondary_opcode==0xb)
				q = 0;
			ic->arg[1] = (size_t)(void *)arm_r[(iword & 0xfff) + q];
		} else {
			imm = iword & 0xff;
			while (r8-- > 0)
				imm = (imm >> 2) | ((imm & 3) << 30);
			ic->arg[1] = imm;
		}

		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
		any_pc_reg = 0;
		if (rn == ARM_PC || rd == ARM_PC)
			any_pc_reg = 1;

		ic->f = arm_dpi_instr[condition_code +
		    16 * secondary_opcode + (s_bit? 256 : 0) +
		    (any_pc_reg? 512 : 0) + (regform? 1024 : 0)];

		if (iword == 0xe113000c)
			cpu->combination_check = arm_combine_netbsd_scanc;
		break;

	case 0x4:	/*  Load and store...  */
	case 0x5:	/*  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate  */
	case 0x6:	/*  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register  */
	case 0x7:
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
		if (rd == ARM_PC || rn == ARM_PC) {
			ic->f = arm_load_store_instr_pc[((iword >> 16)
			    & 0x3f0) + condition_code];
			if (rn == ARM_PC)
				ic->arg[0] = (size_t)(&cpu->cd.arm.tmp_pc);
			if (!l_bit && rd == ARM_PC)
				ic->arg[2] = (size_t)(&cpu->cd.arm.tmp_pc);
		} else {
			ic->f = arm_load_store_instr[((iword >> 16) &
			    0x3f0) + condition_code];
		}
		imm = iword & 0xfff;
		if (main_opcode < 6)
			ic->arg[1] = imm;
		else
			ic->arg[1] = (size_t)(void *)arm_r[iword & 0xfff];
		if ((iword & 0x0e000010) == 0x06000010) {
			fatal("Not a Load/store TODO\n");
			goto bad;
		}
		/*  Special case: pc-relative load within the same page:  */
		if (rn == ARM_PC && rd != ARM_PC && main_opcode < 6) {
			int ofs = (addr & 0xfff) + 8, max = 0xffc;
			int b_bit = iword & 0x00400000;
			if (b_bit)
				max = 0xfff;
			if (u_bit)
				ofs += (iword & 0xfff);
			else
				ofs -= (iword & 0xfff);
			/*  NOTE/TODO: This assumes 4KB pages,
			    it will not work with 1KB pages.  */
			if (ofs >= 0 && ofs <= max) {
				unsigned char *p;
				unsigned char c[4];
				int len = b_bit? 1 : 4;
				uint32_t x, a = (addr & 0xfffff000) | ofs;
				/*  ic->f = cond_instr(mov);  */
				ic->f = arm_dpi_instr[condition_code + 16*0xd];
				ic->arg[2] = (size_t)(&cpu->cd.arm.r[rd]);
				p = cpu->cd.arm.host_load[a >> 12];
				if (p != NULL) {
					memcpy(c, p + (a & 0xfff), len);
				} else {
					if (!cpu->memory_rw(cpu, cpu->mem, a,
					    c, len, MEM_READ, CACHE_DATA)) {
						fatal("to_be_translated(): "
						    "read failed X: TODO\n");
						goto bad;
					}
				}
				if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
					x = c[0] + (c[1]<<8) +
					    (c[2]<<16) + (c[3]<<24);
				else
					x = c[3] + (c[2]<<8) +
					    (c[1]<<16) + (c[0]<<24);
				if (b_bit)
					x = c[0];
				ic->arg[1] = x;
			}
		}
		if (iword == 0xe4b09004)
			cpu->combination_check = arm_combine_netbsd_copyin;
		if (iword == 0xe4a17004)
			cpu->combination_check = arm_combine_netbsd_copyout;
		break;

	case 0x8:	/*  Multiple load/store...  (Block data transfer)  */
	case 0x9:	/*  xxxx100P USWLnnnn llllllll llllllll  */
		ic->arg[0] = (size_t)(&cpu->cd.arm.r[rn]);
		ic->arg[1] = (size_t)iword;
		/*  Generic case:  */
		if (l_bit)
			ic->f = cond_instr(bdt_load);
		else
			ic->f = cond_instr(bdt_store);
#if defined(HOST_LITTLE_ENDIAN) && !defined(GATHER_BDT_STATISTICS)
		/*
		 *  Check for availability of optimized implementation:
		 *  xxxx100P USWLnnnn llllllll llllllll
		 *           ^  ^ ^ ^        ^  ^ ^ ^   (0x00950154)
		 *  These bits are used to select which list to scan, and then
		 *  the list is scanned linearly.
		 *
		 *  The optimized functions do not support show_trace_tree,
		 *  but it's ok to use the unoptimized version in that case.
		 */
		if (!cpu->machine->show_trace_tree) {
			int i = 0, j = iword;
			j = ((j & 0x00800000) >> 16) | ((j & 0x00100000) >> 14)
			  | ((j & 0x00040000) >> 13) | ((j & 0x00010000) >> 12)
			  | ((j & 0x00000100) >>  5) | ((j & 0x00000040) >>  4)
			  | ((j & 0x00000010) >>  3) | ((j & 0x00000004) >>  2);
			while (multi_opcode[j][i] != 0) {
				if ((iword & 0x0fffffff) ==
				    multi_opcode[j][i]) {
					ic->f = multi_opcode_f[j]
					    [i*16 + condition_code];
					break;
				}
				i ++;
			}
		}
#endif
		if (rn == ARM_PC) {
			fatal("TODO: bdt with PC as base\n");
			goto bad;
		}
		break;

	case 0xa:					/*  B: branch  */
	case 0xb:					/*  BL: branch+link  */
		if (main_opcode == 0x0a) {
			ic->f = cond_instr(b);
			samepage_function = cond_instr(b_samepage);
			/*  if (iword == 0xcafffffc)
				cpu->combination_check = arm_combine_test2;  */
			if (iword == 0xcaffffed)
				cpu->combination_check =
				    arm_combine_netbsd_memset;
			if (iword == 0xaafffff9)
				cpu->combination_check =
				    arm_combine_netbsd_memcpy;
		} else {
			if (cpu->machine->show_trace_tree) {
				ic->f = cond_instr(bl_trace);
				samepage_function =
				    cond_instr(bl_samepage_trace);
			} else {
				ic->f = cond_instr(bl);
				samepage_function = cond_instr(bl_samepage);
			}
		}

		/*  arg 1 = offset of current instruction  */
		/*  arg 2 = offset of the following instruction  */
		ic->arg[1] = addr & 0xffc;
		ic->arg[2] = (addr & 0xffc) + 4;

		ic->arg[0] = (iword & 0x00ffffff) << 2;
		/*  Sign-extend:  */
		if (ic->arg[0] & 0x02000000)
			ic->arg[0] |= 0xfc000000;
		/*
		 *  Branches are calculated as PC + 8 + offset.
		 */
		ic->arg[0] = (int32_t)(ic->arg[0] + 8);

		/*  Special case: branch within the same page:  */
		{
			uint32_t mask_within_page =
			    ((ARM_IC_ENTRIES_PER_PAGE-1) <<
			    ARM_INSTR_ALIGNMENT_SHIFT) |
			    ((1 << ARM_INSTR_ALIGNMENT_SHIFT) - 1);
			uint32_t old_pc = addr;
			uint32_t new_pc = old_pc + (int32_t)ic->arg[0];
			if ((old_pc & ~mask_within_page) ==
			    (new_pc & ~mask_within_page)) {
				ic->f = samepage_function;
				ic->arg[0] = (size_t) (
				    cpu->cd.arm.cur_ic_page +
				    ((new_pc & mask_within_page) >>
				    ARM_INSTR_ALIGNMENT_SHIFT));
			}
		}

#if 1
		/*  Hm. This doesn't really increase performance.  */
		if (iword == 0x8afffffa)
			cpu->combination_check = arm_combine_netbsd_cacheclean2;
#endif
		break;

	case 0xc:
	case 0xd:
		/*
		 *  TODO: LDC/STC
		 */
		ic->f = cond_instr(und);
		ic->arg[0] = addr & 0xfff;
		break;

	case 0xe:
		if (iword & 0x10) {
			/*  xxxx1110 oooLNNNN ddddpppp qqq1MMMM  MCR/MRC  */
			ic->arg[0] = iword;
			ic->f = cond_instr(mcr_mrc);
		} else {
			/*  xxxx1110 oooonnnn ddddpppp qqq0mmmm  CDP  */
			ic->arg[0] = iword;
			ic->f = cond_instr(cdp);
		}
		if (iword == 0xee070f9a)
			cpu->combination_check = arm_combine_netbsd_cacheclean;
		break;

	case 0xf:
		/*  SWI:  */
		/*  Default handler:  */
		ic->f = cond_instr(swi);
		ic->arg[0] = addr & 0xfff;
		if (iword == 0xef8c64be) {
			/*  Hack for openfirmware prom emulation:  */
			ic->f = instr(openfirmware);
		} else if (cpu->machine->userland_emul != NULL) {
			if ((iword & 0x00f00000) == 0x00a00000) {
				ic->arg[0] = iword & 0x00ffffff;
				ic->f = cond_instr(swi_useremul);
			} else {
				fatal("Bad userland SWI?\n");
				goto bad;
			}
		}
		break;

	default:goto bad;
	}

okay:

#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

