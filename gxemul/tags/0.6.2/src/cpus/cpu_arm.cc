/*
 *  Copyright (C) 2005-2019  Anders Gavare.  All rights reserved.
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
 *  ARM CPU emulation.
 *
 *  A good source of quick info on ARM instruction encoding:
 *  http://www.pinknoise.demon.co.uk/ARMinstrs/ARMinstrs.html
 *
 *  Another one, with details about THUMB:
 *  http://engold.ui.ac.ir/~nikmehr/Appendix_B2.pdf
 *
 *  and yet another one, with descriptions about THUMB semantics:
 *  https://web.eecs.umich.edu/~prabal/teaching/eecs373-f10/readings/ARM_QRC0006_UAL16.pdf
 *
 *  And one with the newer ARM v7 instructions:
 *  http://vision.gel.ulaval.ca/~jflalonde/cours/1001/h17/docs/ARM_v7.pdf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "arm_cpu_types.h"
#include "cpu.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "of.h"
#include "settings.h"
#include "symbol.h"

#define DYNTRANS_32
#include "tmp_arm_head.cc"


/*  ARM symbolic register names and condition strings:  */
static const char *arm_regname[N_ARM_REGS] = ARM_REG_NAMES;
static const char *arm_condition_string[16] = ARM_CONDITION_STRINGS;

/*  Data Processing Instructions:  */
static const char *arm_dpiname[16] = ARM_DPI_NAMES;
static int arm_dpi_uses_d[16] = { 1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1 };
static int arm_dpi_uses_n[16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0 };

static const char *arm_thumb_dpiname[16] = ARM_THUMB_DPI_NAMES;

static int arm_exception_to_mode[N_ARM_EXCEPTIONS] = ARM_EXCEPTION_TO_MODE;

extern uint8_t condition_hi[16];
extern uint8_t condition_ge[16];
extern uint8_t condition_gt[16];

/*  For quick_pc_to_pointers():  */
void arm_pc_to_pointers(struct cpu *cpu);
#include "quick_pc_to_pointers.h"

void arm_irq_interrupt_assert(struct interrupt *interrupt);
void arm_irq_interrupt_deassert(struct interrupt *interrupt);


/*
 *  arm_cpu_new():
 *
 *  Create a new ARM cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid ARM processor.
 */
int arm_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i, found;
	struct arm_cpu_type_def cpu_type_defs[] = ARM_CPU_TYPE_DEFS;

	/*  Scan the list for this cpu type:  */
	i = 0; found = -1;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			found = i;
			break;
		}
		i++;
	}
	if (found == -1)
		return 0;

	cpu->run_instr = arm_run_instr;
	cpu->memory_rw = arm_memory_rw;
	cpu->update_translation_table = arm_update_translation_table;
	cpu->invalidate_translation_caches =
	    arm_invalidate_translation_caches;
	cpu->invalidate_code_translation = arm_invalidate_code_translation;
	cpu->translate_v2p = arm_translate_v2p;

	cpu->cd.arm.cpu_type = cpu_type_defs[found];
	cpu->name            = strdup(cpu->cd.arm.cpu_type.name);
	cpu->is_32bit        = 1;
	cpu->byte_order      = EMUL_LITTLE_ENDIAN;

	cpu->vaddr_mask = 0x00000000ffffffffULL;

	cpu->cd.arm.cpsr = ARM_FLAG_I | ARM_FLAG_F;
	cpu->cd.arm.control = ARM_CONTROL_PROG32 | ARM_CONTROL_DATA32
	    | ARM_CONTROL_CACHE | ARM_CONTROL_ICACHE | ARM_CONTROL_ALIGN;
	/*  TODO: default auxctrl contents  */

	if (cpu->machine->prom_emulation) {
		cpu->cd.arm.cpsr |= ARM_MODE_SVC32;
		cpu->cd.arm.control |= ARM_CONTROL_S;
	} else {
		cpu->cd.arm.cpsr |= ARM_MODE_SVC32;
		cpu->cd.arm.control |= ARM_CONTROL_R;
	}

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
		if (cpu->cd.arm.cpu_type.icache_shift != 0 ||
		    cpu->cd.arm.cpu_type.dcache_shift != 0) {
			int isize = cpu->cd.arm.cpu_type.icache_shift;
			int dsize = cpu->cd.arm.cpu_type.dcache_shift;
			if (isize != 0)
				isize = 1 << (isize - 10);
			if (dsize != 0)
				dsize = 1 << (dsize - 10);
			debug(" (I+D = %i+%i KB)", isize, dsize);
		}
	}

	/*  TODO: Some of these values (iway and dway) aren't used yet:  */
	cpu->cd.arm.cachetype =
	      (5 << ARM_CACHETYPE_CLASS_SHIFT)
	    | (1 << ARM_CACHETYPE_HARVARD_SHIFT)
	    | ((cpu->cd.arm.cpu_type.dcache_shift - 9) <<
		ARM_CACHETYPE_DSIZE_SHIFT)
	    | (5 << ARM_CACHETYPE_DASSOC_SHIFT)		/*  32-way  */
	    | (2 << ARM_CACHETYPE_DLINE_SHIFT)		/*  8 words/line  */
	    | ((cpu->cd.arm.cpu_type.icache_shift - 9) <<
		ARM_CACHETYPE_ISIZE_SHIFT)
	    | (5 << ARM_CACHETYPE_IASSOC_SHIFT)		/*  32-way  */
	    | (2 << ARM_CACHETYPE_ILINE_SHIFT);		/*  8 words/line  */

	/*  Coprocessor 15 = the system control coprocessor.  */
	cpu->cd.arm.coproc[15] = arm_coproc_15;

	/*  Coprocessor 14 for XScale:  */
	if (cpu->cd.arm.cpu_type.flags & ARM_XSCALE)
		cpu->cd.arm.coproc[14] = arm_coproc_xscale_14;

	/*
	 *  NOTE/TODO: Ugly hack for OpenFirmware emulation:
	 */
	if (cpu->machine->prom_emulation) {
		cpu->cd.arm.of_emul_addr = cpu->machine->physical_ram_in_mb
		    * 1048576 - 8;
		store_32bit_word(cpu, cpu->cd.arm.of_emul_addr, 0xef8c64be);
	}

	cpu->cd.arm.flags = cpu->cd.arm.cpsr >> 28;

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	for (i=0; i<N_ARM_REGS - 1; i++)
		CPU_SETTINGS_ADD_REGISTER32(arm_regname[i], cpu->cd.arm.r[i]);

	CPU_SETTINGS_ADD_REGISTER32("cpsr", cpu->cd.arm.cpsr);

	/*  Register the CPU's "IRQ" and "FIQ" interrupts:  */
	{
		struct interrupt templ;
		char name[50];
		snprintf(name, sizeof(name), "%s.irq", cpu->path);

                memset(&templ, 0, sizeof(templ));
                templ.line = 0;
                templ.name = name;
                templ.extra = cpu;
                templ.interrupt_assert = arm_irq_interrupt_assert;
                templ.interrupt_deassert = arm_irq_interrupt_deassert;
                interrupt_handler_register(&templ);

		/*  FIQ: TODO  */
        }

	return 1;
}


/*
 *  arm_setup_initial_translation_table():
 *
 *  When booting kernels (such as OpenBSD or NetBSD) directly, it is assumed
 *  that the MMU is already enabled by the boot-loader. This function tries
 *  to emulate that.
 */
void arm_setup_initial_translation_table(struct cpu *cpu, uint32_t ttb_addr)
{
	unsigned char nothing[16384];
	unsigned int i, j;

	cpu->cd.arm.control |= ARM_CONTROL_MMU;
	cpu->translate_v2p = arm_translate_v2p_mmu;
	cpu->cd.arm.dacr |= 0x00000003;
	cpu->cd.arm.ttb = ttb_addr;

	memset(nothing, 0, sizeof(nothing));
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.arm.ttb, nothing,
	    sizeof(nothing), MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);
	for (i=0; i<256; i++)
		for (j=0x0; j<=0xf; j++) {
			unsigned char descr[4];
			uint32_t addr = cpu->cd.arm.ttb +
			    (((j << 28) + (i << 20)) >> 18);
			uint32_t d = (1048576*i) | 0xc02;

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				descr[0] = d;       descr[1] = d >> 8;
				descr[2] = d >> 16; descr[3] = d >> 24;
			} else {
				descr[3] = d;       descr[2] = d >> 8;
				descr[1] = d >> 16; descr[0] = d >> 24;
			}
			cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
			    sizeof(descr), MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);
		}
}


/*
 *  arm_translation_table_set_l1():
 */
void arm_translation_table_set_l1(struct cpu *cpu, uint32_t vaddr,
	uint32_t paddr)
{
	unsigned int i, j, vhigh = vaddr >> 28, phigh = paddr >> 28;

	for (i=0; i<256; i++)
		for (j=vhigh; j<=vhigh; j++) {
			unsigned char descr[4];
			uint32_t addr = cpu->cd.arm.ttb +
			    (((j << 28) + (i << 20)) >> 18);
			uint32_t d = ((phigh << 28) + 1048576*i) | 0xc02;

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				descr[0] = d;       descr[1] = d >> 8;
				descr[2] = d >> 16; descr[3] = d >> 24;
			} else {
				descr[3] = d;       descr[2] = d >> 8;
				descr[1] = d >> 16; descr[0] = d >> 24;
			}
			cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
			    sizeof(descr), MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);
		}
}


/*
 *  arm_translation_table_set_l1_b():
 */
void arm_translation_table_set_l1_b(struct cpu *cpu, uint32_t vaddr,
	uint32_t paddr)
{
	unsigned int i, j, vhigh = vaddr >> 24, phigh = paddr >> 24;

	for (i=0; i<16; i++)
		for (j=vhigh; j<=vhigh; j++) {
			unsigned char descr[4];
			uint32_t addr = cpu->cd.arm.ttb +
			    (((j << 24) + (i << 20)) >> 18);
			uint32_t d = ((phigh << 24) + 1048576*i) | 0xc02;

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				descr[0] = d;       descr[1] = d >> 8;
				descr[2] = d >> 16; descr[3] = d >> 24;
			} else {
				descr[3] = d;       descr[2] = d >> 8;
				descr[1] = d >> 16; descr[0] = d >> 24;
			}
			cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
			    sizeof(descr), MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);
		}
}


/*
 *  arm_cpu_dumpinfo():
 */
void arm_cpu_dumpinfo(struct cpu *cpu)
{
	struct arm_cpu_type_def *ct = &cpu->cd.arm.cpu_type;

	debug(" (I+D = %i+%i KB)\n",
	    (1 << ct->icache_shift) / 1024, (1 << ct->dcache_shift) / 1024);
}


/*
 *  arm_cpu_list_available_types():
 *
 *  Print a list of available ARM CPU types.
 */
void arm_cpu_list_available_types(void)
{
	int i, j;
	struct arm_cpu_type_def tdefs[] = ARM_CPU_TYPE_DEFS;

	i = 0;
	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=13 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 5) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  arm_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void arm_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int mode = cpu->cd.arm.cpsr & ARM_FLAG_MODE;
	int i, x = cpu->cpu_id;

	cpu->cd.arm.cpsr &= 0x0fffffff;
	cpu->cd.arm.cpsr |= (cpu->cd.arm.flags << 28);

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:  cpsr = ", x);
		debug("%s%s%s%s%s%s%s%s%s%s%s",
		    (cpu->cd.arm.cpsr & ARM_FLAG_N)? "N" : "n",
		    (cpu->cd.arm.cpsr & ARM_FLAG_Z)? "Z" : "z",
		    (cpu->cd.arm.cpsr & ARM_FLAG_C)? "C" : "c",
		    (cpu->cd.arm.cpsr & ARM_FLAG_V)? "V" : "v",
		    (cpu->cd.arm.cpsr & ARM_FLAG_Q)? "Q" : "q",
		    (cpu->cd.arm.cpsr & ARM_FLAG_J)? "J" : "j",
		    (cpu->cd.arm.cpsr & ARM_FLAG_E)? "E" : "e",
		    (cpu->cd.arm.cpsr & ARM_FLAG_A)? "A" : "a",
		    (cpu->cd.arm.cpsr & ARM_FLAG_I)? "I" : "i",
		    (cpu->cd.arm.cpsr & ARM_FLAG_F)? "F" : "f",
    		    (cpu->cd.arm.cpsr & ARM_FLAG_T)? "T" : "t");
		if (mode < ARM_MODE_USR32)
			debug("   pc =  0x%07x", (int)(cpu->pc & 0x03ffffff));
		else
			debug("   pc = 0x%08x", (int)cpu->pc);

		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_ARM_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i != ARM_PC)
				debug("  %s = 0x%08x", arm_regname[i],
				    (int)cpu->cd.arm.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}

	if (coprocs & 1) {
		int m = cpu->cd.arm.cpsr & ARM_FLAG_MODE;
		debug("cpu%i:  cpsr = 0x%08x (", x, cpu->cd.arm.cpsr);
		switch (m) {
		case ARM_MODE_USR32:
			debug("USR32)\n"); break;
		case ARM_MODE_SYS32:
			debug("SYS32)\n"); break;
		case ARM_MODE_FIQ32:
			debug("FIQ32)\n"); break;
		case ARM_MODE_IRQ32:
			debug("IRQ32)\n"); break;
		case ARM_MODE_SVC32:
			debug("SVC32)\n"); break;
		case ARM_MODE_ABT32:
			debug("ABT32)\n"); break;
		case ARM_MODE_UND32:
			debug("UND32)\n"); break;
		default:debug("unimplemented)\n");
		}

		if (m != ARM_MODE_USR32 && m != ARM_MODE_SYS32) {
			debug("cpu%i:  usr r8-14:", x);
			for (i=0; i<7; i++)
				debug(" %08x", cpu->cd.arm.default_r8_r14[i]);
			debug("\n");
		}

		if (m != ARM_MODE_FIQ32) {
			debug("cpu%i:  fiq r8-14:", x);
			for (i=0; i<7; i++)
				debug(" %08x", cpu->cd.arm.fiq_r8_r14[i]);
			debug("\n");
		}

		if (m != ARM_MODE_IRQ32) {
			debug("cpu%i:  irq r13-14:", x);
			for (i=0; i<2; i++)
				debug(" %08x", cpu->cd.arm.irq_r13_r14[i]);
			debug("\n");
		}

		if (m != ARM_MODE_SVC32) {
			debug("cpu%i:  svc r13-14:", x);
			for (i=0; i<2; i++)
				debug(" %08x", cpu->cd.arm.svc_r13_r14[i]);
			debug("\n");
		}

		if (m != ARM_MODE_ABT32) {
			debug("cpu%i:  abt r13-14:", x);
			for (i=0; i<2; i++)
				debug(" %08x", cpu->cd.arm.abt_r13_r14[i]);
			debug("\n");
		}

		if (m != ARM_MODE_UND32) {
			debug("cpu%i:  und r13-14:", x);
			for (i=0; i<2; i++)
				debug(" %08x", cpu->cd.arm.und_r13_r14[i]);
			debug("\n");
		}
	}

	if (coprocs & 2) {
		debug("cpu%i:  control = 0x%08x\n", x, cpu->cd.arm.control);
		debug("cpu%i:      MMU:               %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_MMU? "enabled" : "disabled");
		debug("cpu%i:      alignment checks:  %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_ALIGN? "enabled" : "disabled");
		debug("cpu%i:      [data] cache:      %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_CACHE? "enabled" : "disabled");
		debug("cpu%i:      instruction cache: %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_ICACHE? "enabled" : "disabled");
		debug("cpu%i:      write buffer:      %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_WBUFFER? "enabled" : "disabled");
		debug("cpu%i:      prog32:            %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_PROG32? "yes" : "no (using prog26)");
		debug("cpu%i:      data32:            %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_DATA32? "yes" : "no (using data26)");
		debug("cpu%i:      endianness:        %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_BIG? "big endian" : "little endian");
		debug("cpu%i:      high vectors:      %s\n", x,
		    cpu->cd.arm.control &
		    ARM_CONTROL_V? "yes (0xffff0000)" : "no");

		/*  TODO: auxctrl on which CPU types?  */
		if (cpu->cd.arm.cpu_type.flags & ARM_XSCALE) {
			debug("cpu%i:  auxctrl = 0x%08x\n", x,
			    cpu->cd.arm.auxctrl);
			debug("cpu%i:      minidata cache attr = 0x%x\n", x,
			    (cpu->cd.arm.auxctrl & ARM_AUXCTRL_MD)
			    >> ARM_AUXCTRL_MD_SHIFT);
			debug("cpu%i:      page table memory attr: %i\n", x,
			    (cpu->cd.arm.auxctrl & ARM_AUXCTRL_P)? 1 : 0);
			debug("cpu%i:      write buffer coalescing: %s\n", x,
			    (cpu->cd.arm.auxctrl & ARM_AUXCTRL_K)?
			    "disabled" : "enabled");
		}

		debug("cpu%i:  ttb = 0x%08x  dacr = 0x%08x\n", x,
		    cpu->cd.arm.ttb, cpu->cd.arm.dacr);
		debug("cpu%i:  fsr = 0x%08x  far = 0x%08x\n", x,
		    cpu->cd.arm.fsr, cpu->cd.arm.far);
	}
}


/*
 *  arm_save_register_bank():
 */
void arm_save_register_bank(struct cpu *cpu)
{
	/*  Save away current registers:  */
	switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
	case ARM_MODE_USR32:
	case ARM_MODE_SYS32:
		memcpy(cpu->cd.arm.default_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 7);
		break;
	case ARM_MODE_FIQ32:
		memcpy(cpu->cd.arm.fiq_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 7);
		break;
	case ARM_MODE_IRQ32:
		memcpy(cpu->cd.arm.default_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 5);
		cpu->cd.arm.irq_r13_r14[0] = cpu->cd.arm.r[13];
		cpu->cd.arm.irq_r13_r14[1] = cpu->cd.arm.r[14];
		break;
	case ARM_MODE_SVC32:
		memcpy(cpu->cd.arm.default_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 5);
		cpu->cd.arm.svc_r13_r14[0] = cpu->cd.arm.r[13];
		cpu->cd.arm.svc_r13_r14[1] = cpu->cd.arm.r[14];
		break;
	case ARM_MODE_ABT32:
		memcpy(cpu->cd.arm.default_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 5);
		cpu->cd.arm.abt_r13_r14[0] = cpu->cd.arm.r[13];
		cpu->cd.arm.abt_r13_r14[1] = cpu->cd.arm.r[14];
		break;
	case ARM_MODE_UND32:
		memcpy(cpu->cd.arm.default_r8_r14,
		    &cpu->cd.arm.r[8], sizeof(uint32_t) * 5);
		cpu->cd.arm.und_r13_r14[0] = cpu->cd.arm.r[13];
		cpu->cd.arm.und_r13_r14[1] = cpu->cd.arm.r[14];
		break;
	default:fatal("arm_save_register_bank: unimplemented mode %i\n",
		    cpu->cd.arm.cpsr & ARM_FLAG_MODE);
		exit(1);
	}
}


/*
 *  arm_load_register_bank():
 */
void arm_load_register_bank(struct cpu *cpu)
{
	/*  Load new registers:  */
	switch (cpu->cd.arm.cpsr & ARM_FLAG_MODE) {
	case ARM_MODE_USR32:
	case ARM_MODE_SYS32:
		memcpy(&cpu->cd.arm.r[8],
		    cpu->cd.arm.default_r8_r14, sizeof(uint32_t) * 7);
		break;
	case ARM_MODE_FIQ32:
		memcpy(&cpu->cd.arm.r[8], cpu->cd.arm.fiq_r8_r14,
		    sizeof(uint32_t) * 7);
		break;
	case ARM_MODE_IRQ32:
		memcpy(&cpu->cd.arm.r[8],
		    cpu->cd.arm.default_r8_r14, sizeof(uint32_t) * 5);
		cpu->cd.arm.r[13] = cpu->cd.arm.irq_r13_r14[0];
		cpu->cd.arm.r[14] = cpu->cd.arm.irq_r13_r14[1];
		break;
	case ARM_MODE_SVC32:
		memcpy(&cpu->cd.arm.r[8],
		    cpu->cd.arm.default_r8_r14, sizeof(uint32_t) * 5);
		cpu->cd.arm.r[13] = cpu->cd.arm.svc_r13_r14[0];
		cpu->cd.arm.r[14] = cpu->cd.arm.svc_r13_r14[1];
		break;
	case ARM_MODE_ABT32:
		memcpy(&cpu->cd.arm.r[8],
		    cpu->cd.arm.default_r8_r14, sizeof(uint32_t) * 5);
		cpu->cd.arm.r[13] = cpu->cd.arm.abt_r13_r14[0];
		cpu->cd.arm.r[14] = cpu->cd.arm.abt_r13_r14[1];
		break;
	case ARM_MODE_UND32:
		memcpy(&cpu->cd.arm.r[8],
		    cpu->cd.arm.default_r8_r14, sizeof(uint32_t) * 5);
		cpu->cd.arm.r[13] = cpu->cd.arm.und_r13_r14[0];
		cpu->cd.arm.r[14] = cpu->cd.arm.und_r13_r14[1];
		break;
	default:fatal("arm_load_register_bank: unimplemented mode %i\n",
		    cpu->cd.arm.cpsr & ARM_FLAG_MODE);
		exit(1);
	}
}


/*
 *  arm_exception():
 */
void arm_exception(struct cpu *cpu, int exception_nr)
{
	int oldmode, newmode;
	uint32_t retaddr;

	if (exception_nr < 0 || exception_nr >= N_ARM_EXCEPTIONS) {
		fatal("arm_exception(): exception_nr = %i\n", exception_nr);
		exit(1);
	}

	retaddr = cpu->pc;

	if (!quiet_mode) {
		debug("[ arm_exception(): ");
		switch (exception_nr) {
		case ARM_EXCEPTION_RESET:
			fatal("RESET: TODO");
			break;
		case ARM_EXCEPTION_UND:
			debug("UNDEFINED");
			break;
		case ARM_EXCEPTION_SWI:
			debug("SWI");
			break;
		case ARM_EXCEPTION_PREF_ABT:
			debug("PREFETCH ABORT");
			break;
		case ARM_EXCEPTION_IRQ:
			debug("IRQ");
			break;
		case ARM_EXCEPTION_FIQ:
			debug("FIQ");
			break;
		case ARM_EXCEPTION_DATA_ABT:
			debug("DATA ABORT, far=0x%08x fsr=0x%02x",
			    cpu->cd.arm.far, cpu->cd.arm.fsr);
			break;
		}
		debug(" ]\n");
	}

	switch (exception_nr) {
	case ARM_EXCEPTION_RESET:
		cpu->running = 0;
		fatal("ARM RESET: TODO");
		exit(1);
	case ARM_EXCEPTION_DATA_ABT:
		retaddr += 4;
		break;
	}

	retaddr += (cpu->cd.arm.cpsr & ARM_FLAG_T ? 2 : 4);

	arm_save_register_bank(cpu);

	cpu->cd.arm.cpsr &= 0x0fffffff;
	cpu->cd.arm.cpsr |= (cpu->cd.arm.flags << 28);

	switch (arm_exception_to_mode[exception_nr]) {
	case ARM_MODE_SVC32:
		cpu->cd.arm.spsr_svc = cpu->cd.arm.cpsr; break;
	case ARM_MODE_ABT32:
		cpu->cd.arm.spsr_abt = cpu->cd.arm.cpsr; break;
	case ARM_MODE_UND32:
		cpu->cd.arm.spsr_und = cpu->cd.arm.cpsr; break;
	case ARM_MODE_IRQ32:
		cpu->cd.arm.spsr_irq = cpu->cd.arm.cpsr; break;
	case ARM_MODE_FIQ32:
		cpu->cd.arm.spsr_fiq = cpu->cd.arm.cpsr; break;
	default:fatal("arm_exception(): unimplemented exception nr\n");
		exit(1);
	}

	/*
	 *  Disable Thumb mode (because exception handlers always execute
	 *  in ARM mode), set the exception mode, and disable interrupts:
	 */
	cpu->cd.arm.cpsr &= ~ARM_FLAG_T;

	oldmode = cpu->cd.arm.cpsr & ARM_FLAG_MODE;

	cpu->cd.arm.cpsr &= ~ARM_FLAG_MODE;
	cpu->cd.arm.cpsr |= arm_exception_to_mode[exception_nr];

	/*
	 *  Usually, an exception should change modes (so that saved status
	 *  bits don't get lost). However, Linux on ARM seems to use floating
	 *  point instructions in the kernel (!), and it emulates those using
	 *  its own fp emulation code. This leads to a situation where we
	 *  sometimes change from SVC32 to SVC32.
	 */
	newmode = cpu->cd.arm.cpsr & ARM_FLAG_MODE;
	if (oldmode == newmode && oldmode != ARM_MODE_SVC32) {
		fatal("[ WARNING! Exception caused no mode change? "
		    "mode 0x%02x (pc=0x%x) ]\n", newmode, (int)cpu->pc);
		/*  exit(1);  */
	}

	cpu->cd.arm.cpsr |= ARM_FLAG_I;
	if (exception_nr == ARM_EXCEPTION_RESET ||
	    exception_nr == ARM_EXCEPTION_FIQ)
		cpu->cd.arm.cpsr |= ARM_FLAG_F;

	/*  Load the new register bank, if we switched:  */
	arm_load_register_bank(cpu);

	/*
	 *  Set the return address and new PC.
	 *
	 *  NOTE: r[ARM_PC] is also set; see cpu_arm_instr_loadstore.c for
	 *  details. (If an exception occurs during a load into the pc
	 *  register, the code in that file assumes that the r[ARM_PC]
	 *  was changed to the address of the exception handler.)
	 */
	cpu->cd.arm.r[ARM_LR] = retaddr;
	cpu->pc = cpu->cd.arm.r[ARM_PC] = exception_nr * 4 +
	    ((cpu->cd.arm.control & ARM_CONTROL_V)? 0xffff0000 : 0);
	quick_pc_to_pointers(cpu);
}


/*
 *  arm_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void arm_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  arm_irq_interrupt_assert():
 *  arm_irq_interrupt_deassert():
 */
void arm_irq_interrupt_assert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.arm.irq_asserted = 1;
}
void arm_irq_interrupt_deassert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.arm.irq_asserted = 0;
}


/*
 *  arm_cpu_disassemble_instr_thumb():
 *
 *  Like arm_cpu_disassemble_instr below, but for THUMB encodings.
 *  Note that the disassbmly uses regular ARM mnemonics, not "THUMB
 *  assembly language".
 */                     
int arm_cpu_disassemble_instr_thumb(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	uint16_t iw;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8);
	else
		iw = ib[1] + (ib[0]<<8);
	debug("%04x    \t", (int)iw);

	int main_opcode = (iw >> 12) & 15;
	int r6 = (iw >> 6) & 7;
	int rs_rb = (iw >> 3) & 7;
	int rd = iw & 7;
	int offset5 = (iw >> 6) & 0x1f;
	int b_bit = (iw >> 12) & 1;
	int l_bit = (iw >> 11) & 1;
	int addsub_op = (iw >> 9) & 1;
	int addsub_immediate = (iw >> 10) & 1;
	int op11 = (iw >> 11) & 3;
	int op10 = (iw >> 10) & 3;
	int rd8 = (iw >> 8) & 7;
	int op8 = (iw >> 8) & 3;
	int h1 = (iw >> 7) & 1;
	int h2 = (iw >> 6) & 1;
	int condition_code = (iw >> 8) & 15;
	const char* condition = arm_condition_string[condition_code];
	const char* symbol;
	uint64_t offset;
	int tmp;
	uint32_t tmp32;
	
	switch (main_opcode)
	{
	case 0x0:
	case 0x1:
		if (op11 <= 2) {
			// Move shifted register.
			debug("%ss\tr%i,r%i,#%i\n",
				op11 & 1 ? "lsr" : (op11 & 2 ? "asr" : "lsl"),
				rd,
				rs_rb,				
				offset5);
		} else {
			// Add/subtract.
			debug("%s\tr%i,r%i,%s%i\n",
				addsub_op ? "subs" : "adds",
				rd,
				rs_rb,
				addsub_immediate ? "#" : "r",
				r6);
		}
		break;

	case 0x2:
	case 0x3:
		// Move/compare/add/subtract immediate.
		rd = (iw >> 8) & 7;
		if (op11 & 2) {
			debug("%s\tr%i,r%i,#%i\n",
				op11 & 1 ? "subs" : "adds",
				rd, rd,
				iw & 0xff);
		} else {
			debug("%s\tr%i,#%i\n",
				op11 & 1 ? "cmp" : "movs",
				rd,
				iw & 0xff);
		}
		break;

	case 0x4:
		switch (op10) {
		case 0:
			// ALU operations.
			debug("%s\t%s,%s",
				arm_thumb_dpiname[(iw >> 6) & 15],
				arm_regname[rd],
				arm_regname[rs_rb]);
			
			if (running) {
				debug("\t\t; %s = 0x%x", arm_regname[rd], cpu->cd.arm.r[rd]);
				debug(", %s = 0x%x", arm_regname[rs_rb], cpu->cd.arm.r[rs_rb]);
			}

			debug("\n");
			break;

		case 1:
			// Hi register operations / branch exchange.
			if (h1)
				rd += 8;
			if (h2)
				rs_rb += 8;
			switch (op8) {
			case 0:
			case 1:
			case 2:
				if (h1 == 0 && h2 == 0) {
					debug("TODO main_opcode = %i, op10 = %i, h1 AND h2 are zero?!\n", main_opcode, op10);
				} else {
					if (op8 == 0)
						debug("add\tr%i,r%i,r%i\n", rd, rd, rs_rb);
					else {
						if (op8 == 2 && rd == rs_rb)
							debug("nop\n");	// mov rX,rX
						else {
							debug("%s\t%s,%s",
							    op8 == 1 ? "cmp" : "mov",
							    	arm_regname[rd],
							    	arm_regname[rs_rb]);
							if (running) {
								debug("\t\t; %s = 0x%x", arm_regname[rd], cpu->cd.arm.r[rd]);
								debug(", %s = 0x%x", arm_regname[rs_rb], cpu->cd.arm.r[rs_rb]);
							}
							debug("\n");
						}
					}
				}
				break;
			case 3:
				if (h1 == 1) {
					debug("TODO main_opcode = %i, op10 = %i, h1 set for BX?!\n", main_opcode, op10);
				} else {
					debug("bx\t%s\n", arm_regname[rs_rb]);

					// Extra newline when returning from function.
					// if (running && rs_rb == ARM_LR)
					//	debug("\n");
				}
				break;
			}
			break;

		case 2:
		case 3:
			// PC-relative load.
			debug("ldr\t%s,[pc,#%i]\t; ",
				arm_regname[rd8],
				(iw & 0xff) * 4);

			// Is this address calculation correct?
			// It works with real code, but is not the same as that of
			// http://engold.ui.ac.ir/~nikmehr/Appendix_B2.pdf
			tmp = (dumpaddr & ~3) + 4 + (iw & 0xff) * 4;
			debug("0x%x", (int)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmp, &offset);
			if (symbol != NULL)
				debug(" <%s>", symbol);
			debug("\n");
			break;
		}
		break;

	case 0x5:
		// Load/Store with register offset.
		switch ((iw >> 9) & 7) {
		case 0:	debug("str"); break;
		case 1:	debug("strh"); break;
		case 2:	debug("strb"); break;
		case 3:	debug("ldrsb"); break;
		case 4:	debug("ldr"); break;
		case 5:	debug("ldrh"); break;
		case 6:	debug("ldrb"); break;
		case 7:	debug("ldrsh"); break;
		}
		debug("\t%s,[%s,%s]\n",
			arm_regname[rd],
			arm_regname[rs_rb],
			arm_regname[r6]);
		break;
		
	case 0x6:
	case 0x7:
		// Load/Store with immediate offset.
		debug("%s%s\tr%i,[r%i,#%i]\n",
			l_bit ? "ldr" : "str",
			b_bit ? "b" : "",
			rd,
			rs_rb,
			offset5 * (b_bit ? sizeof(uint8_t) : sizeof(uint32_t)));
		break;

	case 0x8:
		// Load/Store halfword.
		debug("%sh\tr%i,[r%i,#%i]\n",
			l_bit ? "ldr" : "str",
			rd,
			rs_rb,
			offset5 * sizeof(uint16_t));
		break;

	case 0x9:
		// Load/Store stack pointer relative.
		debug("%s\t%s,[sp,#%i]\n",
			l_bit ? "ldr" : "str",
			arm_regname[rd8],
			4 * (iw & 0xff));
		break;

	case 0xa:
		// add rx, sp or pc plus imm
		debug("add\t%s,%s,#%i\n",
			arm_regname[rd8],
			iw & 0x0800 ? "sp" : "pc",
			4 * (iw & 0xff));
		break;

	case 0xb:
		/*  Bits 11..8:  */
		switch (condition_code) {
		case 0x0:
			tmp = (iw & 0x7f) << 2;
			debug(iw & 0x80 ? "sub" : "add");
			debug("\tsp,#%i\n", tmp);
			break;
		case 0x4:
		case 0x5:
		case 0xc:
		case 0xd:
			debug(condition_code & 8 ? "pop" : "push");
			debug("\t{");
			for (tmp=0; tmp<8; ++tmp) {
				if (iw & (1 << tmp))
					debug("%s,", arm_regname[tmp]);
			}
			if (condition_code & 1) {			
				if (condition_code & 8)
					debug("pc");
				else
					debug("lr");
			}
			debug("}\n");

			// Extra newline when returning from function.
			// if (running && (condition_code & 8) && (condition_code & 1))
			//	debug("\n");
			break;
		default:
			debug("TODO: unimplemented opcode 0x%x,0x%x\n", main_opcode, condition_code);
		}
		break;
		
	case 0xd:
		if (condition_code < 0xe) {
			// Conditional branch.
			debug("b%s\t", condition);
			tmp = (iw & 0xff) << 1;
			if (tmp & 0x100)
				tmp |= 0xfffffe00;
			tmp = (int32_t)(dumpaddr + 4 + tmp);
			debug("0x%x", (int)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmp, &offset);
			if (symbol != NULL)
				debug(" \t<%s>", symbol);
			debug("\n");
		} else if (condition_code == 0xf) {
			debug("swi\t#0x%x\n", iw & 0xff);
		} else {
			debug("UNIMPLEMENTED\n");
		}
		break;

	case 0xe:
		// Unconditional branch.
		if (iw & 0x0800) {
			uint32_t addr = (dumpaddr + 4 + (cpu->cd.arm.tmp_branch + (((iw >> 1) & 0x3ff) << 2))) & ~3;
			
			debug("blx\t");
			if (running) {
				debug("0x%x", addr);
				symbol = get_symbol_name(&cpu->machine->symbol_context, addr, &offset);
				if (symbol != NULL)
					debug(" \t<%s>", symbol);
				debug("\n");
			} else
				debug("offset depending on prefix at runtime\n");
		} else {
			tmp = (iw & 0x7ff) << 1;
			if (tmp & 0x800)
				tmp |= 0xfffff000;
			tmp = (int32_t)(dumpaddr + 4 + tmp);
			debug("b\t0x%x", (int)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context, tmp, &offset);
			if (symbol != NULL)
				debug(" \t<%s>", symbol);
			debug("\n");
		}
		break;

	case 0xf:
		if (iw & 0x0800) {
			uint32_t addr = (dumpaddr + 2 + (cpu->cd.arm.tmp_branch + ((iw & 0x7ff) << 1)));
			
			debug("bl\t");
			if (running) {
				debug("0x%x", addr);
				symbol = get_symbol_name(&cpu->machine->symbol_context, addr, &offset);
				if (symbol != NULL)
					debug(" \t<%s>", symbol);
				debug("\n");
			} else
				debug("offset depending on prefix at runtime\n");
		} else {
			tmp32 = iw & 0x07ff;
			if (tmp32 & 0x0400)
				tmp32 |= 0xfffff800;
			tmp32 <<= 12;
			debug("bprefix\t0x%x\n", tmp32);
		}
		break;

	default:
		debug("TODO: unimplemented opcode 0x%x\n", main_opcode);
	}

	return sizeof(uint16_t);
}


/*
 *  arm_cpu_interpret_thumb_SLOW():
 *
 *  Slow interpretation of THUMB instructions.
 *
 *  TODO: Either replace this with dyntrans in the old framework, or
 *  implement ARM (including THUMB) in the new framework. :-)
 *  For now, this is better than nothing.
 */
int arm_cpu_interpret_thumb_SLOW(struct cpu *cpu)
{
	uint16_t iw;
	uint8_t ib[sizeof(uint16_t)];
	uint32_t addr = cpu->pc & ~1;

	if (!(cpu->pc & 1) || !(cpu->cd.arm.cpsr & ARM_FLAG_T)) {
		fatal("arm_cpu_interpret_thumb_SLOW called when not in "
			"THUMB mode?\n");
		cpu->running = 0;
		return 0;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
			sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
		fatal("arm_cpu_interpret_thumb_SLOW(): could not read "
			"the instruction\n");
		cpu->running = 0;
		return 0;
	}
	
	if (cpu->machine->instruction_trace) {
		uint64_t offset;
		char* symbol = get_symbol_name(&cpu->machine->symbol_context,
		    addr, &offset);
		if (symbol != NULL && offset == 0)
			debug("<%s>\n", symbol);

		if (cpu->machine->ncpus > 1)
			debug("cpu%i:\t", cpu->cpu_id);

		debug("%08x:  ", (int)addr & ~1);
		arm_cpu_disassemble_instr_thumb(cpu, ib, 1, cpu->pc);
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8);
	else
		iw = ib[1] + (ib[0]<<8);

	cpu->ninstrs ++;

	int main_opcode = (iw >> 12) & 15;
	int condition_code = (iw >> 8) & 15;
	int op10 = (iw >> 10) & 3;
	int op11_8 = (iw >> 8) & 15;
	int rd8 = (iw >> 8) & 7;
	int r6 = (iw >> 6) & 7;
	int r3 = (iw >> 3) & 7;
	int rd = (iw >> 0) & 7;
	int hd = (iw >> 7) & 1;
	int hm = (iw >> 6) & 1;
	int imm6 = (iw >> 6) & 31;
	uint8_t word[sizeof(uint32_t)];
	uint32_t tmp;
	int t, len, isLoad;

	switch (main_opcode)
	{
	case 0x0:
		if (iw & 0x0800) {
			// lsr
			tmp = cpu->cd.arm.r[r3];
			if (imm6 > 1)
				tmp >>= (imm6 - 1);
			cpu->cd.arm.r[rd] = cpu->cd.arm.r[r3] >> imm6;
			tmp &= 1;
		} else {
			// lsl
			tmp = cpu->cd.arm.r[r3];
			if (imm6 > 1)
				tmp <<= (imm6 - 1);
			cpu->cd.arm.r[rd] = cpu->cd.arm.r[r3] << imm6;
			tmp >>= 31;
		}
		cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
		if (cpu->cd.arm.r[rd] == 0)
			cpu->cd.arm.flags |= ARM_F_Z;
		if ((int32_t)cpu->cd.arm.r[rd] < 0)
			cpu->cd.arm.flags |= ARM_F_N;
		if (imm6 != 0) {
			// tmp is "last bit shifted out"
			cpu->cd.arm.flags &= ~ARM_F_C;
			if (tmp)
				cpu->cd.arm.flags |= ARM_F_C;
		}
		break;

	case 0x1:
		if (!(iw & 0x0800)) {
			// asr
			tmp = cpu->cd.arm.r[r3];
			if (imm6 > 1)
				tmp = (int32_t)tmp >> (imm6 - 1);
			cpu->cd.arm.r[rd] = (int32_t)cpu->cd.arm.r[r3] >> imm6;
			tmp &= 1;
			cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
			if (cpu->cd.arm.r[rd8] == 0)
				cpu->cd.arm.flags |= ARM_F_Z;
			if ((int32_t)cpu->cd.arm.r[rd8] < 0)
				cpu->cd.arm.flags |= ARM_F_N;
			if (imm6 != 0) {
				// tmp is "last bit shifted out"
				cpu->cd.arm.flags &= ~ARM_F_C;
				if (tmp)
					cpu->cd.arm.flags |= ARM_F_C;
			}
		} else {
			// add or sub
			int isSub = iw & 0x0200;
			int isImm3 = iw & 0x0400;
			uint64_t old = cpu->cd.arm.r[r3];
			uint64_t tmp64 = isImm3 ? r6 : cpu->cd.arm.r[r6];
			tmp64 = (uint32_t)(isSub ? -tmp64 : tmp64);
			uint64_t result = old + tmp64;
			cpu->cd.arm.r[rd] = result;
			cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N | ARM_F_C | ARM_F_V);
			if (result == 0)
				cpu->cd.arm.flags |= ARM_F_Z;
			if ((int32_t)result < 0)
				cpu->cd.arm.flags |= ARM_F_N;
			if (result & 0x100000000ULL)
				cpu->cd.arm.flags |= ARM_F_C;
			if (result & 0x80000000) {
				if ((tmp64 & 0x80000000) == 0 && (old & 0x80000000) == 0)
					cpu->cd.arm.flags |= ARM_F_V;
			} else {
				if ((tmp64 & 0x80000000) != 0 && (old & 0x80000000) != 0)
					cpu->cd.arm.flags |= ARM_F_V;
			}
		}
		break;

	case 0x2:
		// movs or cmp
		if (iw & 0x0800) {
			uint64_t old = cpu->cd.arm.r[rd8];
			uint64_t tmp64 = (uint32_t)(-(iw & 0xff));
			uint64_t result = old + tmp64;
			cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N | ARM_F_C | ARM_F_V);
			if (result == 0)
				cpu->cd.arm.flags |= ARM_F_Z;
			if ((int32_t)result < 0)
				cpu->cd.arm.flags |= ARM_F_N;
			if (result & 0x100000000ULL)
				cpu->cd.arm.flags |= ARM_F_C;
			if (result & 0x80000000) {
				if ((tmp64 & 0x80000000) == 0 && (old & 0x80000000) == 0)
					cpu->cd.arm.flags |= ARM_F_V;
			} else {
				if ((tmp64 & 0x80000000) != 0 && (old & 0x80000000) != 0)
					cpu->cd.arm.flags |= ARM_F_V;
			}
		} else {
			cpu->cd.arm.r[rd8] = iw & 0xff;
			cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
			if (cpu->cd.arm.r[rd8] == 0)
				cpu->cd.arm.flags |= ARM_F_Z;
		}
		break;

	case 0x3:
		// adds or sub
		{
			uint64_t old = cpu->cd.arm.r[rd8];
			uint64_t tmp64 = iw & 0xff;
			
			if (iw & 0x0800)
				tmp64 = (uint32_t)(-tmp64);

			uint64_t result = old + tmp64;
			cpu->cd.arm.r[rd8] = result;

			cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N | ARM_F_C | ARM_F_V);
			if (result == 0)
				cpu->cd.arm.flags |= ARM_F_Z;
			if ((int32_t)result < 0)
				cpu->cd.arm.flags |= ARM_F_N;
			if (result & 0x100000000ULL)
				cpu->cd.arm.flags |= ARM_F_C;
			if (result & 0x80000000) {
				if ((tmp64 & 0x80000000) == 0 && (old & 0x80000000) == 0)
					cpu->cd.arm.flags |= ARM_F_V;
			} else {
				if ((tmp64 & 0x80000000) != 0 && (old & 0x80000000) != 0)
					cpu->cd.arm.flags |= ARM_F_V;
			}
		}
		break;

	case 0x4:
		switch (op10) {
		case 0:
			// "DPIs":
			switch ((iw >> 6) & 15) {
			case 0:// ands
				cpu->cd.arm.r[rd] &= cpu->cd.arm.r[r3];
				cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
				if (cpu->cd.arm.r[rd] == 0)
					cpu->cd.arm.flags |= ARM_F_Z;
				if ((int32_t)cpu->cd.arm.r[rd] < 0)
					cpu->cd.arm.flags |= ARM_F_N;
				break;
			case 1:// eors
				cpu->cd.arm.r[rd] ^= cpu->cd.arm.r[r3];
				cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
				if (cpu->cd.arm.r[rd] == 0)
					cpu->cd.arm.flags |= ARM_F_Z;
				if ((int32_t)cpu->cd.arm.r[rd] < 0)
					cpu->cd.arm.flags |= ARM_F_N;
				break;
			case 7:// rors
				{
					int amount = cpu->cd.arm.r[r3] & 0xff;
					for (int i = 0; i < (amount & 31); ++i) {
						int c = cpu->cd.arm.r[rd] & 1;
						cpu->cd.arm.r[rd] >>= 1;
						cpu->cd.arm.flags &= ~ARM_F_C;
						if (c) {
							cpu->cd.arm.flags |= ARM_F_C;
							cpu->cd.arm.r[rd] |= 0x80000000;
						}
					}
					
					// Rotating right by e.g. 32 means that the C flag
					// should be updated, but the register isn't
					// really rotated.
					if (amount != 0 && (amount & 31) == 0) {
						cpu->cd.arm.flags &= ~ARM_F_C;
						if (cpu->cd.arm.r[rd] & 0x80000000)
							cpu->cd.arm.flags |= ARM_F_C;
					}
					
					if (cpu->cd.arm.r[rd] == 0)
						cpu->cd.arm.flags |= ARM_F_Z;
					if ((int32_t)cpu->cd.arm.r[rd] < 0)
						cpu->cd.arm.flags |= ARM_F_N;
				}
				break;
			case 8:// tst
				tmp = cpu->cd.arm.r[rd] & cpu->cd.arm.r[r3];
				cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
				if (tmp == 0)
					cpu->cd.arm.flags |= ARM_F_Z;
				if ((int32_t)tmp < 0)
					cpu->cd.arm.flags |= ARM_F_N;
				break;
			case 10:	// cmp
				{
					uint64_t old = cpu->cd.arm.r[rd];
					uint64_t tmp64 = (uint32_t) (-cpu->cd.arm.r[r3]);
					uint64_t result = old + tmp64;
					cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N | ARM_F_C | ARM_F_V);
					if (result == 0)
						cpu->cd.arm.flags |= ARM_F_Z;
					if ((int32_t)result < 0)
						cpu->cd.arm.flags |= ARM_F_N;
					if (result & 0x100000000ULL)
						cpu->cd.arm.flags |= ARM_F_C;
					if (result & 0x80000000) {
						if ((tmp64 & 0x80000000) == 0 && (old & 0x80000000) == 0)
							cpu->cd.arm.flags |= ARM_F_V;
					} else {
						if ((tmp64 & 0x80000000) != 0 && (old & 0x80000000) != 0)
							cpu->cd.arm.flags |= ARM_F_V;
					}
				}
				break;
			case 12:// orrs
				cpu->cd.arm.r[rd] |= cpu->cd.arm.r[r3];
				cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
				if (cpu->cd.arm.r[rd] == 0)
					cpu->cd.arm.flags |= ARM_F_Z;
				if ((int32_t)cpu->cd.arm.r[rd] < 0)
					cpu->cd.arm.flags |= ARM_F_N;
				break;
			case 13:// muls
				cpu->cd.arm.r[rd] *= cpu->cd.arm.r[r3];
				cpu->cd.arm.flags &= ~(ARM_F_Z | ARM_F_N);
				if (cpu->cd.arm.r[rd] == 0)
					cpu->cd.arm.flags |= ARM_F_Z;
				if ((int32_t)cpu->cd.arm.r[rd] < 0)
					cpu->cd.arm.flags |= ARM_F_N;
				break;
			default:
				debug("TODO: unimplemented DPI %i at pc 0x%08x\n", (iw >> 6) & 15, (int)cpu->pc);
				cpu->running = 0;
				return 0;
			}
			break;
		case 1:
			switch (op11_8) {
			case 6:
				if (hd)
					rd += 8;
				if (hm)
					r3 += 8;
				cpu->cd.arm.r[rd] = cpu->cd.arm.r[r3];
				if (r3 == ARM_PC) {
					if (cpu->pc & 1) {
						debug("TODO: double check with manual whether it is correct to use old pc + 8 here; at pc 0x%08x\n", (int)cpu->pc);
						cpu->running = 0;
						return 0;
					}
					cpu->cd.arm.r[rd] = cpu->pc + 8;
				}
				if (rd == ARM_PC) {
					cpu->pc = cpu->cd.arm.r[rd];
					cpu->cd.arm.cpsr |= ARM_FLAG_T;
					if (!(cpu->pc & 1))
						cpu->cd.arm.cpsr &= ~ARM_FLAG_T;
					cpu->pc = addr - sizeof(uint16_t);
				}
				break;
			case 7:
				if ((iw & 0xff87) == 0x4700) {
					//  bx
					int rm = (iw >> 3) & 15;
					//  Note: pc will be increased by 2 further down!
					cpu->pc = cpu->cd.arm.r[rm] - sizeof(uint16_t);
					if (!(cpu->pc & 1))
						cpu->cd.arm.cpsr &= ~ARM_FLAG_T;
					if (rm == ARM_LR && cpu->machine->show_trace_tree)
						cpu_functioncall_trace_return(cpu);
				} else {
					debug("TODO: unimplemented opcode 0x%x,%i at pc 0x%08x\n", main_opcode, op11_8, (int)cpu->pc);
					cpu->running = 0;
					return 0;
				}
				break;
			default:
				debug("TODO: unimplemented opcode 0x%x,%i at pc 0x%08x\n", main_opcode, op11_8, (int)cpu->pc);
				cpu->running = 0;
				return 0;
			}
			break;

		case 2:
		case 3:
			// PC-relative load.
			// Is this address calculation correct?
			// It works with real code, but is not the same as that of
			// http://engold.ui.ac.ir/~nikmehr/Appendix_B2.pdf
			tmp = (addr & ~3) + 4 + (iw & 0xff) * 4;

			if (!cpu->memory_rw(cpu, cpu->mem, tmp, &word[0],
					sizeof(word), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("arm_cpu_interpret_thumb_SLOW(): could not load pc-relative word\n");
				cpu->running = 0;
				return 0;
			}

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				tmp = word[0] + (word[1]<<8) + (word[2]<<16) + (word[3]<<24);
			else
				tmp = word[3] + (word[2]<<8) + (word[1]<<16) + (word[0]<<24);

			cpu->cd.arm.r[rd8] = tmp;
			break;

		default:
			debug("TODO: unimplemented opcode 0x%x,%i at pc 0x%08x\n", main_opcode, op10, (int)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0x6:
	case 0x7:
	case 0x8:
		// Load/Store with immediate offset.
		len = main_opcode == 6 ? 4 : (main_opcode == 7 ? 1 : 2);
		isLoad = iw & 0x0800;
		addr = (cpu->cd.arm.r[r3] + imm6 * len) & ~(len - 1);
		tmp = 0;

		if (!isLoad) {
			tmp = cpu->cd.arm.r[rd];

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				for (int i = 0; i < len; ++i)
					word[i] = (tmp >> (8*i));
			} else {
				for (int i = 0; i < len; ++i)
					word[len - 1 - i] = (tmp >> (8*i));
			}
		}

		if (!cpu->memory_rw(cpu, cpu->mem, addr, &word[0],
				len, isLoad ? MEM_READ : MEM_WRITE, CACHE_DATA)) {
			fatal("arm_cpu_interpret_thumb_SLOW(): could not load with immediate offset\n");
			cpu->running = 0;
			return 0;
		}

		if (isLoad) {
			tmp = 0;
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				for (int i = 0; i < len; ++i) {
					tmp <<= 8;
					tmp |= word[len - 1 - i];
				}
			} else {
				for (int i = 0; i < len; ++i) {
					tmp <<= 8;
					tmp |= word[i];
				}
			}

			cpu->cd.arm.r[rd] = tmp;
		}
		break;

	case 0x9:
		// sp-relative load or store
		if (iw & 0x0800) {
			addr = (cpu->cd.arm.r[ARM_SP] + (iw & 0xff) * 4) & ~3;
			if (!cpu->memory_rw(cpu, cpu->mem, addr, &word[0],
					sizeof(word), MEM_READ, CACHE_DATA)) {
				fatal("arm_cpu_interpret_thumb_SLOW(): could not load sp-relative word\n");
				cpu->running = 0;
				return 0;
			}

			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				tmp = word[0] + (word[1]<<8) + (word[2]<<16) + (word[3]<<24);
			else
				tmp = word[3] + (word[2]<<8) + (word[1]<<16) + (word[0]<<24);

			cpu->cd.arm.r[rd8] = tmp;
		} else {
			tmp = cpu->cd.arm.r[rd8];
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				for (size_t i = 0; i < sizeof(word); ++i)
					word[i] = (tmp >> (8*i));
			} else {
				for (size_t i = 0; i < sizeof(word); ++i)
					word[sizeof(word) - 1 - i] = (tmp >> (8*i));
			}

			addr = (cpu->cd.arm.r[ARM_SP] + (iw & 0xff) * 4) & ~3;
			if (!cpu->memory_rw(cpu, cpu->mem, addr, &word[0],
					sizeof(word), MEM_WRITE, CACHE_DATA)) {
				fatal("arm_cpu_interpret_thumb_SLOW(): could not store sp-relative word\n");
				cpu->running = 0;
				return 0;
			}
		}
		break;

	case 0xb:
		switch (op11_8) {
		case 0:
			if (iw & 0x0080)
				cpu->cd.arm.r[ARM_SP] -= ((iw & 0x7f) << 2);
			else
				cpu->cd.arm.r[ARM_SP] += ((iw & 0x7f) << 2);
			break;
		case 4:
		case 5:
			/*  push, i.e. stmdb sp!, reglist  */
			arm_push(cpu, &cpu->cd.arm.r[ARM_SP], 1, 0, 0, 1, (iw & 0xff) |
				(iw & 0x100 ? (1 << ARM_LR) : 0));
			break;
		case 12:
		case 13:
			/*  pop, i.e. ldmia sp!, reglist  */
			cpu->cd.arm.r[ARM_PC] = cpu->pc;
			arm_pop(cpu, &cpu->cd.arm.r[ARM_SP], 0, 1, 0, 1, (iw & 0xff) |
				(iw & 0x100 ? (1 << ARM_PC) : 0));
			cpu->pc = cpu->cd.arm.r[ARM_PC];
			if (cpu->pc & 1)
				cpu->cd.arm.cpsr |= ARM_FLAG_T;
			else
				cpu->cd.arm.cpsr &= ~ARM_FLAG_T;
			// If not popping pc, make sure to move to the next instruction:
			if (!(iw & 0x100))
				cpu->pc += sizeof(uint16_t);
			return 1;
		default:
			debug("TODO: unimplemented opcode 0x%x,%i at pc 0x%08x\n", main_opcode, op11_8, (int)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xd:
		if (condition_code < 0xe) {
			// Conditional branch.
			tmp = (iw & 0xff) << 1;
			if (tmp & 0x100)
				tmp |= 0xfffffe00;
			tmp = (int32_t)(cpu->pc + 4 + tmp);

			switch (condition_code) {
			case 0x0:	// eq:
					t = cpu->cd.arm.flags & ARM_F_Z;
					break;
			case 0x1:	// ne:
					t = !(cpu->cd.arm.flags & ARM_F_Z);
					break;
			case 0x2:	// cs:
					t = cpu->cd.arm.flags & ARM_F_C;
					break;
			case 0x3:	// cc:
					t = !(cpu->cd.arm.flags & ARM_F_C);
					break;
			case 0x4:	// mi:
					t = cpu->cd.arm.flags & ARM_F_N;
					break;
			case 0x5:	// pl:
					t = !(cpu->cd.arm.flags & ARM_F_N);
					break;
			case 0x6:	// vs:
					t = cpu->cd.arm.flags & ARM_F_V;
					break;
			case 0x7:	// vc:
					t = !(cpu->cd.arm.flags & ARM_F_V);
					break;
			case 0x8:	// hi:
					t = condition_hi[cpu->cd.arm.flags];
					break;
			case 0x9:	// ls:
					t = !condition_hi[cpu->cd.arm.flags];
					break;
			case 0xa:	// ge:
					t = condition_ge[cpu->cd.arm.flags];
					break;
			case 0xb:	// lt:
					t = !condition_ge[cpu->cd.arm.flags];
					break;
			case 0xc:	// gt:
					t = condition_gt[cpu->cd.arm.flags];
					break;
			case 0xd:	// le:
					t = !condition_gt[cpu->cd.arm.flags];
					break;
			}

			if (t) {
				cpu->pc = tmp;
				return 1;
			}
		} else if (condition_code == 0xf) {
			arm_exception(cpu, ARM_EXCEPTION_SWI);
			return 1;
		} else {
			debug("TODO: unimplemented opcode 0x%x, non-branch, at pc 0x%08x\n", main_opcode, (int)cpu->pc);
			cpu->running = 0;
			return 0;
		}
		break;

	case 0xe:
		if (iw & 0x0800) {
			// blx
			addr = (cpu->pc + 4 + (cpu->cd.arm.tmp_branch + (((iw >> 1) & 0x3ff) << 2))) & ~3;

			if (iw & 1) {
				fatal("lowest bit set in thumb blx instruction?\n");
				cpu->running = 0;
				return 0;
			}

			cpu->cd.arm.r[ARM_LR] = cpu->pc + 2;
			cpu->pc = addr;
			if (cpu->machine->show_trace_tree)
				cpu_functioncall_trace(cpu, cpu->pc);
			cpu->cd.arm.cpsr &= ~ARM_FLAG_T;
			return 1;
		} else {
			// b
			tmp = (iw & 0x7ff) << 1;
			if (tmp & 0x800)
				tmp |= 0xfffff000;
			cpu->pc = (int32_t)(cpu->pc + 4 + tmp);
			return 1;
		}
		break;

	case 0xf:
		if (iw & 0x0800) {
			// bl
			addr = (cpu->pc + 2 + (cpu->cd.arm.tmp_branch + ((iw & 0x7ff) << 1)));
			cpu->cd.arm.r[ARM_LR] = cpu->pc + 2;
			cpu->pc = addr;
			if (cpu->machine->show_trace_tree)
				cpu_functioncall_trace(cpu, cpu->pc);
			return 1;
		} else {
			// "branch prefix".
			uint32_t tmp32 = iw & 0x07ff;
			if (tmp32 & 0x0400)
				tmp32 |= 0xfffff800;
			tmp32 <<= 12;
			cpu->cd.arm.tmp_branch = tmp32;
		}
		break;

	default:
		debug("TODO: unimplemented opcode 0x%x at pc 0x%08x\n", main_opcode, (int)cpu->pc);
		cpu->running = 0;
		return 0;
	}

	cpu->pc += sizeof(uint16_t);

	return 1;
}


/*
 *  arm_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *              
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */                     
int arm_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	uint32_t iw, tmp;
	int main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int i, n, p_bit, u_bit, b_bit, w_bit, l_bit;
	const char *symbol, *condition;
	uint64_t offset;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr & ~1);

	if (dumpaddr & 1)
		return arm_cpu_disassemble_instr_thumb(cpu, ib, running, dumpaddr);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);
	debug("%08x\t", (int)iw);

	condition = arm_condition_string[iw >> 28];
	main_opcode = (iw >> 24) & 15;
	secondary_opcode = (iw >> 21) & 15;
	u_bit = (iw >> 23) & 1;
	b_bit = (iw >> 22) & 1;
	w_bit = (iw >> 21) & 1;
	s_bit = l_bit = (iw >> 20) & 1;
	r16 = (iw >> 16) & 15;
	r12 = (iw >> 12) & 15;
	r8 = (iw >> 8) & 15;

	if (iw == 0xf10c0040) {
		debug("cpsid\tf\n");
		return sizeof(uint32_t);
	} else if (iw == 0xf10c0080) {
		debug("cpsid\ti\n");
		return sizeof(uint32_t);
	} else if (iw == 0xf57ff04f) {
		debug("dsb\tsy\n");
		return sizeof(uint32_t);
	} else if (iw == 0xf57ff05f) {
		debug("dmb\tsy\n");
		return sizeof(uint32_t);
	} else if (iw == 0xf57ff06f) {
		debug("isb\tsy\n");
		return sizeof(uint32_t);
	} else if ((iw >> 28) == 0xf) {
		switch (main_opcode) {
		case 0xa:
		case 0xb:
			tmp = (iw & 0xffffff);
			if (tmp & 0x800000)
				tmp |= 0xff000000;
			tmp = (int32_t)(dumpaddr + 8 + 4*tmp + (main_opcode == 0xb? 2 : 0)) + 1;
			debug("blx\t0x%x", (int)tmp);
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmp, &offset);
			if (symbol != NULL)
				debug(" \t<%s>", symbol);
			debug("\n");
			break;
		default:debug("UNIMPLEMENTED\n");
		}
		return sizeof(uint32_t);
	}

	switch (main_opcode) {
	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*
		 *  Special cases first:
		 */

		/*
		 *  ldrex: Load Register Exclusive
		 *  strex: Store Register Exclusive
		 */
		if ((iw & 0x0ff00fff) == 0x01900f9f) {
			/*  ldrex rt[,rn]:  */
			debug("ldrex%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[r16]);
			debug("\n");
			break;
		}
		if ((iw & 0x0ff00ff0) == 0x01800f90) {
			/*  strex rd,rt[,rn]:  */
			debug("strex%s\t%s,%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15],
				arm_regname[r16]);
			debug("\n");
			break;
		}

		/*
		 *  Multiplication:
		 *  xxxx0000 0110dddd aaaammmm 1001nnnn  mls rd,rn,rm,ra
		 */
		if ((iw & 0x0ff000f0) == 0x00600090) {
			debug("mls%s\t", condition);
			debug("%s,", arm_regname[r16]);
			debug("%s,", arm_regname[iw & 15]);
			debug("%s,", arm_regname[r8]);
			debug("%s", arm_regname[r12]);
			debug("\n");
			break;
		}

		/*
		 *  Multiplication:
		 *  xxxx0000 00ASdddd nnnnssss 1001mmmm  (Rd, Rm, Rs [,Rn])
		 */
		if ((iw & 0x0fc000f0) == 0x00000090) {
			int a_bit = (iw >> 21) & 1;
			debug("%s%s%s\t", a_bit? "mla" : "mul",
			    condition, s_bit? "s" : "");
			debug("%s,", arm_regname[r16]);
			debug("%s,", arm_regname[iw & 15]);
			debug("%s", arm_regname[r8]);
			if (a_bit)
				debug(",%s", arm_regname[r12]);
			debug("\n");
			break;
		}

		/*
		 *  Long multiplication:
		 *  xxxx0000 1UAShhhh llllssss 1001mmmm  (Rl,Rh,Rm,Rs)
		 */
		if ((iw & 0x0f8000f0) == 0x00800090) {
			int a_bit = (iw >> 21) & 1;
			u_bit = (iw >> 22) & 1;
			debug("%s%sl%s%s\t", u_bit? "s" : "u",
			    a_bit? "mla" : "mul", condition, s_bit? "s" : "");
			debug("%s,%s,", arm_regname[r12], arm_regname[r16]);
			debug("%s,%s\n", arm_regname[iw&15], arm_regname[r8]);
			break;
		}

		/*
		 *  xxxx0001 0000nnnn dddd0000 0101mmmm  qadd Rd,Rm,Rn
		 *  xxxx0001 0010nnnn dddd0000 0101mmmm  qsub Rd,Rm,Rn
		 *  xxxx0001 0100nnnn dddd0000 0101mmmm  qdadd Rd,Rm,Rn
		 *  xxxx0001 0110nnnn dddd0000 0101mmmm  qdsub Rd,Rm,Rn
		 */
		if ((iw & 0x0f900ff0) == 0x01000050) {
			debug("q%s%s%s\t", iw & 0x400000? "d" : "",
			    iw & 0x200000? "sub" : "add", condition);
			debug("%s,%s,%s\n", arm_regname[r12],
			    arm_regname[iw&15], arm_regname[r16]);
			break;
		}

		/*
		 *  xxxx0001 0010.... ........ 00L1mmmm  bx/blx rm
		 */
		if ((iw & 0x0ff000d0) == 0x01200010) {
			l_bit = iw & 0x20;
			debug("b%sx%s\t%s\n", l_bit? "l" : "", condition,
			    arm_regname[iw & 15]);
			break;
		}

		/*
		 *  xxxx0001 0s10aaaa 11110000 0000mmmm  MSR  Regform
		 *  xxxx0011 0s10aaaa 1111rrrr bbbbbbbb  MSR  Immform
		 *  xxxx0001 0s001111 dddd0000 00000000  MRS
		 */
		if ((iw & 0x0fb0fff0) == 0x0120f000 ||
		    (iw & 0x0fb0f000) == 0x0320f000) {
			debug("msr%s\t%s", condition, (iw&0x400000)? "S":"C");
			debug("PSR_");
			if (iw & (1<<19)) debug("f");
			if (iw & (1<<18)) debug("s");
			if (iw & (1<<17)) debug("x");
			if (iw & (1<<16)) debug("c");
			if (iw & 0x02000000) {
				int r = (iw >> 7) & 30;
				uint32_t b = iw & 0xff;
				while (r-- > 0)
					b = (b >> 1) | ((b & 1) << 31);
				debug(",#0x%x\n", b);
			} else
				debug(",%s\n", arm_regname[iw & 15]);
			break;
		}
		if ((iw & 0x0fbf0fff) == 0x010f0000) {
			debug("mrs%s\t", condition);
			debug("%s,%sPSR\n", arm_regname[r12],
			    (iw&0x400000)? "S":"C");
			break;
		}

		/*
		 *  xxxx0001 0B00nnnn dddd0000 1001mmmm    SWP Rd,Rm,[Rn]
		 */
		if ((iw & 0x0fb00ff0) == 0x01000090) {
			debug("swp%s%s\t", condition, (iw&0x400000)? "b":"");
			debug("%s,%s,[%s]\n", arm_regname[r12],
			    arm_regname[iw & 15], arm_regname[r16]);
			break;
		}

		/*
		 *  xxxx0001 0010iiii iiiiiiii 0111iiii    BKPT immed16
		 */
		if ((iw & 0x0ff000f0) == 0x01200070) {
			debug("bkpt%s\t0x%04x\n", condition,
			    ((iw & 0x000fff00) >> 4) + (iw & 0xf));
			break;
		}

		/*
		 *  xxxx0001 01101111 dddd1111 0001mmmm    CLZ Rd,Rm
		 */
		if ((iw & 0x0fff0ff0) == 0x016f0f10) {
			debug("clz%s\t", condition);
			debug("%s,%s\n", arm_regname[r12], arm_regname[iw&15]);
			break;
		}

		/*
		 *  xxxx0011 0000mmm ddddmmm mmmmmmm    MOVW Rd,imm
		 */
		if ((iw & 0x0ff00000) == 0x03000000) {
			debug("movw%s\t", condition);
			debug("%s,#%i\n", arm_regname[r12],
				((iw & 0xf0000) >> 4) | (iw & 0xfff));
			break;
		}

		/*
		 *  xxxx0011 0100mmm ddddmmm mmmmmmm    MOVT Rd,imm
		 */
		if ((iw & 0x0ff00000) == 0x03400000) {
			debug("movt%s\t", condition);
			debug("%s,#%i\n", arm_regname[r12],
				((iw & 0xf0000) >> 4) | (iw & 0xfff));
			break;
		}

		/*
		 *  xxxx0001 0000dddd nnnnssss 1yx0mmmm  SMLAxy Rd,Rm,Rs,Rn
		 *  xxxx0001 0100dddd DDDDssss 1yx0mmmm  SMLALxy RdL,RdH,Rm,Rs
		 *  xxxx0001 0010dddd nnnnssss 1y00mmmm  SMLAWy Rd,Rm,Rs,Rn
		 *  xxxx0001 0110dddd 0000ssss 1yx0mmmm  SMULxy Rd,Rm,Rs
		 *  xxxx0001 0010dddd 0000ssss 1y10mmmm  SMULWy Rd,Rm,Rs
		 */
		if ((iw & 0x0ff00090) == 0x01000080) {
			debug("smla%s%s%s\t",
			    iw & 0x20? "t" : "b", iw & 0x40? "t" : "b",
			    condition);
			debug("%s,%s,%s,%s\n", arm_regname[r16],
			    arm_regname[iw&15], arm_regname[r8],
			    arm_regname[r12]);
			break;
		}
		if ((iw & 0x0ff00090) == 0x01400080) {
			debug("smlal%s%s%s\t",
			    iw & 0x20? "t" : "b", iw & 0x40? "t" : "b",
			    condition);
			debug("%s,%s,%s,%s\n", arm_regname[r12],
			    arm_regname[r16], arm_regname[iw&15],
			    arm_regname[r8]);
			break;
		}
		if ((iw & 0x0ff000b0) == 0x01200080) {
			debug("smlaw%s%s\t", iw & 0x40? "t" : "b",
			    condition);
			debug("%s,%s,%s,%s\n", arm_regname[r16],
			    arm_regname[iw&15], arm_regname[r8],
			    arm_regname[r12]);
			break;
		}
		if ((iw & 0x0ff0f090) == 0x01600080) {
			debug("smul%s%s%s\t",
			    iw & 0x20? "t" : "b", iw & 0x40? "t" : "b",
			    condition);
			debug("%s,%s,%s\n", arm_regname[r16],
			    arm_regname[iw&15], arm_regname[r8]);
			break;
		}
		if ((iw & 0x0ff0f0b0) == 0x012000a0) {
			debug("smulw%s%s\t", iw & 0x40? "t" : "b",
			    condition);
			debug("%s,%s,%s\n", arm_regname[r16],
			    arm_regname[iw&15], arm_regname[r8]);
			break;
		}

		/*
		 *  xxxx000P U1WLnnnn ddddHHHH 1SH1LLLL load/store rd,imm(rn)
		 */
		if ((iw & 0x0e000090) == 0x00000090) {
			const char *op = "st";
			int imm = ((iw >> 4) & 0xf0) | (iw & 0xf);
			int regform = !(iw & 0x00400000);
			p_bit = main_opcode & 1;
			/*
			 *  TODO: detect some illegal variants:
			 *  signed store,  or  unsigned byte load/store
			 */
			if (!l_bit && (iw & 0xd0) == 0xd0 && (r12 & 1)) {
				debug("TODO: r12 odd, not load/store\n");
				break;
			}
			/*  Semi-generic case:  */
			if (iw & 0x00100000)
				op = "ld";
			if (!l_bit && (iw & 0xd0) == 0xd0) {
				if (iw & 0x20)
					op = "st";
				else
					op = "ld";
			}
			debug("%sr%s", op, condition);
			if (!l_bit && (iw & 0xd0) == 0xd0) {
				debug("d");		/*  Double-register  */
			} else {
				if (iw & 0x40)
					debug("s");	/*  signed  */
				if (iw & 0x20)
					debug("h");	/*  half-word  */
				else
					debug("b");	/*  byte  */
			}
			debug("\t%s,[%s", arm_regname[r12], arm_regname[r16]);
			if (p_bit) {
				/*  Pre-index:  */
				if (regform)
					debug(",%s%s", u_bit? "" : "-",
					    arm_regname[iw & 15]);
				else {
					if (imm != 0)
						debug(",#%s%i", u_bit? "" : "-",
						    imm);
				}
				debug("]%s\n", w_bit? "!" : "");
			} else {
				/*  Post-index:  */
				debug("],");
				if (regform)
					debug("%s%s\n", u_bit? "" : "-",
					    arm_regname[iw & 15]);
				else
					debug("#%s%i\n", u_bit? "" : "-", imm);
			}
			break;
		}

		/*  Other special cases:  */
		if (iw & 0x80 && !(main_opcode & 2) && iw & 0x10) {
			debug("UNIMPLEMENTED reg (c!=0), t odd\n");
			break;
		}

		/*
		 *  Generic Data Processing Instructions:
		 *
		 *  xxxx000a aaaSnnnn ddddcccc ctttmmmm  Register form
		 *  xxxx001a aaaSnnnn ddddrrrr bbbbbbbb  Immediate form
		 */

		debug("%s%s%s\t", arm_dpiname[secondary_opcode],
		    condition, s_bit? "s" : "");
		if (arm_dpi_uses_d[secondary_opcode])
			debug("%s,", arm_regname[r12]);
		if (arm_dpi_uses_n[secondary_opcode])
			debug("%s,", arm_regname[r16]);

		if (main_opcode & 2) {
			/*  Immediate form:  */
			int r = (iw >> 7) & 30;
			uint32_t b = iw & 0xff;
			while (r-- > 0)
				b = (b >> 1) | ((b & 1) << 31);
			if (b < 15)
				debug("#%i", b);
			else
				debug("#0x%x", b);
		} else {
			/*  Register form:  */
			int t = (iw >> 4) & 7;
			int c = (iw >> 7) & 31;
			debug("%s", arm_regname[iw & 15]);
			switch (t) {
			case 0:	if (c != 0)
					debug(", lsl #%i", c);
				break;
			case 1:	debug(", lsl %s", arm_regname[c >> 1]);
				break;
			case 2:	debug(", lsr #%i", c? c : 32);
				break;
			case 3:	debug(", lsr %s", arm_regname[c >> 1]);
				break;
			case 4:	debug(", asr #%i", c? c : 32);
				break;
			case 5:	debug(", asr %s", arm_regname[c >> 1]);
				break;
			case 6:	if (c != 0)
					debug(", ror #%i", c);
				else
					debug(", rrx");
				break;
			case 7:	debug(", ror %s", arm_regname[c >> 1]);
				break;
			}

			/*  mov pc,reg:  */
			if (running && t == 0 && c == 0 && secondary_opcode
			    == 0xd && r12 == ARM_PC && (iw&15)!=ARM_PC) {
				symbol = get_symbol_name(&cpu->machine->
				    symbol_context, cpu->cd.arm.r[iw & 15],
				    &offset);
				if (symbol != NULL)
					debug(" \t<%s>", symbol);
			}
		}
		debug("\n");
		break;
	case 0x4:				/*  Single Data Transfer  */
	case 0x5:
	case 0x6:
	case 0x7:
		/*  Special cases first:  */
		if ((iw & 0xfc70f000) == 0xf450f000) {
			/*  Preload:  */
			debug("pld\t[%s]\n", arm_regname[r16]);
			break;
		}

		if ((iw & 0x0fff0ff0) == 0x06bf0f30) {
			/*  rev rd,rm:  */
			debug("rev%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			debug("\n");
			break;
		}

		if ((iw & 0x0fff03f0) == 0x06bf0070) {
			/*  sxth rd,rm[,rot]:  */
			debug("sxth%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			int rot = ((iw & 0xc00) >> 10) << 3;
			if (rot != 0)
				debug(",%i", rot);
			debug("\n");
			break;
		}

		if ((iw & 0x0fff03f0) == 0x06ef0070) {
			/*  uxtb rd,rm[,rot]:  */
			debug("uxtb%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			int rot = ((iw & 0xc00) >> 10) << 3;
			if (rot != 0)
				debug(",%i", rot);
			debug("\n");
			break;
		}

		// Note: uxtab is decoded AFTER uxtb.
		if ((iw & 0x0ff003f0) == 0x06e00070) {
			/*  uxtab rd,rn,rm[,rot]:  */
			debug("uxtab%s\t%s,%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[r16],
				arm_regname[iw & 15]);
			int rot = ((iw & 0xc00) >> 10) << 3;
			if (rot != 0)
				debug(",%i", rot);
			debug("\n");
			break;
		}

		if ((iw & 0x0fff03f0) == 0x06ff0070) {
			/*  uxth rd,rm[,rot]:  */
			debug("uxth%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			int rot = ((iw & 0xc00) >> 10) << 3;
			if (rot != 0)
				debug(",%i", rot);
			debug("\n");
			break;
		}

		// Note: uxtah is decoded AFTER uxth.
		if ((iw & 0x0ff003f0) == 0x06f00070) {
			/*  uxtah rd,rn,rm[,rot]:  */
			debug("uxtah%s\t%s,%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[r16],
				arm_regname[iw & 15]);
			int rot = ((iw & 0xc00) >> 10) << 3;
			if (rot != 0)
				debug(",%i", rot);
			debug("\n");
			break;
		}

		if ((iw & 0x0fe00070) == 0x07c00010) {
			/*  bfi rd,rn,#lsb,#width:  */
			debug("bfi%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			int lsb = (iw >> 7) & 31;
			int msb = (iw >> 16) & 31;
			int width = msb - lsb + 1;
			debug(",%i", lsb);
			debug(",%i", width);
			debug("\n");
			break;
		}

		if ((iw & 0x0fe00070) == 0x07e00050) {
			/*  ubfx rd,rn,#lsb,#width:  */
			debug("ubfx%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			debug(",%i", (iw >> 7) & 31);
			debug(",%i", 1 + ((iw >> 16) & 31));
			debug("\n");
			break;
		}

		if ((iw & 0x0fe00070) == 0x07a00050) {
			/*  sbfx rd,rn,#lsb,#width:  */
			debug("sbfx%s\t%s,%s",
				condition,
				arm_regname[r12],
				arm_regname[iw & 15]);
			debug(",%i", (iw >> 7) & 31);
			debug(",%i", 1 + ((iw >> 16) & 31));
			debug("\n");
			break;
		}

		if ((iw & 0x0fffffff) == 0x07f001f2) {
			debug("linux_bug%s\t; see https://github.com/torvalds/linux/blob/master/arch/arm/include/asm/bug.h\n",
				condition);
			break;
		}


		/*
		 *  xxxx010P UBWLnnnn ddddoooo oooooooo  Immediate form
		 *  xxxx011P UBWLnnnn ddddcccc ctt0mmmm  Register form
		 */
		p_bit = main_opcode & 1;
		if (main_opcode >= 6 && iw & 0x10) {
			debug("TODO: single data transf. but 0x10\n");
			break;
		}
		debug("%s%s%s", l_bit? "ldr" : "str",
		    condition, b_bit? "b" : "");
		if (!p_bit && w_bit)
			debug("t");
		debug("\t%s,[%s", arm_regname[r12], arm_regname[r16]);
		if ((iw & 0x0e000000) == 0x04000000) {
			/*  Immediate form:  */
			uint32_t imm = iw & 0xfff;
			if (!p_bit)
				debug("]");
			if (imm != 0)
				debug(",#%s%i", u_bit? "" : "-", imm);
			if (p_bit)
				debug("]");
		} else if ((iw & 0x0e000010) == 0x06000000) {
			/*  Register form:  */
			if (!p_bit)
				debug("]");
			if ((iw & 0xfff) != 0)
				debug(",%s%s", u_bit? "" : "-",
				    arm_regname[iw & 15]);
			if ((iw & 0xff0) != 0x000) {
				int c = (iw >> 7) & 31;
				int t = (iw >> 4) & 7;
				switch (t) {
				case 0:	if (c != 0)
						debug(", lsl #%i", c);
					break;
				case 2:	debug(", lsr #%i", c? c : 32);
					break;
				case 4:	debug(", asr #%i", c? c : 32);
					break;
				case 6:	if (c != 0)
						debug(", ror #%i", c);
					else
						debug(", rrx");
					break;
				}
			}
			if (p_bit)
				debug("]");
		} else {
			debug("UNKNOWN\n");
			break;
		}
		debug("%s", (p_bit && w_bit)? "!" : "");
		if ((iw & 0x0f000000) == 0x05000000 &&
		    (r16 == ARM_PC || running)) {
			unsigned char tmpw[4];
			uint32_t imm = iw & 0xfff;
			uint32_t addr = (u_bit? imm : -imm);
			if (r16 == ARM_PC)
				addr += dumpaddr + 8;
			else
				addr += cpu->cd.arm.r[r16];
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    addr, &offset);
			if (symbol != NULL)
				debug(" \t<%s", symbol);
			else
				debug(" \t<0x%08x", addr);
			if ((l_bit && cpu->memory_rw(cpu, cpu->mem, addr, tmpw,
			    b_bit? 1 : sizeof(tmpw), MEM_READ, NO_EXCEPTIONS))
			    || (!l_bit && running)) {
				if (l_bit) {
					if (cpu->byte_order ==
					    EMUL_LITTLE_ENDIAN)
						addr = tmpw[0] +(tmpw[1] << 8) +
						    (tmpw[2]<<16)+(tmpw[3]<<24);
					else
						addr = tmpw[3] + (tmpw[2]<<8) +
						    (tmpw[1]<<16)+(tmpw[0]<<24);
				} else {
					tmpw[0] = addr = cpu->cd.arm.r[r12];
					if (r12 == ARM_PC)
						addr = cpu->pc + 8;
				}
				debug(": ");
				if (b_bit)
					debug("%i", tmpw[0]);
				else {
					symbol = get_symbol_name(&cpu->machine->
					    symbol_context, addr, &offset);
					if (symbol != NULL)
						debug("%s", symbol);
					else if ((int32_t)addr > -256 &&
						    (int32_t)addr < 256)
						debug("%i", addr);
					else
						debug("0x%x", addr);
				}
			}
			debug(">");
		}
		debug("\n");
		break;
	case 0x8:				/*  Block Data Transfer  */
	case 0x9:
		/*  xxxx100P USWLnnnn llllllll llllllll  */
		p_bit = main_opcode & 1;
		s_bit = b_bit;
		debug("%s%s", l_bit? "ldm" : "stm", condition);
		switch (u_bit * 2 + p_bit) {
		case 0:	debug("da"); break;
		case 1:	debug("db"); break;
		case 2:	debug("ia"); break;
		case 3:	debug("ib"); break;
		}
		debug("\t%s", arm_regname[r16]);
		if (w_bit)
			debug("!");
		debug(",{");
		n = 0;
		for (i=0; i<16; i++)
			if ((iw >> i) & 1) {
				debug("%s%s", (n > 0)? ",":"", arm_regname[i]);
				n++;
			}
		debug("}");
		if (s_bit)
			debug("^");
		debug("\n");
		break;
	case 0xa:				/*  B: branch  */
	case 0xb:				/*  BL: branch and link  */
		debug("b%s%s\t", main_opcode == 0xa? "" : "l", condition);
		tmp = (iw & 0x00ffffff) << 2;
		if (tmp & 0x02000000)
			tmp |= 0xfc000000;
		tmp = (int32_t)(dumpaddr + tmp + 8);
		debug("0x%x", (int)tmp);
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    tmp, &offset);
		if (symbol != NULL)
			debug(" \t<%s>", symbol);
		debug("\n");
		break;
	case 0xc:				/*  Coprocessor  */
	case 0xd:				/*  LDC/STC  */
		/*
		 *  xxxx1100 0100nnnn ddddcccc oooommmm    MCRR c,op,Rd,Rn,CRm
		 *  xxxx1100 0101nnnn ddddcccc oooommmm    MRRC c,op,Rd,Rn,CRm
		 */
		if ((iw & 0x0fe00fff) == 0x0c400000) {
			debug("%s%s\t", iw & 0x100000? "mra" : "mar",
			    condition);
			if (iw & 0x100000)
				debug("%s,%s,acc0\n",
				    arm_regname[r12], arm_regname[r16]);
			else
				debug("acc0,%s,%s\n",
				    arm_regname[r12], arm_regname[r16]);
			break;
		}
		if ((iw & 0x0fe00000) == 0x0c400000) {
			debug("%s%s\t", iw & 0x100000? "mrrc" : "mcrr",
			    condition);
			debug("%i,%i,%s,%s,cr%i\n", r8, (iw >> 4) & 15,
			    arm_regname[r12], arm_regname[r16], iw & 15);
			break;
		}

		/*  xxxx110P UNWLnnnn DDDDpppp oooooooo LDC/STC  */
		debug("TODO: coprocessor LDC/STC\n");
		break;
	case 0xe:				/*  CDP (Coprocessor Op)  */
		/*				    or MRC/MCR!
		 *  xxxx1110 oooonnnn ddddpppp qqq0mmmm		CDP
		 *  xxxx1110 oooLNNNN ddddpppp qqq1MMMM		MRC/MCR
		 */
		if ((iw & 0x0ff00ff0) == 0x0e200010) {
			/*  Special case: mia* DSP instructions  */
			switch ((iw >> 16) & 0xf) {
			case  0: debug("mia"); break;
			case  8: debug("miaph"); break;
			case 12: debug("miaBB"); break;
			case 13: debug("miaTB"); break;
			case 14: debug("miaBT"); break;
			case 15: debug("miaTT"); break;
			default: debug("UNKNOWN mia vector instruction?");
			}
			debug("%s\t", condition);
			debug("acc%i,%s,%s\n", ((iw >> 5) & 7),
			    arm_regname[iw & 15], arm_regname[r12]);
			break;
		}
		if (iw & 0x10) {
			debug("%s%s\t",
			    (iw & 0x00100000)? "mrc" : "mcr", condition);
			debug("%i,%i,r%i,cr%i,cr%i,%i",
			    (int)((iw >> 8) & 15), (int)((iw >>21) & 7),
			    (int)((iw >>12) & 15), (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15), (int)((iw >> 5) & 7));
		} else {
			debug("cdp%s\t", condition);
			debug("%i,%i,cr%i,cr%i,cr%i",
			    (int)((iw >> 8) & 15),
			    (int)((iw >>20) & 15),
			    (int)((iw >>12) & 15),
			    (int)((iw >>16) & 15),
			    (int)((iw >> 0) & 15));
			if ((iw >> 5) & 7)
				debug(",0x%x", (int)((iw >> 5) & 7));
		}
		debug("\n");
		break;
	case 0xf:				/*  SWI  */
		debug("swi%s\t", condition);
		debug("0x%x\n", (int)(iw & 0x00ffffff));
		break;
	default:debug("UNIMPLEMENTED\n");
	}

	return sizeof(uint32_t);
}


/*****************************************************************************/


/*
 *  arm_mcr_mrc():
 *
 *  Coprocessor register move.
 *
 *  The program counter should be synched before calling this function (to
 *  make debug output with the correct PC value possible).
 */
void arm_mcr_mrc(struct cpu *cpu, uint32_t iword)
{
	int opcode1 = (iword >> 21) & 7;
	int l_bit = (iword >> 20) & 1;
	int crn = (iword >> 16) & 15;
	int rd = (iword >> 12) & 15;
	int cp_num = (iword >> 8) & 15;
	int opcode2 = (iword >> 5) & 7;
	int crm = iword & 15;

	if (cpu->cd.arm.coproc[cp_num] != NULL)
		cpu->cd.arm.coproc[cp_num](cpu, opcode1, opcode2, l_bit,
		    crn, crm, rd);
	else {
		fatal("[ TODO: arm_mcr_mrc: pc=0x%08x, iword=0x%08x: "
		    "cp_num=%i ]\n", (int)cpu->pc, iword, cp_num);

		// arm_exception(cpu, ARM_EXCEPTION_UND);
		/*  exit(1);  */
	}
}


/*
 *  arm_cdp():
 *
 *  Coprocessor operations.
 *
 *  The program counter should be synched before calling this function (to
 *  make debug output with the correct PC value possible).
 */
void arm_cdp(struct cpu *cpu, uint32_t iword)
{
	fatal("[ arm_cdp: pc=0x%08x, iword=0x%08x ]\n", (int)cpu->pc, iword);
	arm_exception(cpu, ARM_EXCEPTION_UND);
	/*  exit(1);  */
}


/*****************************************************************************/


#include "tmp_arm_tail.cc"

