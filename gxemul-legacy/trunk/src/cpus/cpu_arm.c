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
 *  $Id: cpu_arm.c,v 1.4 2005-08-30 21:47:43 debug Exp $
 *
 *  ARM CPU emulation.
 *
 *  A good source of quick info on ARM instruction encoding:
 *
 *	http://www.pinknoise.demon.co.uk/ARMinstrs/ARMinstrs.html
 *
 *  (Most "xxxx0101..." and similar strings in this file are from that URL,
 *  or from the ARM manual.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "symbol.h"

#define DYNTRANS_32
#include "tmp_arm_head.c"


/*  ARM symbolic register names and condition strings:  */
static char *arm_regname[N_ARM_REGS] = ARM_REG_NAMES;
static char *arm_condition_string[16] = ARM_CONDITION_STRINGS;

/*  Data Processing Instructions:  */
static char *arm_dpiname[16] = ARM_DPI_NAMES;
static int arm_dpi_uses_d[16] = { 1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1 };
static int arm_dpi_uses_n[16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0 };


/*
 *  arm_cpu_new():
 *
 *  Create a new ARM cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid ARM processor.
 */
int arm_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int any_cache = 0, i, found;
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

	cpu->memory_rw = arm_memory_rw;
	cpu->update_translation_table = arm_update_translation_table;
	cpu->invalidate_translation_caches_paddr =
	    arm_invalidate_translation_caches_paddr;
	cpu->invalidate_code_translation = arm_invalidate_code_translation;
	cpu->translate_address = arm_translate_address;

	cpu->cd.arm.cpu_type    = cpu_type_defs[found];
	cpu->name               = cpu->cd.arm.cpu_type.name;
	cpu->is_32bit           = 1;

	cpu->cd.arm.cpsr = ARM_FLAG_I | ARM_FLAG_F | ARM_MODE_USR32;
	cpu->cd.arm.control = ARM_CONTROL_PROG32 | ARM_CONTROL_DATA32
	    | ARM_CONTROL_CACHE | ARM_CONTROL_ICACHE | ARM_CONTROL_ALIGN;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
		if (cpu->cd.arm.cpu_type.icache_shift != 0)
			any_cache = 1;
		if (cpu->cd.arm.cpu_type.dcache_shift != 0)
			any_cache = 1;
		if (any_cache) {
			debug(" (I+D = %i+%i KB",
			    (int)(1 << (cpu->cd.arm.cpu_type.icache_shift-10)),
			    (int)(1 << (cpu->cd.arm.cpu_type.dcache_shift-10)));
			debug(")");
		}
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

	if (cpu->machine->userland_emul != NULL) {
		fatal("arm_setup_initial_translation_table(): should not "
		    "be called for userland emulation!\n");
		exit(1);
	}

	cpu->cd.arm.control |= ARM_CONTROL_MMU;
	cpu->cd.arm.ttb = ttb_addr;
	memset(nothing, 0, sizeof(nothing));
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.arm.ttb, nothing,
	    sizeof(nothing), MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);
	for (i=0; i<256; i++)
		for (j=0x0; j<=0xf; j++) {
			unsigned char descr[4];
			uint32_t addr = cpu->cd.arm.ttb +
			    (((j << 28) + (i << 20)) >> 18);
			uint32_t d = (1048576*i) | 2;
/*
d = (1048576 * (i + (j==12? 10 : j)*256)) | 2;
*/
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
				descr[0] = d;       descr[1] = d >> 8;
				descr[2] = d >> 16; descr[3] = d >> 24;
			} else {
				descr[3] = d;       descr[2] = d >> 8;
				descr[1] = d >> 16; descr[0] = d >> 24;
			}
			cpu->memory_rw(cpu, cpu->mem, addr, &descr[0],
			    sizeof(descr), MEM_WRITE, PHYSICAL |
			    NO_EXCEPTIONS);
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
		for (j=10 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 6) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  arm_cpu_register_match():
 */
void arm_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int i, cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register names:  */
	for (i=0; i<N_ARM_REGS; i++) {
		if (strcasecmp(name, arm_regname[i]) == 0) {
			if (writeflag) {
				m->cpus[cpunr]->cd.arm.r[i] = *valuep;
				if (i == ARM_PC)
					m->cpus[cpunr]->pc = *valuep;
			} else
				*valuep = m->cpus[cpunr]->cd.arm.r[i];
			*match_register = 1;
		}
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

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->cd.arm.r[ARM_PC], &offset);
		debug("cpu%i:  cpsr = ", x);
		debug("%s%s%s%s%s%s",
		    (cpu->cd.arm.cpsr & ARM_FLAG_N)? "N" : "n",
		    (cpu->cd.arm.cpsr & ARM_FLAG_Z)? "Z" : "z",
		    (cpu->cd.arm.cpsr & ARM_FLAG_C)? "C" : "c",
		    (cpu->cd.arm.cpsr & ARM_FLAG_V)? "V" : "v",
		    (cpu->cd.arm.cpsr & ARM_FLAG_I)? "I" : "i",
		    (cpu->cd.arm.cpsr & ARM_FLAG_F)? "F" : "f");
		if (mode < ARM_MODE_USR32)
			debug("   pc =  0x%07x",
			    (int)(cpu->cd.arm.r[ARM_PC] & 0x03ffffff));
		else
			debug("   pc = 0x%08x", (int)cpu->cd.arm.r[ARM_PC]);

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

	if (coprocs) {
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

		debug("cpu%i:  ttb = 0x%08x  dacr = 0x%08x\n", x,
		    cpu->cd.arm.ttb, cpu->cd.arm.dacr);
		debug("cpu%i:  fsr = 0x%08x  far = 0x%08x\n", x,
		    cpu->cd.arm.fsr, cpu->cd.arm.far);
	}
}


/*
 *  arm_cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void arm_cpu_show_full_statistics(struct machine *m)
{
	fatal("arm_cpu_show_full_statistics(): TODO\n");
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
	fatal("arm_cpu_tlbdump(): TODO\n");
}


/*
 *  arm_cpu_interrupt():
 */
int arm_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr)
{
	fatal("arm_cpu_interrupt(): TODO\n");
	return 0;
}


/*
 *  arm_cpu_interrupt_ack():
 */
int arm_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr)
{
	/*  fatal("arm_cpu_interrupt_ack(): TODO\n");  */
	return 0;
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
        int running, uint64_t dumpaddr, int bintrans)
{
	uint32_t iw, tmp;
	int main_opcode, secondary_opcode, s_bit, r16, r12, r8;
	int i, n, p_bit, u_bit, b_bit, w_bit, l_bit;
	char *symbol, *condition;
	uint64_t offset;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr);

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

	switch (main_opcode) {
	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		/*
		 *  Special cases first:
		 */

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
			int u_bit = (iw >> 22) & 1;
			int a_bit = (iw >> 21) & 1;
			debug("%s%sl%s%s\t", u_bit? "s" : "u",
			    a_bit? "mla" : "mul", condition, s_bit? "s" : "");
			debug("%s,", arm_regname[r12]);
			debug("%s,", arm_regname[r16]);
			debug("%s,", arm_regname[iw & 15]);
			debug("%s\n", arm_regname[r8]);
			break;
		}

		/*
		 *  xxxx0001 0010.... ........ 00L1mmmm  bx/blx rm
		 */
		if ((iw & 0x0ff000d0) == 0x01200010) {
			int l_bit = iw & 0x20;
			debug("b%sx%s\t%s\n", l_bit? "l" : "", condition,
			    arm_regname[iw & 15]);
			break;
		}

		/*
		 *  xxxx0001 0s10aaaa 11110000 0000mmmm  MSR  Regform
		 *  xxxx0011 0s10aaaa 1111rrrr bbbbbbbb  MSR  Immform
		 *  xxxx0001 0s001111 dddd0000 00000000  MRS
		 */
		if ((iw & 0x0fb0fff0) == 0x0120f000) {
			int a = (iw >> 16) & 15;
			debug("msr%s\t%s", condition, (iw&0x400000)? "S":"C");
			debug("PSR_");
			switch (a) {
			case 1:	debug("ctl"); break;
			case 8:	debug("flg"); break;
			case 9:	debug("all"); break;
			default:debug(" UNIMPLEMENTED ");
			}
			debug(",%s\n", arm_regname[iw & 15]);
			break;
		}
		if ((iw & 0x0fb0f000) == 0x0320f000) {
			debug("msr%s\tTODO Immform...", condition);
			debug("\n");
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
			/*  TODO: is swpb the right mnemonic for byte swap?  */
			debug("swp%s%s\t", (iw&0x400000)? "b":"", condition);
			debug("%s,%s,[%s]\n", arm_regname[r12],
			    arm_regname[iw & 15], arm_regname[r16]);
			break;
		}

		/*
		 *  xxxx000P U1WLnnnn ddddHHHH 1SH1LLLL load/store rd,imm(rn)
		 */
		if ((iw & 0x0e000090) == 0x00000090) {
			int imm = ((iw >> 4) & 0xf0) | (iw & 0xf);
			int regform = !(iw & 0x00400000);
			p_bit = main_opcode & 1;
			/*
			 *  TODO: detect some illegal variants:
			 *  signed store,  or  unsigned byte load/store
			 */
			debug("%sr%s", iw & 0x00100000? "ld" : "st",
			    condition);
			if (iw & 0x40)
				debug("s");	/*  signed  */
			if (iw & 0x20)
				debug("h");	/*  half-word  */
			else
				debug("b");	/*  byte  */
			debug("\t%s,[%s", arm_regname[r12], arm_regname[r16]);
			if (p_bit) {
				/*  Pre-index:  */
				if (regform)
					debug(",%s%s", u_bit? "" : "-",
					    arm_regname[iw & 15]);
				else {
					if (imm != 0)
						debug(",%s%i", u_bit? "" : "-",
						    imm);
				}
				debug("]%s\n", w_bit? "!" : "");
			} else {
				/*  Post-index:  */
				debug("],%s", u_bit? "" : "-");
				if (regform)
					debug("%s\n", arm_regname[iw & 15]);
				else
					debug("%i\n", imm);
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
		}
		debug("\n");
		break;
	case 0x4:				/*  Single Data Transfer  */
	case 0x5:
	case 0x6:
	case 0x7:
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
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
						addr = tmpw[0] + (tmpw[1] << 8) +
						    (tmpw[2] << 16) + (tmpw[3] << 24);
					else
						addr = tmpw[3] + (tmpw[2] << 8) +
						    (tmpw[1] << 16) + (tmpw[0] << 24);
				} else {
					tmpw[0] = addr = cpu->cd.arm.r[r12];
					if (r12 == ARM_PC)
						addr += 8;
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
		/*  xxxx110P UNWLnnnn DDDDpppp oooooooo LDC/STC  */
		debug("TODO: coprocessor LDC/STC\n");
		break;
	case 0xe:				/*  CDP (Coprocessor Op)  */
		/*				    or MRC/MCR!
		 *  xxxx1110 oooonnnn ddddpppp qqq0mmmm		CDP
		 *  xxxx1110 oooLNNNN ddddpppp qqq1MMMM		MRC/MCR
		 */
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


/*
 *  arm_mcr_mrc_15():
 *
 *  The system control coprocessor.
 */
void arm_mcr_mrc_15(struct cpu *cpu, int opcode1, int opcode2, int l_bit,
	int crn, int crm, int rd)
{
	uint32_t old_control;

	/*  Some sanity checks:  */
	if (opcode1 != 0) {
		fatal("arm_mcr_mrc_15: opcode1 = %i, should be 0\n", opcode1);
		exit(1);
	}
	if (rd == ARM_PC) {
		fatal("arm_mcr_mrc_15: rd = PC\n");
		exit(1);
	}

	switch (crn) {

	case 0:	/*  Main ID register:  */
		if (opcode2 != 0)
			fatal("[ arm_mcr_mrc_15: TODO: cr0, opcode2=%i ]\n",
			    opcode2);
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.cpu_type.cpu_id;
		else
			fatal("[ arm_mcr_mrc_15: attempt to write to cr0? ]\n");
		break;

	case 1:	/*  Control Register:  */
		if (l_bit) {
			cpu->cd.arm.r[rd] = cpu->cd.arm.control;
			return;
		}
		/*
		 *  Write to control:  Check each bit individually:
		 */
		old_control = cpu->cd.arm.control;
		cpu->cd.arm.control = cpu->cd.arm.r[rd];
		if ((old_control & ARM_CONTROL_MMU) !=
		    (cpu->cd.arm.control & ARM_CONTROL_MMU))
			debug("[ %s the MMU ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_MMU? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_ALIGN) !=
		    (cpu->cd.arm.control & ARM_CONTROL_ALIGN))
			debug("[ %s alignment checks ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_ALIGN? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_CACHE) !=
		    (cpu->cd.arm.control & ARM_CONTROL_CACHE))
			debug("[ %s the [data] cache ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_CACHE? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_WBUFFER) !=
		    (cpu->cd.arm.control & ARM_CONTROL_WBUFFER))
			debug("[ %s the write buffer ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_WBUFFER? "enabling" : "disabling");
		if ((old_control & ARM_CONTROL_BIG) !=
		    (cpu->cd.arm.control & ARM_CONTROL_BIG)) {
			fatal("ERROR: Trying to switch endianness. Not "
			    "supported yet.\n");
			exit(1);
		}
		if ((old_control & ARM_CONTROL_ICACHE) !=
		    (cpu->cd.arm.control & ARM_CONTROL_ICACHE))
			debug("[ %s the icache ]\n", cpu->cd.arm.control &
			    ARM_CONTROL_ICACHE? "enabling" : "disabling");
		/*  TODO: More bits.  */
		break;

	case 2:	/*  Translation Table Base register:  */
		/*  NOTE: 16 KB aligned.  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.ttb & 0xffffc000;
		else
			cpu->cd.arm.ttb = cpu->cd.arm.r[rd] & 0xffffc000;
		break;

	case 3:	/*  Domain Access Control Register:  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.dacr;
		else
			cpu->cd.arm.dacr = cpu->cd.arm.r[rd];
		break;

	case 5:	/*  Fault Status Register:  */
		/*  Note: Only the lowest 8 bits are defined.  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.fsr & 0xff;
		else
			cpu->cd.arm.fsr = cpu->cd.arm.r[rd] & 0xff;
		break;

	case 6:	/*  Fault Address Register:  */
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.far;
		else
			cpu->cd.arm.far = cpu->cd.arm.r[rd];
		break;

	case 7:	/*  Cache functions:  */
		if (l_bit) {
			fatal("[ arm_mcr_mrc_15: attempt to read cr7? ]\n");
			return;
		}
		/*  debug("[ arm_mcr_mrc_15: cache op: TODO ]\n");  */
		break;

	case 8:	/*  TLB functions:  */
		if (l_bit) {
			fatal("[ arm_mcr_mrc_15: attempt to read cr8? ]\n");
			return;
		}
		fatal("[ arm_mcr_mrc_15: TLB: op2=%i crm=%i rd=0x%08x ]\n",
		    opcode2, crm, cpu->cd.arm.r[rd]);
		/*  TODO:  */
		cpu->invalidate_translation_caches_paddr(cpu,
		    0, INVALIDATE_ALL);
		break;

	case 13:/*  Process ID Register:  */
		if (opcode2 != 0)
			fatal("[ arm_mcr_mrc_15: PID access, but opcode2 "
			    "= %i? (should be 0) ]\n", opcode2);
		if (crm != 0)
			fatal("[ arm_mcr_mrc_15: PID access, but crm "
			    "= %i? (should be 0) ]\n", crm);
		if (l_bit)
			cpu->cd.arm.r[rd] = cpu->cd.arm.pid;
		else
			cpu->cd.arm.pid = cpu->cd.arm.r[rd];
		if (cpu->cd.arm.pid != 0) {
			fatal("ARM TODO: pid!=0. Fast Context Switch"
			    " Extension not implemented yet\n");
			exit(1);
		}
		break;
	default:fatal("arm_mcr_mrc_15: unimplemented crn = %i\n", crn);
		fatal("(opcode1=%i opcode2=%i crm=%i rd=%i l=%i)\n",
		    opcode1, opcode2, crm, rd, l_bit);
		exit(1);
	}
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

	switch (cp_num) {
	case 15:arm_mcr_mrc_15(cpu, opcode1, opcode2, l_bit, crn, crm, rd);
		break;
	default:fatal("arm_mcr_mrc: pc=0x%08x, iword=0x%08x: "
		    "cp_num=%i\n", (int)cpu->pc, iword, cp_num);
		exit(1);
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
	fatal("arm_cdp: pc=0x%08x, iword=0x%08x\n", (int)cpu->pc, iword);
	exit(1);
}


/*****************************************************************************/


#include "tmp_arm_tail.c"
