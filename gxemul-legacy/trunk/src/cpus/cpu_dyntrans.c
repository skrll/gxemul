/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_dyntrans.c,v 1.95 2006-06-16 18:31:25 debug Exp $
 *
 *  Common dyntrans routines. Included from cpu_*.c.
 */


#ifdef	DYNTRANS_CPU_RUN_INSTR
#if 1	/*  IC statistics:  */
static void gather_statistics(struct cpu *cpu)
{
	struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic;
	static long long n = 0;
	static FILE *f = NULL;

	n++;
	if (n < 100000000)
		return;

	if (f == NULL) {
		f = fopen("instruction_call_statistics.raw", "w");
		if (f == NULL) {
			fatal("Unable to open statistics file for output.\n");
			exit(1);
		}
	}
	fwrite(&ic->f, 1, sizeof(void *), f);
}
#else	/*  PC statistics:  */
static void gather_statistics(struct cpu *cpu)
{
	uint64_t a;
	int low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - (size_t)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page) / sizeof(struct DYNTRANS_IC);
	if (low_pc < 0 || low_pc >= DYNTRANS_IC_ENTRIES_PER_PAGE)
		return;

#if 0
	/*  Use the physical address:  */
	cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page;
	a = cpu->cd.DYNTRANS_ARCH.cur_physpage->physaddr;
#else
	/*  Use the PC (virtual address):  */
	a = cpu->pc;
#endif

	a &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
	    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	a += low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT;

	/*
	 *  TODO: Everything below this line should be cleaned up :-)
	 */
a &= 0x03ffffff;
{
	static long long *array = NULL;
	static char *array_16kpage_in_use = NULL;
	static int n = 0;
	a >>= DYNTRANS_INSTR_ALIGNMENT_SHIFT;
	if (array == NULL)
		array = zeroed_alloc(sizeof(long long) * 16384*1024);
	if (array_16kpage_in_use == NULL)
		array_16kpage_in_use = zeroed_alloc(sizeof(char) * 1024);
	a &= (16384*1024-1);
	array[a] ++;
	array_16kpage_in_use[a / 16384] = 1;
	n++;
	if ((n & 0x3fffffff) == 0) {
		FILE *f = fopen("statistics.out", "w");
		int i, j;
		printf("Saving statistics... "); fflush(stdout);
		for (i=0; i<1024; i++)
			if (array_16kpage_in_use[i]) {
				for (j=0; j<16384; j++)
					if (array[i*16384 + j] > 0)
						fprintf(f, "%lli\t"
						    "0x%016"PRIx64"\n",
						    (uint64_t)array[i*16384+j],
						    (uint64_t)((i*16384+j) <<
					    DYNTRANS_INSTR_ALIGNMENT_SHIFT));
			}
		fclose(f);
		printf("n=0x%08x\n", n);
	}
}
}
#endif	/*  PC statistics  */


#define S		gather_statistics(cpu)


#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
#define I		ic = cpu->cd.DYNTRANS_ARCH.next_ic;		\
			cpu->cd.DYNTRANS_ARCH.next_ic += ic->arg[0];	\
			ic->f(cpu, ic);
#else

/*  The normal instruction execution core:  */
#define I	ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; ic->f(cpu, ic);

/*  static long long nr_of_I_calls = 0;  */

/*  Temporary hack for finding NULL bugs:  */
/*  #define I	ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; 			\
		nr_of_I_calls ++;					\
		if (ic->f == NULL) {					\
			int low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - \
			    (size_t)cpu->cd.DYNTRANS_ARCH.cur_ic_page) / \
			    sizeof(struct DYNTRANS_IC);			\
			cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) << \
			    DYNTRANS_INSTR_ALIGNMENT_SHIFT);		\
			cpu->pc += (low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT);\
			printf("Crash at %016"PRIx64"\n", cpu->pc);	\
			printf("nr of I calls: %lli\n", nr_of_I_calls);	\
			printf("Next ic = %p\n", cpu->cd.		\
				DYNTRANS_ARCH.next_ic);			\
			printf("cur ic page = %p\n", cpu->cd.		\
				DYNTRANS_ARCH.cur_ic_page);		\
			cpu->running = 0;				\
			return 0;					\
		}							\
		ic->f(cpu, ic);  */

/*  Temporary hack for MIPS, to hunt for 32-bit/64-bit sign-extension bugs:  */
/*  #define I		{ int k; for (k=1; k<=31; k++)	\
	cpu->cd.mips.gpr[k] = (int32_t)cpu->cd.mips.gpr[k];\
	if (cpu->cd.mips.gpr[0] != 0) {			\
		fatal("NOOOOOO\n"); exit(1);		\
	}						\
	ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; ic->f(cpu, ic); }
*/
#endif


/*
 *  XXX_cpu_run_instr():
 *
 *  Execute one or more instructions on a specific CPU, using dyntrans.
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int DYNTRANS_CPU_RUN_INSTR(struct emul *emul, struct cpu *cpu)
{
	/*
	 *  TODO:  Statistics stuff!
	 */
	int show_opcode_statistics = 0;

#ifdef DYNTRANS_DUALMODE_32
	uint64_t cached_pc;
#else
	uint32_t cached_pc;
#endif
	int low_pc, n_instrs;

#ifdef DYNTRANS_DUALMODE_32
	if (cpu->is_32bit)
		DYNTRANS_PC_TO_POINTERS32(cpu);
	else
#endif
	DYNTRANS_PC_TO_POINTERS(cpu);

	/*
	 *  Interrupt assertion?  (This is _below_ the initial PC to pointer
	 *  conversion; if the conversion caused an exception of some kind
	 *  then interrupts are probably disabled, and the exception will get
	 *  priority over device interrupts.)
	 *
	 *  TODO: Turn this into a family-specific function somewhere...
 	 */
#ifdef DYNTRANS_ARM
	if (cpu->cd.arm.irq_asserted && !(cpu->cd.arm.cpsr & ARM_FLAG_I))
		arm_exception(cpu, ARM_EXCEPTION_IRQ);
#endif
#ifdef DYNTRANS_MIPS
	{
		int enabled, mask;
		int status = cpu->cd.mips.coproc[0]->reg[COP0_STATUS];
		if (cpu->cd.mips.cpu_type.exc_model == EXC3K) {
			/*  R3000:  */
			enabled = status & MIPS_SR_INT_IE;
		} else {
			/*  R4000 and others:  */
			enabled = (status & STATUS_IE)
			    && !(status & STATUS_EXL) && !(status & STATUS_ERL);
			/*  Special case for R5900/C790/TX79:  */
			if (cpu->cd.mips.cpu_type.rev == MIPS_R5900 &&
			    !(status & R5900_STATUS_EIE))
				enabled = 0;
		}
		mask = status & cpu->cd.mips.coproc[0]->reg[COP0_CAUSE]
		    & STATUS_IM_MASK;

		if (enabled && mask)
			mips_cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0,0);
	}
#endif
#ifdef DYNTRANS_PPC
	if (cpu->cd.ppc.dec_intr_pending && cpu->cd.ppc.msr & PPC_MSR_EE) {
		ppc_exception(cpu, PPC_EXCEPTION_DEC);
		cpu->cd.ppc.dec_intr_pending = 0;
	}
	if (cpu->cd.ppc.irq_asserted && cpu->cd.ppc.msr & PPC_MSR_EE)
		ppc_exception(cpu, PPC_EXCEPTION_EI);
#endif

	cached_pc = cpu->pc;

	cpu->n_translated_instrs = 0;
	cpu->running_translated = 1;

	cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page;

	if (single_step || cpu->machine->instruction_trace
	    || cpu->machine->register_dump) {
		/*
		 *  Single-step:
		 */
		struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic;
		if (cpu->machine->register_dump) {
			debug("\n");
			cpu_register_dump(cpu->machine, cpu, 1, 0x1);
		}
		if (cpu->machine->instruction_trace) {
#ifdef DYNTRANS_X86
			unsigned char instr[17];
			cpu->cd.x86.cursegment = X86_S_CS;
			cpu->cd.x86.seg_override = 0;
#else
#ifdef DYNTRANS_M68K
			unsigned char instr[16];	/*  TODO: 16?  */
#else
			unsigned char instr[4];		/*  General case...  */
#endif
#endif

			if (!cpu->memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("XXX_cpu_run_instr(): could not read "
				    "the instruction\n");
			} else {
				cpu_disassemble_instr(cpu->machine, cpu,
				    instr, 1, 0);
#ifdef DYNTRANS_DELAYSLOT
				/*  Show the instruction in the delay slot,
				    if any:  */
				if (cpu->instruction_has_delayslot == NULL)
					fatal("WARNING: ihd func not yet"
					    " implemented?\n");
				else if (cpu->instruction_has_delayslot(cpu,
				    instr)) {
					int saved_delayslot = cpu->delay_slot;
					cpu->memory_rw(cpu, cpu->mem, cached_pc
					    + sizeof(instr), &instr[0],
					    sizeof(instr), MEM_READ,
					    CACHE_INSTRUCTION);
					cpu->delay_slot = DELAYED;
					cpu->pc += sizeof(instr);
					cpu_disassemble_instr(cpu->machine,
					    cpu, instr, 1, 0);
					cpu->delay_slot = saved_delayslot;
					cpu->pc -= sizeof(instr);
				}
#endif
			}
		}

		/*  When single-stepping, multiple instruction calls cannot
		    be combined into one. This clears all translations:  */
		if (cpu->cd.DYNTRANS_ARCH.cur_physpage->flags & COMBINATIONS) {
			int i;
			for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++) {
				cpu->cd.DYNTRANS_ARCH.cur_physpage->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
				    cpu->is_32bit?
				        instr32(to_be_translated) :
#endif
				        instr(to_be_translated);
#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
				cpu->cd.DYNTRANS_ARCH.cur_physpage->ics[i].
				    arg[0] = 0;
#endif
			}
			fatal("[ Note: The translation of physical page 0x%"
			    PRIx64" contained combinations of instructions; "
			    "these are now flushed because we are single-"
			    "stepping. ]\n", (long long)cpu->cd.DYNTRANS_ARCH.
			    cur_physpage->physaddr);
			cpu->cd.DYNTRANS_ARCH.cur_physpage->flags &=
			    ~(COMBINATIONS | TRANSLATIONS);
		}

		if (show_opcode_statistics)
			S;

		/*  Execute just one instruction:  */
		I;

		n_instrs = 1;
	} else if (cpu->machine->cycle_accurate) {
		/*  Executing multiple instructions, and call devices'
		    tick functions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;
/*  TODO: continue here  */
int64_t cycles = cpu->cd.avr.extra_cycles;
			I;
			n_instrs += 1;
cycles = cpu->cd.avr.extra_cycles - cycles + 1;
/*  The instruction took 'cycles' cycles.  */
/* printf("A\n"); */
while (cycles-- > 0)
	cpu->machine->tick_func[1](cpu, cpu->machine->tick_extra[1]);
/* printf("B\n"); */

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >=
			    N_SAFE_DYNTRANS_LIMIT / 2)
				break;
		}
	} else if (show_opcode_statistics) {
		/*  Gather statistics while executing multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;

			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;

			n_instrs += 24;

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >=
			    N_SAFE_DYNTRANS_LIMIT / 2)
				break;
		}
	} else {
		/*  Execute multiple instructions:  */
		n_instrs = 0;
		for (;;) {
			struct DYNTRANS_IC *ic;

			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;
			I; I; I; I; I;   I; I; I; I; I;

			I; I; I; I; I;   I; I; I; I; I;

			n_instrs += 60;

			if (!cpu->running_translated ||
			    n_instrs + cpu->n_translated_instrs >=
			    N_SAFE_DYNTRANS_LIMIT / 2)
				break;
		}
	}

	n_instrs += cpu->n_translated_instrs;

	/*  Synchronize the program counter:  */
	low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - (size_t)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page) / sizeof(struct DYNTRANS_IC);
	if (low_pc >= 0 && low_pc < DYNTRANS_IC_ENTRIES_PER_PAGE) {
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (DYNTRANS_IC_ENTRIES_PER_PAGE <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE + 1) {
		/*  Switch to next page and skip an instruction which was
		    already executed (in a delay slot):  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += ((DYNTRANS_IC_ENTRIES_PER_PAGE + 1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	}

#ifdef DYNTRANS_MIPS
	/*  Update the count register (on everything except EXC3K):  */
	if (cpu->cd.mips.cpu_type.exc_model != EXC3K) {
		uint32_t old = cpu->cd.mips.coproc[0]->reg[COP0_COUNT];
		int32_t diff1 = cpu->cd.mips.coproc[0]->reg[COP0_COMPARE] - old;
		int32_t diff2;
		cpu->cd.mips.coproc[0]->reg[COP0_COUNT] =
		    (int32_t) (old + n_instrs);
		diff2 = cpu->cd.mips.coproc[0]->reg[COP0_COMPARE] -
		    cpu->cd.mips.coproc[0]->reg[COP0_COUNT];
		if (cpu->cd.mips.compare_register_set && diff1>0 && diff2<=0)
			cpu_interrupt(cpu, 7);
	}
#endif
#ifdef DYNTRANS_PPC
	/*  Update the Decrementer and Time base registers:  */
	{
		uint32_t old = cpu->cd.ppc.spr[SPR_DEC];
		cpu->cd.ppc.spr[SPR_DEC] = (uint32_t) (old - n_instrs);
		if ((old >> 31) == 0 && (cpu->cd.ppc.spr[SPR_DEC] >> 31) == 1
		    && !(cpu->cd.ppc.cpu_type.flags & PPC_NO_DEC))
			cpu->cd.ppc.dec_intr_pending = 1;
		old = cpu->cd.ppc.spr[SPR_TBL];
		cpu->cd.ppc.spr[SPR_TBL] += n_instrs;
		if ((old >> 31) == 1 && (cpu->cd.ppc.spr[SPR_TBL] >> 31) == 0)
			cpu->cd.ppc.spr[SPR_TBU] ++;
	}
#endif

	/*  Return the nr of instructions executed:  */
	return n_instrs;
}
#endif	/*  DYNTRANS_CPU_RUN_INSTR  */



#ifdef DYNTRANS_FUNCTION_TRACE
/*
 *  XXX_cpu_functioncall_trace():
 *
 *  Without this function, the main trace tree function prints something
 *  like    <f()>  or  <0x1234()>   on a function call. It is up to this
 *  function to print the arguments passed.
 */
void DYNTRANS_FUNCTION_TRACE(struct cpu *cpu, uint64_t f, int n_args)
{
        char strbuf[50];
	char *symbol;
	uint64_t ot;
	int x, print_dots = 1, n_args_to_print =
#if defined(DYNTRANS_ALPHA) || defined(DYNTRANS_SPARC)
	    6
#else
#ifdef DYNTRANS_SH
	    8
#else
	    4	/*  Default value for most archs  */
#endif
#endif
	    ;

	if (n_args >= 0 && n_args <= n_args_to_print) {
		print_dots = 0;
		n_args_to_print = n_args;
	}

	/*
	 *  TODO: The type of each argument should be taken from the symbol
	 *  table, in some way.
	 *
	 *  The code here does a kind of "heuristic guess" regarding what the
	 *  argument values might mean. Sometimes the output looks weird, but
	 *  usually it looks good enough.
	 *
	 *  Print ".." afterwards to show that there might be more arguments
	 *  than were passed in register.
	 */
	for (x=0; x<n_args_to_print; x++) {
		int64_t d;
#ifdef DYNTRANS_X86
		d = 0;		/*  TODO  */
#else
		/*  Args in registers:  */
		d = cpu->cd.DYNTRANS_ARCH.
#ifdef DYNTRANS_ALPHA
		    r[ALPHA_A0
#endif
#ifdef DYNTRANS_ARM
		    r[0
#endif
#ifdef DYNTRANS_AVR
		    /*  TODO: 24,25 = first register, but then
			they go downwards, ie. 22,23 and so on  */
		    r[24
#endif
#ifdef DYNTRANS_HPPA
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_I960
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_IA64
		    r[0		/*  TODO  */
#endif
#ifdef DYNTRANS_M68K
		    d[0		/*  TODO  */
#endif
#ifdef DYNTRANS_MIPS
		    gpr[MIPS_GPR_A0
#endif
#ifdef DYNTRANS_PPC
		    gpr[3
#endif
#ifdef DYNTRANS_SH
		    r[2
#endif
#ifdef DYNTRANS_SPARC
		    r[24
#endif
		    + x];
#endif
		symbol = get_symbol_name(&cpu->machine->symbol_context, d, &ot);

		if (d > -256 && d < 256)
			fatal("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1))
			fatal("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
		else if (symbol != NULL && ot == 0)
			fatal("&%s", symbol);
		else {
			if (cpu->is_32bit)
				fatal("0x%"PRIx32, (uint32_t)d);
			else
				fatal("0x%"PRIx64, (uint64_t)d);
		}

		if (x < n_args_to_print - 1)
			fatal(",");
	}

	if (print_dots)
		fatal(",..");
}
#endif



#ifdef DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE

/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr32(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#endif

#ifdef DYNTRANS_DELAYSLOT
static void instr(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#endif
#endif

/*
 *  XXX_tc_allocate_default_page():
 *
 *  Create a default page (with just pointers to instr(to_be_translated)
 *  at cpu->translation_cache_cur_ofs.
 */
static void DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE(struct cpu *cpu,
	uint64_t physaddr)
{ 
	struct DYNTRANS_TC_PHYSPAGE *ppp;
	int i;

	/*  Create the physpage header:  */
	ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
	    + cpu->translation_cache_cur_ofs);
	ppp->next_ofs = 0;
	ppp->physaddr = physaddr;

	/*  TODO: Is this faster than copying an entire template page?  */
	for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++) {
		ppp->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
		    cpu->is_32bit? instr32(to_be_translated) :
#endif
		    instr(to_be_translated);
#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
		ppp->ics[i].arg[0] = 0;
#endif
	}

	/*  End-of-page:  */
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 0].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page) :
#endif
	    instr(end_of_page);

#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 0].arg[0] = 0;
#endif

	/*  End-of-page-2, for delay-slot architectures:  */
#ifdef DYNTRANS_DELAYSLOT
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 1].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page2) :
#endif
	    instr(end_of_page2);
#endif

	cpu->translation_cache_cur_ofs += sizeof(struct DYNTRANS_TC_PHYSPAGE);

	cpu->translation_cache_cur_ofs --;
	cpu->translation_cache_cur_ofs |= 63;
	cpu->translation_cache_cur_ofs ++;
}
#endif	/*  DYNTRANS_TC_ALLOCATE_DEFAULT_PAGE  */



#ifdef DYNTRANS_PC_TO_POINTERS_FUNC
/*
 *  XXX_pc_to_pointers_generic():
 *
 *  Generic case. See DYNTRANS_PC_TO_POINTERS_FUNC below.
 */
void DYNTRANS_PC_TO_POINTERS_GENERIC(struct cpu *cpu)
{
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    cached_pc = cpu->pc, physaddr = 0;
	uint32_t physpage_ofs;
	int ok, pagenr, table_index;
	uint32_t *physpage_entryp;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

#ifdef MODE32
	int index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
#else
	const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
	const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
	const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
	uint32_t x1, x2, x3;
	struct DYNTRANS_L2_64_TABLE *l2;
	struct DYNTRANS_L3_64_TABLE *l3;

	x1 = (cached_pc >> (64-DYNTRANS_L1N)) & mask1;
	x2 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
	x3 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N)) & mask3;
	/*  fatal("X3: cached_pc=%016"PRIx64" x1=%x x2=%x x3=%x\n",
	    (uint64_t)cached_pc, (int)x1, (int)x2, (int)x3);  */
	l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
	/*  fatal("  l2 = %p\n", l2);  */
	l3 = l2->l3[x2];
	/*  fatal("  l3 = %p\n", l3);  */
#endif

	/*  Virtual to physical address translation:  */
	ok = 0;
#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] != NULL) {
		physaddr = cpu->cd.DYNTRANS_ARCH.phys_addr[index];
		ok = 1;
	}
#else
	if (l3->host_load[x3] != NULL) {
		physaddr = l3->phys_addr[x3];
		ok = 1;
	}
#endif

	if (!ok) {
		uint64_t paddr;
		if (cpu->translate_address != NULL)
			ok = cpu->translate_address(cpu, cached_pc,
			    &paddr, FLAG_INSTR);
		else {
			paddr = cached_pc;
			ok = 1;
		}
		if (!ok) {
			/*  fatal("TODO: instruction vaddr=>paddr translation "
			    "failed. vaddr=0x%"PRIx64"\n", (uint64_t)cached_pc);
			fatal("!! cpu->pc=0x%"PRIx64"\n", (uint64_t)cpu->pc); */

			ok = cpu->translate_address(cpu, cpu->pc, &paddr,
			    FLAG_INSTR);

			/*  printf("EXCEPTION HANDLER: vaddr = 0x%x ==> "
			    "paddr = 0x%x\n", (int)cpu->pc, (int)paddr);
			fatal("!? cpu->pc=0x%"PRIx64"\n", (uint64_t)cpu->pc); */

			if (!ok) {
				fatal("FATAL: could not find physical"
				    " address of the exception handler?");
				exit(1);
			}
		}

		/*  If there was an exception, the PC can have changed.
		    Update cached_pc:  */
		cached_pc = cpu->pc;

#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
#else
		x1 = (cached_pc >> (64-DYNTRANS_L1N)) & mask1;
		x2 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		x3 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N))
		    & mask3;
#endif

		physaddr = paddr;
	}

#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] == NULL) {
#else
	if (l3->host_load[x3] == NULL) {
#endif
		unsigned char *host_page = memory_paddr_to_hostaddr(cpu->mem,
		    physaddr, MEM_READ);
		if (host_page != NULL) {
			int q = DYNTRANS_PAGESIZE - 1;
			host_page += (physaddr &
			    ((1 << BITS_PER_MEMBLOCK) - 1) & ~q);
			cpu->update_translation_table(cpu, cached_pc & ~q,
			    host_page, TLB_CODE, physaddr & ~q);
#ifndef MODE32
			/*  Recalculate l2 and l3, since they might have
			    changed now:  */
			l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
			l3 = l2->l3[x2];
#endif
		}
	}

	if (cpu->translation_cache_cur_ofs >= DYNTRANS_CACHE_SIZE) {
		debug("[ dyntrans: resetting the translation cache ]\n");
		cpu_create_or_reset_tc(cpu);
	}

	pagenr = DYNTRANS_ADDR_TO_PAGENR(physaddr);
	table_index = PAGENR_TO_TABLE_INDEX(pagenr);

	physpage_entryp = &(((uint32_t *)cpu->translation_cache)[table_index]);
	physpage_ofs = *physpage_entryp;
	ppp = NULL;

	/*  Traverse the physical page chain:  */
	while (physpage_ofs != 0) {
		ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
		    + physpage_ofs);
		/*  If we found the page in the cache, then we're done:  */
		if (DYNTRANS_ADDR_TO_PAGENR(ppp->physaddr) == pagenr)
			break;
		/*  Try the next page in the chain:  */
		physpage_ofs = ppp->next_ofs;
	}

	/*  If the offset is 0 (or ppp is NULL), then we need to create a
	    new "default" empty translation page.  */

	if (ppp == NULL) {
		/*  fatal("CREATING page %lli (physaddr 0x%"PRIx64"), table "
		    "index %i\n", (long long)pagenr, (uint64_t)physaddr,
		    (int)table_index);  */
		*physpage_entryp = physpage_ofs =
		    cpu->translation_cache_cur_ofs;

		/*  Allocate a default page, with to_be_translated entries:  */
		DYNTRANS_TC_ALLOCATE(cpu, physaddr);

		ppp = (struct DYNTRANS_TC_PHYSPAGE *)(cpu->translation_cache
		    + physpage_ofs);
	}

#ifdef MODE32
	if (cpu->cd.DYNTRANS_ARCH.host_load[index] != NULL)
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = ppp;
#else
	if (l3->host_load[x3] != NULL)
		l3->phys_page[x3] = ppp;
#endif

#ifdef MODE32
	/*  Small optimization: only mark the physical page as non-writable
	    if it did not contain translations. (Because if it does contain
	    translations, it is already non-writable.)  */
	if (!cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5] &
	    (1 << (pagenr & 31)))
#endif
	cpu->invalidate_translation_caches(cpu, physaddr,
	    JUST_MARK_AS_NON_WRITABLE | INVALIDATE_PADDR);

	cpu->cd.DYNTRANS_ARCH.cur_ic_page = &ppp->ics[0];

	cpu->cd.DYNTRANS_ARCH.next_ic = cpu->cd.DYNTRANS_ARCH.cur_ic_page +
	    DYNTRANS_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc=0x%016"PRIx64"  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016"PRIx64"\n", (uint64_t)cached_pc, (long long)
	    pagenr, (long long)table_index, (uint64_t)physpage_ofs);  */
}


/*
 *  XXX_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 *
 *  NOTE: This is the quick lookup version. See
 *  DYNTRANS_PC_TO_POINTERS_GENERIC above for the generic case.
 */
void DYNTRANS_PC_TO_POINTERS_FUNC(struct cpu *cpu)
{
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    cached_pc = cpu->pc;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

#ifdef MODE32
	int index;
	index = DYNTRANS_ADDR_TO_PAGENR(cached_pc);
	ppp = cpu->cd.DYNTRANS_ARCH.phys_page[index];
	if (ppp != NULL)
		goto have_it;
#else
	const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
	const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
	const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
	uint32_t x1, x2, x3;
	struct DYNTRANS_L2_64_TABLE *l2;
	struct DYNTRANS_L3_64_TABLE *l3;

	x1 = (cached_pc >> (64-DYNTRANS_L1N)) & mask1;
	x2 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
	x3 = (cached_pc >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N)) & mask3;
	l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
	l3 = l2->l3[x2];
	ppp = l3->phys_page[x3];
	if (ppp != NULL)
		goto have_it;
#endif

	DYNTRANS_PC_TO_POINTERS_GENERIC(cpu);
	return;

	/*  Quick return path:  */
have_it:
	cpu->cd.DYNTRANS_ARCH.cur_ic_page = &ppp->ics[0];
	cpu->cd.DYNTRANS_ARCH.next_ic = cpu->cd.DYNTRANS_ARCH.cur_ic_page +
	    DYNTRANS_PC_TO_IC_ENTRY(cached_pc);

	/*  printf("cached_pc=0x%016"PRIx64"  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016"PRIx64"\n", (uint64_t)cached_pc, (long long)
	    pagenr, (long long)table_index, (uint64_t)physpage_ofs);  */
}
#endif	/*  DYNTRANS_PC_TO_POINTERS_FUNC  */



#ifdef DYNTRANS_INIT_64BIT_DUMMY_TABLES
/*
 *  XXX_init_64bit_dummy_tables():
 *
 *  Initializes 64-bit dummy tables and pointers.
 */
void DYNTRANS_INIT_64BIT_DUMMY_TABLES(struct cpu *cpu)
{
	struct DYNTRANS_L2_64_TABLE *dummy_l2;
	struct DYNTRANS_L3_64_TABLE *dummy_l3;
	int x1, x2;

	if (cpu->is_32bit)
		return;

	dummy_l2 = zeroed_alloc(sizeof(struct DYNTRANS_L2_64_TABLE));
	dummy_l3 = zeroed_alloc(sizeof(struct DYNTRANS_L3_64_TABLE));

	cpu->cd.DYNTRANS_ARCH.l2_64_dummy = dummy_l2;
	cpu->cd.DYNTRANS_ARCH.l3_64_dummy = dummy_l3;

	for (x1 = 0; x1 < (1 << DYNTRANS_L1N); x1 ++)
		cpu->cd.DYNTRANS_ARCH.l1_64[x1] = dummy_l2;

	for (x2 = 0; x2 < (1 << DYNTRANS_L2N); x2 ++)
		dummy_l2->l3[x2] = dummy_l3;
}
#endif	/*  DYNTRANS_INIT_64BIT_DUMMY_TABLES  */



#ifdef DYNTRANS_INVAL_ENTRY
/*
 *  XXX_invalidate_tlb_entry():
 *
 *  Invalidate one translation entry (based on virtual address).
 *
 *  If the JUST_MARK_AS_NON_WRITABLE flag is set, then the translation entry
 *  is just downgraded to non-writable (ie the host store page is set to
 *  NULL). Otherwise, the entire translation is removed.
 */
static void DYNTRANS_INVALIDATE_TLB_ENTRY(struct cpu *cpu,
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	vaddr_page, int flags)
{
#ifdef MODE32
	uint32_t index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);

#ifdef DYNTRANS_ARM
	cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5] &= ~(1 << (index & 31));
#endif

	if (flags & JUST_MARK_AS_NON_WRITABLE) {
		/*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
		    (int)vaddr_page);  */
		cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
	} else {
		cpu->cd.DYNTRANS_ARCH.host_load[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.phys_addr[index] = 0;
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[index] = 0;
	}
#else
	const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
	const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
	const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
	uint32_t x1, x2, x3;
	struct DYNTRANS_L2_64_TABLE *l2;
	struct DYNTRANS_L3_64_TABLE *l3;

	x1 = (vaddr_page >> (64-DYNTRANS_L1N)) & mask1;
	x2 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
	x3 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N))& mask3;

	l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
	if (l2 == cpu->cd.DYNTRANS_ARCH.l2_64_dummy)
		return;

	l3 = l2->l3[x2];
	if (l3 == cpu->cd.DYNTRANS_ARCH.l3_64_dummy)
		return;

	if (flags & JUST_MARK_AS_NON_WRITABLE) {
		l3->host_store[x3] = NULL;
		return;
	}
	l3->host_load[x3] = NULL;
	l3->host_store[x3] = NULL;
	l3->phys_addr[x3] = 0;
	l3->phys_page[x3] = NULL;
	l3->refcount --;
	if (l3->refcount < 0) {
		fatal("xxx_invalidate_tlb_entry(): huh? Refcount bug.\n");
		exit(1);
	}
	if (l3->refcount == 0) {
		l3->next = cpu->cd.DYNTRANS_ARCH.next_free_l3;
		cpu->cd.DYNTRANS_ARCH.next_free_l3 = l3;
		l2->l3[x2] = cpu->cd.DYNTRANS_ARCH.l3_64_dummy;

		l2->refcount --;
		if (l2->refcount < 0) {
			fatal("xxx_invalidate_tlb_entry(): Refcount bug L2.\n");
			exit(1);
		}
		if (l2->refcount == 0) {
			l2->next = cpu->cd.DYNTRANS_ARCH.next_free_l2;
			cpu->cd.DYNTRANS_ARCH.next_free_l2 = l2;
			cpu->cd.DYNTRANS_ARCH.l1_64[x1] =
			    cpu->cd.DYNTRANS_ARCH.l2_64_dummy;
		}
	}
#endif
}
#endif


#ifdef DYNTRANS_INVALIDATE_TC
/*
 *  XXX_invalidate_translation_caches():
 *
 *  Invalidate all entries matching a specific physical address, a specific
 *  virtual address, or ALL entries.
 *
 *  flags should be one of
 *	INVALIDATE_PADDR  INVALIDATE_VADDR  or  INVALIDATE_ALL
 *
 *  In addition, for INVALIDATE_ALL, INVALIDATE_VADDR_UPPER4 may be set and
 *  bit 31..28 of addr are used to select the virtual addresses to invalidate.
 *  (This is useful for PowerPC emulation, when segment registers are updated.)
 *
 *  In the case when all translations are invalidated, paddr doesn't need
 *  to be supplied.
 *
 *  NOTE/TODO: When invalidating a virtual address, it is only cleared from
 *             the quick translation array, not from the linear
 *             vph_tlb_entry[] array.  Hopefully this is enough anyway.
 */
void DYNTRANS_INVALIDATE_TC(struct cpu *cpu, uint64_t addr, int flags)
{
	int r;
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    addr_page = addr & ~(DYNTRANS_PAGESIZE - 1);

	/*  fatal("invalidate(): ");  */

	/*  Quick case for _one_ virtual addresses: see note above.  */
	if (flags & INVALIDATE_VADDR) {
		/*  fatal("vaddr 0x%08x\n", (int)addr_page);  */
		DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, addr_page, flags);
		return;
	}

	/*  Invalidate everything:  */
#ifdef DYNTRANS_PPC
	if (flags & INVALIDATE_ALL && flags & INVALIDATE_VADDR_UPPER4) {
		/*  fatal("all, upper4 (PowerPC segment)\n");  */
		for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
			if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid &&
			    (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page
			    & 0xf0000000) == addr_page) {
				DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, cpu->cd.
				    DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
				    0);
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid=0;
			}
		}
		return;
	}
#endif
	if (flags & INVALIDATE_ALL) {
		/*  fatal("all\n");  */
		for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
			if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
				DYNTRANS_INVALIDATE_TLB_ENTRY(cpu, cpu->cd.
				    DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
				    0);
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid=0;
			}
		}
		return;
	}

	/*  Invalidate a physical page:  */

	if (!(flags & INVALIDATE_PADDR))
		fatal("HUH? Invalidate: Not vaddr, all, or paddr?\n");

	/*  fatal("addr 0x%08x\n", (int)addr_page);  */

	for (r=0; r<DYNTRANS_MAX_VPH_TLB_ENTRIES; r++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid && addr_page
		    == cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].paddr_page) {
			DYNTRANS_INVALIDATE_TLB_ENTRY(cpu,
			    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
			    flags);
			if (flags & JUST_MARK_AS_NON_WRITABLE)
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
				    .writeflag = 0;
			else
				cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
				    .valid = 0;
		}
	}
}
#endif	/*  DYNTRANS_INVALIDATE_TC  */



#ifdef DYNTRANS_INVALIDATE_TC_CODE
/*
 *  XXX_invalidate_code_translation():
 *
 *  Invalidate code translations for a specific physical address, a specific
 *  virtual address, or for all entries in the cache.
 */
void DYNTRANS_INVALIDATE_TC_CODE(struct cpu *cpu, uint64_t addr, int flags)
{
	int r;
#ifdef MODE32
	uint32_t
#else
	uint64_t
#endif
	    vaddr_page, paddr_page;

	addr &= ~(DYNTRANS_PAGESIZE-1);

	/*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
	    (int)addr, flags);  */

	if (flags & INVALIDATE_PADDR) {
		int pagenr, table_index;
		uint32_t physpage_ofs, *physpage_entryp;
		struct DYNTRANS_TC_PHYSPAGE *ppp, *prev_ppp;

		pagenr = DYNTRANS_ADDR_TO_PAGENR(addr);

#ifdef MODE32
		/*  If this page isn't marked as having any translations,
		    then return immediately.  */
		if (!(cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5]
		    & 1 << (pagenr & 31)))
			return;
		/*  Remove the mark:  */
		cpu->cd.DYNTRANS_ARCH.phystranslation[pagenr >> 5] &=
		    ~ (1 << (pagenr & 31));
#endif

		table_index = PAGENR_TO_TABLE_INDEX(pagenr);

		physpage_entryp = &(((uint32_t *)cpu->
		    translation_cache)[table_index]);
		physpage_ofs = *physpage_entryp;
		prev_ppp = ppp = NULL;

		/*  Traverse the physical page chain:  */
		while (physpage_ofs != 0) {
			prev_ppp = ppp;
			ppp = (struct DYNTRANS_TC_PHYSPAGE *)
			    (cpu->translation_cache + physpage_ofs);
			/*  If we found the page in the cache,
			    then we're done:  */
			if (ppp->physaddr == addr)
				break;
			/*  Try the next page in the chain:  */
			physpage_ofs = ppp->next_ofs;
		}

		if (physpage_ofs == 0)
			ppp = NULL;

#if 1
		/*
		 *  "Bypass" the page, removing it from the code cache.
		 *
		 *  NOTE/TODO: This gives _TERRIBLE_ performance with self-
		 *  modifying code, or when a single page is used for both
		 *  code and (writable) data.
		 */
		if (ppp != NULL) {
			if (prev_ppp != NULL)
				prev_ppp->next_ofs = ppp->next_ofs;
			else
				*physpage_entryp = ppp->next_ofs;
		}
#else
		/*
		 *  Instead of removing the page from the code cache, each
		 *  entry can be set to "to_be_translated". This is slow in
		 *  the general case, but in the case of self-modifying code,
		 *  it might be faster since we don't risk wasting cache
		 *  memory as quickly (which would force unnecessary Restarts).
		 */
		if (ppp != NULL) {
			/*  TODO: Is this faster than copying an entire
			    template page?  */
			int i;
			for (i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++)
				ppp->ics[i].f =
#ifdef DYNTRANS_DUALMODE_32
				    cpu->is_32bit? instr32(to_be_translated) :
#endif
				    instr(to_be_translated);
		}
#endif
	}

	/*  Invalidate entries (NOTE: only code entries) in the VPH table:  */
	for (r = DYNTRANS_MAX_VPH_TLB_ENTRIES/2;
	     r < DYNTRANS_MAX_VPH_TLB_ENTRIES; r ++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
			vaddr_page = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
			    .vaddr_page & ~(DYNTRANS_PAGESIZE-1);
			paddr_page = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r]
			    .paddr_page & ~(DYNTRANS_PAGESIZE-1);

			if (flags & INVALIDATE_ALL ||
			    (flags & INVALIDATE_PADDR && paddr_page == addr) ||
			    (flags & INVALIDATE_VADDR && vaddr_page == addr)) {
#ifdef MODE32
				uint32_t index =
				    DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
				cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
				/*  Remove the mark:  */
				index = DYNTRANS_ADDR_TO_PAGENR(paddr_page);
				cpu->cd.DYNTRANS_ARCH.phystranslation[
				    index >> 5] &= ~ (1 << (index & 31));
#else
				const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
				const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
				const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
				uint32_t x1, x2, x3;
				struct DYNTRANS_L2_64_TABLE *l2;
				struct DYNTRANS_L3_64_TABLE *l3;

				x1 = (vaddr_page >> (64-DYNTRANS_L1N)) & mask1;
				x2 = (vaddr_page >> (64-DYNTRANS_L1N -
				    DYNTRANS_L2N)) & mask2;
				x3 = (vaddr_page >> (64-DYNTRANS_L1N -
				    DYNTRANS_L2N - DYNTRANS_L3N)) & mask3;
				l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
				l3 = l2->l3[x2];
				l3->phys_page[x3] = NULL;
#endif
			}
		}
	}
}
#endif	/*  DYNTRANS_INVALIDATE_TC_CODE  */



#ifdef DYNTRANS_UPDATE_TRANSLATION_TABLE
/*
 *  XXX_update_translation_table():
 *
 *  Update the virtual memory translation tables.
 */
void DYNTRANS_UPDATE_TRANSLATION_TABLE(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
#ifndef MODE32
	int64_t lowest, highest = -1;
#endif
	int found, r, lowest_index, start, end, useraccess = 0;

#ifdef MODE32
	uint32_t index;
	vaddr_page &= 0xffffffffULL;
	paddr_page &= 0xffffffffULL;
	/*  fatal("update_translation_table(): v=0x%x, h=%p w=%i"
	    " p=0x%x\n", (int)vaddr_page, host_page, writeflag,
	    (int)paddr_page);  */
#else	/*  !MODE32  */
	const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
	const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
	const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
	uint32_t x1, x2, x3;
	struct DYNTRANS_L2_64_TABLE *l2;
	struct DYNTRANS_L3_64_TABLE *l3;
	/*  fatal("update_translation_table(): v=0x%"PRIx64", h=%p w=%i"
	    " p=0x%"PRIx64"\n", (uint64_t)vaddr_page, host_page, writeflag,
	    (uint64_t)paddr_page);  */
#endif

	if (writeflag & MEMORY_USER_ACCESS) {
		writeflag &= ~MEMORY_USER_ACCESS;
		useraccess = 1;
	}

	start = 0; end = DYNTRANS_MAX_VPH_TLB_ENTRIES / 2;
#if 1
	/*  Half of the TLB used for data, half for code:  */
	if (writeflag & TLB_CODE) {
		writeflag &= ~TLB_CODE;
		start = end; end = DYNTRANS_MAX_VPH_TLB_ENTRIES;
	}
#else
	/*  Data and code entries are mixed.  */
	end = DYNTRANS_MAX_VPH_TLB_ENTRIES;
#endif

	/*  Scan the current TLB entries:  */
	lowest_index = start;

#ifdef MODE32
	/*
	 *  NOTE 1: vaddr_to_tlbindex is one more than the index, so that
	 *          0 becomes -1, which means a miss.
	 *
	 *  NOTE 2: When a miss occurs, instead of scanning the entire tlb
	 *          for the entry with the lowest time stamp, just choosing
	 *          one at random will work as well.
	 */
	found = (int)cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[
	    DYNTRANS_ADDR_TO_PAGENR(vaddr_page)] - 1;
	if (found < 0) {
		static unsigned int x = 0;
		lowest_index = (x % (end-start)) + start;
		x ++;
	}
#else
	lowest = cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[0].timestamp;
	found = -1;
	for (r=start; r<end; r++) {
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp < lowest) {
			lowest = cpu->cd.DYNTRANS_ARCH.
			    vph_tlb_entry[r].timestamp;
			lowest_index = r;
		}
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp > highest)
			highest = cpu->cd.DYNTRANS_ARCH.
			    vph_tlb_entry[r].timestamp;
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid &&
		    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page ==
		    vaddr_page) {
			found = r;
			break;
		}
	}
#endif

	if (found < 0) {
		/*  Create the new TLB entry, overwriting the oldest one:  */
		r = lowest_index;
		if (cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid) {
			/*  This one has to be invalidated first:  */
			DYNTRANS_INVALIDATE_TLB_ENTRY(cpu,
			    cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page,
			    0);
		}

		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid = 1;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].host_page = host_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].paddr_page = paddr_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].vaddr_page = vaddr_page;
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag =
		    writeflag & MEM_WRITE;
#ifndef MODE32
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp = highest + 1;
#endif

		/*  Add the new translation to the table:  */
#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
		cpu->cd.DYNTRANS_ARCH.host_load[index] = host_page;
		cpu->cd.DYNTRANS_ARCH.host_store[index] =
		    writeflag? host_page : NULL;
		cpu->cd.DYNTRANS_ARCH.phys_addr[index] = paddr_page;
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
		cpu->cd.DYNTRANS_ARCH.vaddr_to_tlbindex[index] = r + 1;
#ifdef DYNTRANS_ARM
		if (useraccess)
			cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5]
			    |= 1 << (index & 31);
#endif
#else	/* !MODE32  */
		x1 = (vaddr_page >> (64-DYNTRANS_L1N)) & mask1;
		x2 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		x3 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N))
		    & mask3;
		l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
		if (l2 == cpu->cd.DYNTRANS_ARCH.l2_64_dummy) {
			if (cpu->cd.DYNTRANS_ARCH.next_free_l2 != NULL) {
				l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1] =
				    cpu->cd.DYNTRANS_ARCH.next_free_l2;
				cpu->cd.DYNTRANS_ARCH.next_free_l2 = l2->next;
			} else {
				int i;
				l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1] =
				    malloc(sizeof(struct DYNTRANS_L2_64_TABLE));
				for (i=0; i<(1 << DYNTRANS_L2N); i++)
					l2->l3[i] = cpu->cd.DYNTRANS_ARCH.
					    l3_64_dummy;
			}
		}
		l3 = l2->l3[x2];
		if (l3 == cpu->cd.DYNTRANS_ARCH.l3_64_dummy) {
			if (cpu->cd.DYNTRANS_ARCH.next_free_l3 != NULL) {
				l3 = l2->l3[x2] =
				    cpu->cd.DYNTRANS_ARCH.next_free_l3;
				cpu->cd.DYNTRANS_ARCH.next_free_l3 = l3->next;
			} else {
				l3 = l2->l3[x2] = zeroed_alloc(sizeof(
				    struct DYNTRANS_L3_64_TABLE));
			}
			l2->refcount ++;
		}
		l3->host_load[x3] = host_page;
		l3->host_store[x3] = writeflag? host_page : NULL;
		l3->phys_addr[x3] = paddr_page;
		l3->phys_page[x3] = NULL;
		l3->vaddr_to_tlbindex[x3] = r + 1;
		l3->refcount ++;
#endif	/* !MODE32  */
	} else {
		/*
		 *  The translation was already in the TLB.
		 *	Writeflag = 0:  Do nothing.
		 *	Writeflag = 1:  Make sure the page is writable.
		 *	Writeflag = MEM_DOWNGRADE: Downgrade to readonly.
		 */
		r = found;
#ifndef MODE32
		cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].timestamp = highest + 1;
#endif
		if (writeflag & MEM_WRITE)
			cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag = 1;
		if (writeflag & MEM_DOWNGRADE)
			cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].writeflag = 0;
#ifdef MODE32
		index = DYNTRANS_ADDR_TO_PAGENR(vaddr_page);
		cpu->cd.DYNTRANS_ARCH.phys_page[index] = NULL;
#ifdef DYNTRANS_ARM
		cpu->cd.DYNTRANS_ARCH.is_userpage[index>>5] &= ~(1<<(index&31));
		if (useraccess)
			cpu->cd.DYNTRANS_ARCH.is_userpage[index >> 5]
			    |= 1 << (index & 31);
#endif
		if (cpu->cd.DYNTRANS_ARCH.phys_addr[index] == paddr_page) {
			if (writeflag & MEM_WRITE)
				cpu->cd.DYNTRANS_ARCH.host_store[index] =
				    host_page;
			if (writeflag & MEM_DOWNGRADE)
				cpu->cd.DYNTRANS_ARCH.host_store[index] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			cpu->cd.DYNTRANS_ARCH.host_load[index] = host_page;
			cpu->cd.DYNTRANS_ARCH.host_store[index] =
			    writeflag? host_page : NULL;
			cpu->cd.DYNTRANS_ARCH.phys_addr[index] = paddr_page;
		}
#else	/*  !MODE32  */
		x1 = (vaddr_page >> (64-DYNTRANS_L1N)) & mask1;
		x2 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		x3 = (vaddr_page >> (64-DYNTRANS_L1N-DYNTRANS_L2N-DYNTRANS_L3N))
		    & mask3;
		l2 = cpu->cd.DYNTRANS_ARCH.l1_64[x1];
		l3 = l2->l3[x2];
		if (l3->phys_addr[x3] == paddr_page) {
			if (writeflag & MEM_WRITE)
				l3->host_store[x3] = host_page;
			if (writeflag & MEM_DOWNGRADE)
				l3->host_store[x3] = NULL;
		} else {
			/*  Change the entire physical/host mapping:  */
			l3->host_load[x3] = host_page;
			l3->host_store[x3] = writeflag? host_page : NULL;
			l3->phys_addr[x3] = paddr_page;
		}
#endif	/*  !MODE32  */
	}
}
#endif	/*  DYNTRANS_UPDATE_TRANSLATION_TABLE  */


/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_HEAD
	/*
	 *  Check for breakpoints.
	 */
	if (!single_step_breakpoint) {
		MODE_uint_t curpc = cpu->pc;
		int i;
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (curpc == (MODE_uint_t)
			    cpu->machine->breakpoint_addr[i]) {
				if (!cpu->machine->instruction_trace) {
					int old_quiet_mode = quiet_mode;
					quiet_mode = 0;
					DISASSEMBLE(cpu, ib, 1, 0);
					quiet_mode = old_quiet_mode;
				}
				fatal("BREAKPOINT: pc = 0x%"PRIx64"\n(The "
				    "instruction has not yet executed.)\n",
				    (uint64_t)cpu->pc);
#ifdef DYNTRANS_DELAYSLOT
				if (cpu->delay_slot != NOT_DELAYED)
					fatal("ERROR! Breakpoint in a delay"
					    " slot! Not yet supported.\n");
#endif
				single_step_breakpoint = 1;
				single_step = 1;
				goto stop_running_translated;
			}
	}
#endif	/*  DYNTRANS_TO_BE_TRANSLATED_HEAD  */


/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_TAIL
	/*
	 *  If we end up here, then an instruction was translated.
	 *  Mark the page as containing a translation.
	 *
	 *  (Special case for 32-bit mode: set the corresponding bit in the
	 *  phystranslation[] array.)
	 */
	/*  Make sure cur_physpage is in synch:  */
	cpu->cd.DYNTRANS_ARCH.cur_physpage = (void *)
	    cpu->cd.DYNTRANS_ARCH.cur_ic_page;
#ifdef MODE32
	if (!(cpu->cd.DYNTRANS_ARCH.cur_physpage->flags & TRANSLATIONS)) {
		uint32_t index = DYNTRANS_ADDR_TO_PAGENR((uint32_t)addr);
		cpu->cd.DYNTRANS_ARCH.phystranslation[index >> 5] |=
		    (1 << (index & 31));
	}
#endif
	cpu->cd.DYNTRANS_ARCH.cur_physpage->flags |= TRANSLATIONS;


	/*
	 *  Now it is time to check for combinations of instructions that can
	 *  be converted into a single function call.
	 *
	 *  Note: Single-stepping or instruction tracing doesn't work with
	 *  instruction combination. For architectures with delay slots,
	 *  we also ignore combinations if the delay slot is across a page
	 *  boundary.
	 */
	if (!single_step && !cpu->machine->instruction_trace
#ifdef DYNTRANS_DELAYSLOT
	    && !in_crosspage_delayslot
#endif
	    ) {
		if (cpu->cd.DYNTRANS_ARCH.combination_check != NULL &&
		    cpu->machine->speed_tricks)
			cpu->cd.DYNTRANS_ARCH.combination_check(cpu, ic,
			    addr & (DYNTRANS_PAGESIZE - 1));
	}

	cpu->cd.DYNTRANS_ARCH.combination_check = NULL;

	/*  An additional check, to catch some bugs:  */
	if (ic->f == (
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(to_be_translated) :
#endif
	    instr(to_be_translated))) {
		fatal("INTERNAL ERROR: ic->f not set!\n");
		goto bad;
	}
	if (ic->f == NULL) {
		fatal("INTERNAL ERROR: ic->f == NULL!\n");
		goto bad;
	}

	/*  ... and finally execute the translated instruction:  */
	if ((single_step_breakpoint && cpu->delay_slot == NOT_DELAYED)
#ifdef DYNTRANS_DELAYSLOT
	    || in_crosspage_delayslot
#endif
	    ) {
		/*
		 *  Special case when single-stepping: Execute the translated
		 *  instruction, but then replace it with a "to be translated"
		 *  directly afterwards.
		 */
		single_step_breakpoint = 0;
#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
		cpu->cd.DYNTRANS_ARCH.next_ic = ic + ic->arg[0];
#endif
		ic->f(cpu, ic);
		ic->f =
#ifdef DYNTRANS_DUALMODE_32
		    cpu->is_32bit? instr32(to_be_translated) :
#endif
		    instr(to_be_translated);
#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
		ic->arg[0] = 0;
#endif
	} else {
#ifdef DYNTRANS_VARIABLE_INSTRUCTION_LENGTH
		cpu->cd.DYNTRANS_ARCH.next_ic = ic + ic->arg[0];

		/*  Additional check, for variable length ISAs:  */
		if (ic->arg[0] == 0) {
			fatal("INTERNAL ERROR: instr len = 0!\n");
			goto bad;
		}
#endif

		/*  Finally finally :-), execute the instruction:  */
		ic->f(cpu, ic);
	}

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */

	quiet_mode = 0;
	fatal("to_be_translated(): TODO: unimplemented instruction");

	if (cpu->machine->instruction_trace)
#ifdef MODE32
		fatal(" at 0x%"PRIx32"\n", (uint32_t)cpu->pc);
#else
		fatal(" at 0x%"PRIx64"\n", (uint64_t)cpu->pc);
#endif
	else {
		fatal(":\n");
		DISASSEMBLE(cpu, ib, 1, 0);
	}

	cpu->running = 0;
	cpu->dead = 1;
stop_running_translated:
	debugger_n_steps_left_before_interaction = 0;
	cpu->running_translated = 0;
	ic = cpu->cd.DYNTRANS_ARCH.next_ic = &nothing_call;
	cpu->cd.DYNTRANS_ARCH.next_ic ++;

	/*  Execute the "nothing" instruction:  */
	ic->f(cpu, ic);
#endif	/*  DYNTRANS_TO_BE_TRANSLATED_TAIL  */

