/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: cpu.c,v 1.203 2004-11-30 11:57:25 debug Exp $
 *
 *  MIPS core CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "misc.h"

#include "bintrans.h"
#include "console.h"
#include "emul.h"
#include "dec_5100.h"
#include "dec_kn02.h"
#include "devices.h"
#include "memory.h"
#include "symbol.h"


extern int old_show_trace_tree;
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;

char *exception_names[] = EXCEPTION_NAMES;

static char *hi6_names[] = HI6_NAMES;
static char *regimm_names[] = REGIMM_NAMES;
static char *special_names[] = SPECIAL_NAMES;
static char *special2_names[] = SPECIAL2_NAMES;


/*
 *  cpu_new():
 *
 *  Create a new cpu object.
 */
struct cpu *cpu_new(struct memory *mem, struct emul *emul, int cpu_id,
	char *cpu_type_name)
{
	struct cpu *cpu;
	int i, j, tags_size, n_cache_lines, size_per_cache_line;
	struct cpu_type_def cpu_type_defs[] = CPU_TYPE_DEFS;
	int64_t secondary_cache_size;
	int x, linesize;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->mem                = mem;
	cpu->emul               = emul;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;
	cpu->gpr[GPR_SP]	= INITIAL_STACK_POINTER;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	i = 0;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			cpu->cpu_type = cpu_type_defs[i];
			i = -1;
			break;
		}
		i++;
	}

	if (i != -1) {
		fprintf(stderr, "cpu_new(): unknown cpu type '%s'\n",
		    cpu_type_name);
		exit(1);
	}

	/*
	 *  CACHES:
	 *
	 *  1) Use DEFAULT_PCACHE_SIZE and DEFAULT_PCACHE_LINESIZE etc.
	 *  2) If there are specific values defined for this type of cpu,
	 *     in its cpu_type substruct, then let's use those.
	 *  3) Values in the emul struct override both of the above.
	 *
	 *  Once we've decided which values to use, they are stored in
	 *  the emul struct so they can be used from src/machine.c etc.
	 */

	x = DEFAULT_PCACHE_SIZE;
	if (cpu->cpu_type.default_pdcache)
		x = cpu->cpu_type.default_pdcache;
	if (emul->cache_pdcache == 0)
		emul->cache_pdcache = x;

	x = DEFAULT_PCACHE_SIZE;
	if (cpu->cpu_type.default_picache)
		x = cpu->cpu_type.default_picache;
	if (emul->cache_picache == 0)
		emul->cache_picache = x;

	if (emul->cache_secondary == 0)
		emul->cache_secondary = cpu->cpu_type.default_scache;

	linesize = DEFAULT_PCACHE_LINESIZE;
	if (cpu->cpu_type.default_pdlinesize)
		linesize = cpu->cpu_type.default_pdlinesize;
	if (emul->cache_pdcache_linesize == 0)
		emul->cache_pdcache_linesize = linesize;

	linesize = DEFAULT_PCACHE_LINESIZE;
	if (cpu->cpu_type.default_pilinesize)
		linesize = cpu->cpu_type.default_pilinesize;
	if (emul->cache_picache_linesize == 0)
		emul->cache_picache_linesize = linesize;

	linesize = 0;
	if (cpu->cpu_type.default_slinesize)
		linesize = cpu->cpu_type.default_slinesize;
	if (emul->cache_secondary_linesize == 0)
		emul->cache_secondary_linesize = linesize;


	/*
	 *  Primary Data and Instruction caches:
	 */
	for (i=CACHE_DATA; i<=CACHE_INSTRUCTION; i++) {
		switch (i) {
		case CACHE_DATA:
			x = 1 << emul->cache_pdcache;
			linesize = 1 << emul->cache_pdcache_linesize;
			break;
		case CACHE_INSTRUCTION:
			x = 1 << emul->cache_picache;
			linesize = 1 << emul->cache_picache_linesize;
			break;
		}

		/*  Primary cache size and linesize:  */
		cpu->cache_size[i] = x;
		cpu->cache_linesize[i] = linesize;

		switch (cpu->cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			size_per_cache_line = sizeof(struct r3000_cache_line);
			break;
		default:
			size_per_cache_line = sizeof(struct r4000_cache_line);
		}

		cpu->cache_mask[i] = cpu->cache_size[i] - 1;
		cpu->cache_miss_penalty[i] = 10;	/*  TODO ?  */

		cpu->cache[i] = malloc(cpu->cache_size[i]);
		if (cpu->cache[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		n_cache_lines = cpu->cache_size[i] / cpu->cache_linesize[i];
		tags_size = n_cache_lines * size_per_cache_line;

		cpu->cache_tags[i] = malloc(tags_size);
		if (cpu->cache_tags[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		/*  Initialize the cache tags:  */
		switch (cpu->cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			for (j=0; j<n_cache_lines; j++) {
				struct r3000_cache_line *rp;
				rp = (struct r3000_cache_line *)
				    cpu->cache_tags[i];
				rp[j].tag_paddr = 0;
				rp[j].tag_valid = 0;
			}
			break;
		default:
			;
		}

		/*  Set cache_last_paddr to something "impossible":  */
		cpu->cache_last_paddr[i] = IMPOSSIBLE_PADDR;
	}

	/*
	 *  Secondary cache:
	 */
	secondary_cache_size = 0;
	if (emul->cache_secondary)
		secondary_cache_size = 1 << emul->cache_secondary;
	/*  TODO: linesize...  */

	if (cpu_id == 0) {
		debug(" (I+D = %i+%i KB",
		    (int)(cpu->cache_size[CACHE_INSTRUCTION] / 1024),
		    (int)(cpu->cache_size[CACHE_DATA] / 1024));

		if (secondary_cache_size != 0) {
			debug(", L2 = ");
			if (secondary_cache_size >= 1048576)
				debug("%i MB", (int)
				    (secondary_cache_size / 1048576));
			else
				debug("%i KB", (int)
				    (secondary_cache_size / 1024));
		}

		debug(")");
	}

	cpu->coproc[0] = coproc_new(cpu, 0);	/*  System control, MMU  */
	cpu->coproc[1] = coproc_new(cpu, 1);	/*  FPU  */

	/*
	 *  Initialize the cpu->pc_last_* cache (a 1-entry cache of the
	 *  last program counter value).  For pc_last_virtual_page, any
	 *  "impossible" value will do.  The pc should never ever get this
	 *  value.  (The other pc_last* variables do not need initialization,
	 *  as they are not used before pc_last_virtual_page.)
	 */
	cpu->pc_last_virtual_page = PC_LAST_PAGE_IMPOSSIBLE_VALUE;

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		cpu->translate_address = translate_address_mmu3k;
		break;
	default:
		cpu->translate_address = translate_address_generic;
	}

	return cpu;
}


/*
 *  cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
static void cpu_show_full_statistics(struct emul *emul, struct cpu **cpus)
{
	int i, s1, s2;

	if (emul->bintrans_enable)
		printf("\nNOTE: Dynamic binary translation is used; this list of opcode usage\n"
		    "      only includes instructions that were interpreted manually!\n");

	for (i=0; i<emul->ncpus; i++) {
		printf("cpu%i opcode statistics:\n", i);
		for (s1=0; s1<N_HI6; s1++) {
			if (cpus[i]->stats_opcode[s1] > 0)
				printf("  opcode %02x (%7s): %li\n", s1,
				    hi6_names[s1], cpus[i]->stats_opcode[s1]);
			if (s1 == HI6_SPECIAL)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (cpus[i]->stats__special[s2] > 0)
						printf("      special %02x (%7s): %li\n",
						    s2, special_names[s2], cpus[i]->stats__special[s2]);
			if (s1 == HI6_REGIMM)
				for (s2=0; s2<N_REGIMM; s2++)
					if (cpus[i]->stats__regimm[s2] > 0)
						printf("      regimm %02x (%7s): %li\n",
						    s2, regimm_names[s2], cpus[i]->stats__regimm[s2]);
			if (s1 == HI6_SPECIAL2)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (cpus[i]->stats__special2[s2] > 0)
						printf("      special2 %02x (%7s): %li\n",
						    s2, special2_names[s2], cpus[i]->stats__special2[s2]);
		}
	}
}


/*
 *  cpu_add_tickfunction():
 *
 *  Adds a tick function (a function called every now and then, depending on
 *  clock cycle count).
 */
void cpu_add_tickfunction(struct cpu *cpu, void (*func)(struct cpu *, void *),
	void *extra, int clockshift)
{
	int n = cpu->n_tick_entries;

	if (n >= MAX_TICK_FUNCTIONS) {
		fprintf(stderr, "cpu_add_tickfunction(): too many tick functions\n");
		exit(1);
	}

	/*  Don't use too low clockshifts, that would be too inefficient with bintrans.  */
	if (clockshift < N_SAFE_BINTRANS_LIMIT_SHIFT)
		fatal("WARNING! clockshift = %i, less than N_SAFE_BINTRANS_LIMIT_SHIFT (%i)\n",
		    clockshift, N_SAFE_BINTRANS_LIMIT_SHIFT);

	cpu->ticks_till_next[n]   = 0;
	cpu->ticks_reset_value[n] = 1 << clockshift;
	cpu->tick_func[n]         = func;
	cpu->tick_extra[n]        = extra;

	cpu->n_tick_entries ++;
}


/*
 *  cpu_flags():
 *
 *  Returns a pointer to a string containing "(d)" "(j)" "(dj)" or "",
 *  depending on the cpu's current delay_slot and last_was_jumptoself
 *  flags.
 */
const char *cpu_flags(struct cpu *cpu)
{
	if (cpu->delay_slot) {
		if (cpu->last_was_jumptoself)
			return " (dj)";
		else
			return " (d)";
	} else {
		if (cpu->last_was_jumptoself)
			return " (j)";
		else
			return "";
	}
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running is 1, cpu->pc_last should be the address of the instruction,
 *  cpu->pc should already point to the _next_ instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 *
 *  NOTE 2:  coprocessor instructions are not decoded nicely yet  (TODO)
 */
void cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int hi6, special6, regimm5;
	int rt, rd, rs, sa, imm, copz, cache_op, which_cache, showtag;
	uint64_t addr, offset;
	uint32_t instrword;
	char *symbol;

	if (running)
		dumpaddr = cpu->pc_last;

	symbol = get_symbol_name(&cpu->emul->symbol_context, dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (running)
		debug("cpu%i @ ", cpu->cpu_id);
	else
		debug("0x");

	if (cpu->cpu_type.isa_level < 3 ||
	    cpu->cpu_type.isa_level == 32)
		debug("%08x", (int)dumpaddr);
	else
		debug("%016llx", (long long)dumpaddr);

	debug(": %02x%02x%02x%02x",
	    instr[3], instr[2], instr[1], instr[0]);

	if (running)
		debug("%s", cpu_flags(cpu));

	debug("\t");

	if (bintrans) {
		debug("(bintrans)");
		goto disasm_ret;
	}

	/*
	 *  Decode the instruction:
	 */

	if (cpu->nullify_next && running) {
		debug("(nullified)");
		goto disasm_ret;
	}

	hi6 = (instr[3] >> 2) & 0x3f;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;
		switch (special6) {
		case SPECIAL_SLL:
		case SPECIAL_SRL:
		case SPECIAL_SRA:
		case SPECIAL_DSLL:
		case SPECIAL_DSRL:
		case SPECIAL_DSRA:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRL32:
		case SPECIAL_DSRA32:
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (sa == 0)
					debug("nop");
				else if (sa == 1)
					debug("ssnop");
				else
					debug("nop (weird, sa=%i)", sa);
				goto disasm_ret;
			} else
				debug("%s\tr%i,r%i,%i",
				    special_names[special6], rd, rt, sa);
			break;
		case SPECIAL_DSRLV:
		case SPECIAL_DSRAV:
		case SPECIAL_DSLLV:
		case SPECIAL_SLLV:
		case SPECIAL_SRAV:
		case SPECIAL_SRLV:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			debug("%s\tr%i,r%i,r%i",
			    special_names[special6], rd, rt, rs);
			break;
		case SPECIAL_JR:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			symbol = get_symbol_name(&cpu->emul->symbol_context,
			    cpu->gpr[rs], &offset);
			debug("jr\tr%i", rs);
			if (running && symbol != NULL)
				debug("\t\t<%s>", symbol);
			break;
		case SPECIAL_JALR:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;
			symbol = get_symbol_name(&cpu->emul->symbol_context,
			    cpu->gpr[rs], &offset);
			debug("jalr\tr%i,r%i", rd, rs);
			if (running && symbol != NULL)
				debug("<%s>", symbol);
			break;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;
			debug("%s\tr%i", special_names[special6], rd);
			break;
		case SPECIAL_MTLO:
		case SPECIAL_MTHI:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			debug("%s\tr%i", special_names[special6], rs);
			break;
		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_SLT:
		case SPECIAL_SLTU: 
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			debug("%s\tr%i,r%i,r%i", special_names[special6],
			    rd, rs, rt);
			break;
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:  
		case SPECIAL_DDIVU:
		case SPECIAL_TGE:                
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			if (special6 == SPECIAL_MULT) {
				if (rd != 0) {
					debug("mult_xx\tr%i,r%i,r%i",
					    rd, rs, rt);
					goto disasm_ret;
				}
			}
			debug("%s\tr%i,r%i", special_names[special6],
			    rs, rt);
			break;
		case SPECIAL_SYNC:
			imm = ((instr[1] & 7) << 2) + (instr[0] >> 6);
			debug("sync\t0x%02x", imm);
			break;
		case SPECIAL_SYSCALL:
			imm = (((instr[3] << 24) + (instr[2] << 16) +
			    (instr[1] << 8) + instr[0]) >> 6) & 0xfffff;
			debug("syscall\t0x%05x", imm);
			break;
		case SPECIAL_BREAK:
			debug("break");
			break;
		case SPECIAL_MFSA:
			rd = (instr[1] >> 3) & 31;
			debug("mfsa\tr%i", rd);
			break;
		case SPECIAL_MTSA:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			debug("mtsa\tr%i", rs);
			break;
		default:
			debug("unimplemented special6 = 0x%02x", special6);
		}
		break;
	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BNEL:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		addr = (dumpaddr + 4) + (imm << 2);
		debug("%s\tr%i,r%i,%016llx", hi6_names[hi6], rt, rs,
		    (long long)addr);
		symbol = get_symbol_name(&cpu->emul->symbol_context,
		    addr, &offset);
		if (symbol != NULL && offset != addr)
			debug("\t<%s>", symbol);
		break;
	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		debug("%s\tr%i,r%i,%i", hi6_names[hi6], rt, rs, imm);
		break;
	case HI6_LUI:
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		debug("lui\tr%i,0x%x", rt, imm & 0xffff);
		break;
	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_LWL:   
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)
			imm -= 65536;
		symbol = get_symbol_name(&cpu->emul->symbol_context,
		    cpu->gpr[rs] + imm, &offset);

		/*  LWC3 is PREF in the newer ISA levels:  */
		if (hi6 == HI6_LWC3) {
			debug("pref\t0x%x,%i(r%i)\t\t[0x%016llx = %s]",
			    rt, imm, rs, (long long)(cpu->gpr[rs] + imm),
			    symbol);
			goto disasm_ret;
		}

		debug("%s\tr%i,%i(r%i)",
		    hi6_names[hi6], rt, imm, rs);

		if (running) {
			if (symbol != NULL)
				debug("\t\t[0x%016llx = %s, data=",
				    (long long)(cpu->gpr[rs] + imm), symbol);
			else
				debug("\t\t[0x%016llx, data=",
				    (long long)(cpu->gpr[rs] + imm));
		} else
			break;
		/*  NOTE: No break here (if we are running) as it is up
		    to the caller to print 'data'.  */
		return;
	case HI6_J:
	case HI6_JAL:
		imm = (((instr[3] & 3) << 24) + (instr[2] << 16) +
		    (instr[1] << 8) + instr[0]) << 2;
		addr = (dumpaddr + 4) & ~((1 << 28) - 1);
		addr |= imm;
		symbol = get_symbol_name(&cpu->emul->symbol_context,
		    addr, &offset);
		if (symbol != NULL)
			debug("%s\t0x%016llx\t<%s>",
			    hi6_names[hi6], (long long)addr, symbol);
		else
			debug("%s\t0x%016llx", hi6_names[hi6], (long long)addr);
		break;
	case HI6_COP0:
	case HI6_COP1:
	case HI6_COP2:
	case HI6_COP3:
		imm = (instr[3] << 24) + (instr[2] << 16) +
		     (instr[1] << 8) + instr[0];
		imm &= ((1 << 26) - 1);

		/*  Call coproc_function(), but ONLY disassembly, no exec:  */
		coproc_function(cpu, cpu->coproc[hi6 - HI6_COP0], imm,
		    1, running);
		return;
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5); /*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];
		cache_op    = copz >> 2;
		which_cache = copz & 3;
		showtag = 0;
		debug("cache\t0x%02x,0x%04x(r%i)", copz, imm, rt);
		if (which_cache==0)	debug("  [ primary I-cache");
		if (which_cache==1)	debug("  [ primary D-cache");
		if (which_cache==2)	debug("  [ secondary I-cache");
		if (which_cache==3)	debug("  [ secondary D-cache");
		debug(", ");
		if (cache_op==0)	debug("index invalidate");
		if (cache_op==1)	debug("index load tag");
		if (cache_op==2)	debug("index store tag"), showtag=1;
		if (cache_op==3)	debug("create dirty exclusive");
		if (cache_op==4)	debug("hit invalidate");
		if (cache_op==5)	debug("fill OR hit writeback invalidate");
		if (cache_op==6)	debug("hit writeback");
		if (cache_op==7)	debug("hit set virtual");
		debug(", r%i=0x%016llx", rt, (long long)cpu->gpr[rt]);
		if (showtag)
		debug(", taghi=%08lx lo=%08lx",
		    (long)cpu->coproc[0]->reg[COP0_TAGDATA_HI],
		    (long)cpu->coproc[0]->reg[COP0_TAGDATA_LO]);
		debug(" ]");
		break;
	case HI6_SPECIAL2:
		special6 = instr[0] & 0x3f;
		instrword = (instr[3] << 24) + (instr[2] << 16) +
		    (instr[1] << 8) + instr[0];
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;
		if ((instrword & 0xfc0007ffULL) == 0x70000000) {
			debug("madd\tr(r%i,)r%i,r%i\n", rd, rs, rt);
		} else if (special6 == SPECIAL2_MUL) {
			/*  TODO: this is just a guess, I don't have the
				docs in front of me  */
			debug("mul\tr%i,r%i,r%i\n", rd, rs, rt);
		} else if (special6 == SPECIAL2_CLZ) {
			debug("clz\tr%i,r%i", rd, rs);
		} else if (special6 == SPECIAL2_CLO) {
			debug("clo\tr%i,r%i", rd, rs);
		} else if (special6 == SPECIAL2_DCLZ) {
			debug("dclz\tr%i,r%i", rd, rs);
		} else if (special6 == SPECIAL2_DCLO) {
			debug("dclo\tr%i,r%i", rd, rs);
		} else if ((instrword & 0xffff07ffULL) == 0x70000209
		    || (instrword & 0xffff07ffULL) == 0x70000249) {
			if (instr[0] == 0x49) {
			debug("pmflo\tr%i rs=%i\n", rd);
			} else {
			debug("pmfhi\tr%i rs=%i\n", rd);
			}
		} else if ((instrword & 0xfc1fffff) == 0x70000269 
		    || (instrword & 0xfc1fffff) == 0x70000229) {
			if (instr[0] == 0x69) {
				debug("pmtlo\tr%i rs=%i\n", rs);
			} else {
				debug("pmthi\tr%i rs=%i\n", rs);
			} 
		} else if ((instrword & 0xfc0007ff) == 0x700004a9) {
			debug("por\tr%i,r%i,r%i\n", rd, rs, rt);
		} else if ((instrword & 0xfc0007ff) == 0x70000488) {
			debug("pextlw\tr%i,r%i,r%i\n", rd, rs, rt);
		} else {
			debug("unimplemented special2 = 0x%02x", special6);
		}
		break;
	case HI6_REGIMM:
		regimm5 = instr[2] & 0x1f;
		switch (regimm5) {
		case REGIMM_BLTZ:
		case REGIMM_BGEZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)               
				imm -= 65536;
			debug("%s\tr%i,%016llx", regimm_names[regimm5],
			    rs, (dumpaddr + 4) + (imm << 2));
			break;
		default:
			debug("unimplemented regimm5 = 0x%02x", regimm5);
		}
		break;
	default:
		debug("unimplemented hi6 = 0x%02x", hi6);
	}

disasm_ret:
	debug("\n");
}


/*
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 */
void cpu_register_dump(struct cpu *cpu)
{
	int i;
	uint64_t offset;
	char *symbol;

	symbol = get_symbol_name(&cpu->emul->symbol_context, cpu->pc, &offset);

	/*  Special registers:  */
	if (cpu->cpu_type.isa_level < 3 ||
	    cpu->cpu_type.isa_level == 32)
		debug("cpu%i:  hi  = %08x  lo  = %08x  pc  = %08x",
		    cpu->cpu_id, (int)cpu->hi, (int)cpu->lo, (int)cpu->pc);
	else
		debug("cpu%i:  hi  = %016llx  lo  = %016llx  pc  = %016llx",
		    cpu->cpu_id, (long long)cpu->hi,
		    (long long)cpu->lo, (long long)cpu->pc);

	if (symbol != NULL)
		debug(" <%s>", symbol);
	debug("\n");

	/*  General registers:  */
	if (cpu->cpu_type.rev == MIPS_R5900) {
		/*  128-bit:  */
		for (i=0; i<32; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", cpu->cpu_id);
			debug(" r%02i=%016llx%016llx", i,
			    (long long)cpu->gpr_quadhi[i],
			    (long long)cpu->gpr[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
	} else if (cpu->cpu_type.isa_level < 3 ||
	    cpu->cpu_type.isa_level == 32) {
		/*  32-bit:  */
		for (i=0; i<32; i++) {
			if ((i & 3) == 0)
				debug("cpu%i:", cpu->cpu_id);
			debug("  r%02i = %08x", i, (int)cpu->gpr[i]);
			if ((i & 3) == 3)
				debug("\n");
		}
	} else {
		/*  64-bit:  */
		for (i=0; i<32; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", cpu->cpu_id);
			debug("    r%02i = %016llx", i, (long long)cpu->gpr[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
	}

	/*  Coprocessor 0 registers:  */
	/*  TODO: multiple selections per register?  */
	if (cpu->cpu_type.isa_level < 3 ||
	    cpu->cpu_type.isa_level == 32) {
		/*  32-bit:  */
		for (i=0; i<32; i++) {
			if ((i & 3) == 0)
				debug("cpu%i:", cpu->cpu_id);
			debug("  c0,%02i = %08x", i,
			    (int)cpu->coproc[0]->reg[i]);
			if ((i & 3) == 3)
				debug("\n");
		}
	} else {
		/*  64-bit:  */
		for (i=0; i<32; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", cpu->cpu_id);
			debug("  c0,%02i = %016llx", i,
			    (long long)cpu->coproc[0]->reg[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
	}
}


/*
 *  show_trace():
 *
 *  Show trace tree.   This function should be called every time
 *  a function is called.  cpu->trace_tree_depth is increased here
 *  and should not be increased by the caller.
 *
 *  Note:  This function should not be called if show_trace_tree == 0.
 */
void show_trace(struct cpu *cpu, uint64_t addr)
{
	uint64_t offset;
	int x, n_args_to_print;
	char strbuf[50];
	char *symbol;

	cpu->trace_tree_depth ++;

	if (cpu->emul->ncpus > 1)
		debug("cpu%i:", cpu->cpu_id);

	symbol = get_symbol_name(&cpu->emul->symbol_context, addr, &offset);

	for (x=0; x<cpu->trace_tree_depth; x++)
		debug("  ");

	/*  debug("<%s>\n", symbol!=NULL? symbol : "no symbol");  */

	if (symbol != NULL)
		debug("<%s(", symbol);
	else
		debug("<0x%016llx(", (long long)addr);

	/*
	 *  TODO:  The number of arguments and the symbol type of each
	 *  argument should be taken from the symbol table, in some way.
	 *
	 *  The MIPS binary calling convention is that the first 4
	 *  arguments are in registers a0..a3.
	 *
	 *  Choose a value greater than 4 (eg 5) to print all values in
	 *  the A0..A3 registers and then add a ".." to indicate that
	 *  there might be more arguments.
	 */
	n_args_to_print = 5;

	for (x=0; x<n_args_to_print; x++) {
		int64_t d = cpu->gpr[x + GPR_A0];

		if (d > -256 && d < 256)
			debug("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1))
			debug("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
		else
			debug("0x%llx", (long long)d);

		if (x < n_args_to_print - 1)
			debug(",");

		/*  Cannot go beyound GPR_A3:  */
		if (x == 3)
			break;
	}

	if (n_args_to_print > 4)
		debug("..");

	debug(")>\n");
}


/*
 *  NOTE:  memory.c is included here, so as to lie close to the often
 *         used CPU routines in the [host's] cache.
 */
#include "memory.c"


/*
 *  cpu_interrupt():
 *
 *  Cause an interrupt. If irq_nr is 2..7, then it is a MIPS interrupt.
 *  0 and 1 are ignored (software interrupts).
 *
 *  If it is >= 8, then it is an external (machine dependant) interrupt.
 *  cpu->md_interrupt() is called. That function may call cpu_interrupt()
 *  using low values (2..7). There's no actual check to make sure that
 *  there's no recursion, so the md_interrupt routine has to make sure of
 *  this.
 */
int cpu_interrupt(struct cpu *cpu, int irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->md_interrupt != NULL)
			cpu->md_interrupt(cpu, irq_nr, 1);
		else
			fatal("cpu_interrupt(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->coproc[0]->reg[COP0_CAUSE] |= ((1 << irq_nr) << STATUS_IM_SHIFT);
	cpu->cached_interrupt_is_possible = 1;
	return 1;
}


/*
 *  cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt. If irq_nr is 2..7, then it is a MIPS interrupt.
 *  If it is >= 8, then it is machine dependant.
 */
int cpu_interrupt_ack(struct cpu *cpu, int irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->md_interrupt != NULL)
			cpu->md_interrupt(cpu, irq_nr, 0);
		else
			fatal("cpu_interrupt_ack(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->coproc[0]->reg[COP0_CAUSE] &= ~((1 << irq_nr) << STATUS_IM_SHIFT);
	if (!(cpu->coproc[0]->reg[COP0_CAUSE] & STATUS_IM_MASK))
		cpu->cached_interrupt_is_possible = 0;

	return 1;
}


/*
 *  cpu_exception():
 *
 *  Cause an exception in a CPU.  This sets a couple of coprocessor 0
 *  registers, and the program counter.
 *
 *	exccode		the exception code
 *	tlb		set to non-zero if the exception handler at
 *			0x80000000 should be used. (normal = 0x80000180)
 *	vaddr		virtual address (for some exceptions)
 *	coproc_nr	coprocessor number (for some exceptions)
 *	vaddr_vpn2	vpn2 (for some exceptions)
 *	vaddr_asid	asid (for some exceptions)
 *	x_64		non-zero for 64-bit mode for R4000-style tlb misses
 */
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
	int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64)
{
	uint64_t *reg = &cpu->coproc[0]->reg[0];
	int exc_model = cpu->cpu_type.exc_model;

	if (!quiet_mode) {
		uint64_t offset;
		int x;
		char *symbol = get_symbol_name(
		    &cpu->emul->symbol_context, cpu->pc_last, &offset);

		debug("[ ");
		if (cpu->emul->ncpus > 1)
			debug("cpu%i: ", cpu->cpu_id);

		debug("exception %s%s",
		    exception_names[exccode], tlb? " <tlb>" : "");

		switch (exccode) {
		case EXCEPTION_INT:
			debug(" cause_im=0x%02x", (int)((reg[COP0_CAUSE] & CAUSE_IP_MASK) >> CAUSE_IP_SHIFT));
			break;
		case EXCEPTION_SYS:
			debug(" v0=%i", (int)cpu->gpr[GPR_V0]);
			for (x=0; x<4; x++) {
				int64_t d = cpu->gpr[GPR_A0 + x];
				char strbuf[30];

				if (d > -256 && d < 256)
					debug(" a%i=%i", x, (int)d);
				else if (memory_points_to_string(cpu, cpu->mem, d, 1))
					debug(" a%i=\"%s\"", x, memory_conv_to_string(cpu, cpu->mem, d, strbuf, sizeof(strbuf)));
				else
					debug(" a%i=0x%llx", x, (long long)d);
			}
			break;
		default:
			debug(" vaddr=%08llx", (long long)vaddr);
		}

		debug(" pc->last=%08llx <%s> ]\n",
		    (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
	}

	if (tlb && vaddr < 0x1000) {
		uint64_t offset;
		char *symbol = get_symbol_name(
		    &cpu->emul->symbol_context, cpu->pc_last, &offset);
		fatal("warning: LOW reference vaddr=0x%08x, exception %s, pc->last=%08llx <%s>\n",
		    (int)vaddr, exception_names[exccode], (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
	}

	/*  Clear the exception code bits of the cause register...  */
	if (exc_model == EXC3K) {
		reg[COP0_CAUSE] &= ~R2K3K_CAUSE_EXCCODE_MASK;
#if 0
		if (exccode >= 16) {
			fatal("exccode = %i  (there are only 16 exceptions on R3000 and lower)\n", exccode);
			cpu->running = 0;
			return;
		}
#endif
	} else
		reg[COP0_CAUSE] &= ~CAUSE_EXCCODE_MASK;

	/*  ... and OR in the exception code:  */
	reg[COP0_CAUSE] |= (exccode << CAUSE_EXCCODE_SHIFT);

	/*  Always set CE (according to the R5000 manual):  */
	reg[COP0_CAUSE] &= ~CAUSE_CE_MASK;
	reg[COP0_CAUSE] |= (coproc_nr << CAUSE_CE_SHIFT);

	/*  TODO:  On R4000, vaddr should NOT be set on bus errors!!!  */
#if 0
	if (exccode == EXCEPTION_DBE) {
		reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;
	}
#endif

	if (tlb || (exccode >= EXCEPTION_MOD && exccode <= EXCEPTION_ADES) ||
	    exccode == EXCEPTION_VCEI || exccode == EXCEPTION_VCED) {
		reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;

		if (exc_model == EXC3K) {
			reg[COP0_CONTEXT] &= ~R2K3K_CONTEXT_BADVPN_MASK;
			reg[COP0_CONTEXT] |= ((vaddr_vpn2 << R2K3K_CONTEXT_BADVPN_SHIFT) & R2K3K_CONTEXT_BADVPN_MASK);

			reg[COP0_ENTRYHI] = (vaddr & R2K3K_ENTRYHI_VPN_MASK)
			    | (vaddr_asid << R2K3K_ENTRYHI_ASID_SHIFT);

			/*  Sign-extend:  */
			reg[COP0_CONTEXT] = (int64_t)(int32_t)reg[COP0_CONTEXT];
			reg[COP0_ENTRYHI] = (int64_t)(int32_t)reg[COP0_ENTRYHI];
		} else {
			reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK;
			reg[COP0_CONTEXT] |= ((vaddr_vpn2 << CONTEXT_BADVPN2_SHIFT) & CONTEXT_BADVPN2_MASK);

			/*  TODO: Sign-extend CONTEXT?  */

			reg[COP0_XCONTEXT] &= ~XCONTEXT_R_MASK;
			reg[COP0_XCONTEXT] &= ~XCONTEXT_BADVPN2_MASK;
			reg[COP0_XCONTEXT] |= (vaddr_vpn2 << XCONTEXT_BADVPN2_SHIFT) & XCONTEXT_BADVPN2_MASK;
			reg[COP0_XCONTEXT] |= ((vaddr >> 62) & 0x3) << XCONTEXT_R_SHIFT;

			/*  reg[COP0_PAGEMASK] = cpu->coproc[0]->tlbs[0].mask & PAGEMASK_MASK;  */

			if (cpu->cpu_type.mmu_model == MMU10K)
				reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K)) | vaddr_asid;
			else
				reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK)) | vaddr_asid;
		}
	}

	if (exc_model == EXC4K && reg[COP0_STATUS] & STATUS_EXL) {
		/*
		 *  Don't set EPC if STATUS_EXL is set, for R4000 and up.
		 *  This actually happens when running IRIX and Ultrix, when
		 *  they handle interrupts and/or tlb updates, I think, so
		 *  printing this with debug() looks better than with fatal().
		 */
		/*  debug("[ warning: cpu%i exception while EXL is set, not setting EPC ]\n", cpu->cpu_id);  */
	} else {
		if (cpu->delay_slot || cpu->nullify_next) {
			reg[COP0_EPC] = cpu->pc_last - 4;
			reg[COP0_CAUSE] |= CAUSE_BD;

			/*  TODO: Should the BD flag actually be set
			    on nullified slots?  */
		} else {
			reg[COP0_EPC] = cpu->pc_last;
			reg[COP0_CAUSE] &= ~CAUSE_BD;
		}
	}

	cpu->delay_slot = NOT_DELAYED;
	cpu->nullify_next = 0;

	if (exc_model == EXC3K) {
		/*  Userspace tlb, vs others:  */
		if (tlb && !(vaddr & 0x80000000ULL) &&
		    (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS) )
			cpu->pc = 0xffffffff80000000ULL;
		else
			cpu->pc = 0xffffffff80000080ULL;
	} else {
		/*  R4000:  */
		if (tlb && (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS)
		    && !(reg[COP0_STATUS] & STATUS_EXL)) {
			if (x_64)
				cpu->pc = 0xffffffff80000080ULL;
			else
				cpu->pc = 0xffffffff80000000ULL;
		} else
			cpu->pc = 0xffffffff80000180ULL;
	}

	if (exc_model == EXC3K) {
		/*  R2000/R3000:  Shift the lowest 6 bits to the left two steps:  */
		reg[COP0_STATUS] =
		    (reg[COP0_STATUS] & ~0x3f) +
		    ((reg[COP0_STATUS] & 0xf) << 2);
	} else {
		/*  R4000:  */
		reg[COP0_STATUS] |= STATUS_EXL;
	}

	/*  Sign-extend:  */
	reg[COP0_CAUSE] = (int64_t)(int32_t)reg[COP0_CAUSE];
	reg[COP0_STATUS] = (int64_t)(int32_t)reg[COP0_STATUS];
}


/*
 *  cpu_run_instr():
 *
 *  Execute one instruction on a cpu.
 *
 *  If we are in a delay slot, set cpu->pc to cpu->delay_jmpaddr after the
 *  instruction is executed.
 *
 *  Return value is the number of instructions executed during this call
 *  to cpu_run_instr() (0 if no instruction was executed).
 */
int cpu_run_instr(struct cpu *cpu)
{
	int quiet_mode_cached = quiet_mode;
	int instruction_trace_cached = cpu->emul->instruction_trace;
	struct coproc *cp0 = cpu->coproc[0];
	int i, ninstrs_executed;
	unsigned char instr[4];
	uint32_t instrword;
	uint64_t cached_pc;
	int hi6, special6, regimm5, rd, rs, rt, sa, imm;
	int copz, which_cache, cache_op;

	int cond, likely, and_link;

	/*  for unaligned load/store  */
	uint64_t dir, is_left, reg_ofs, reg_dir;

	uint64_t tmpvalue, tmpaddr;

	int cpnr;			/*  coprocessor nr  */

	/*  for load/store  */
	uint64_t addr, value, value_hi, result_value;
	int wlen, st, signd, linked;
	unsigned char d[16];		/*  room for at most 128 bits  */


	/*
	 *  Update Coprocessor 0 registers:
	 *
	 *  The COUNT register needs to be updated on every [other] instruction.
	 *  The RANDOM register should decrease for every instruction.
	 */

	if (cpu->cpu_type.exc_model == EXC3K) {
		int r = (cp0->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK) >> R2K3K_RANDOM_SHIFT;
		r --;
		if (r >= cp0->nr_of_tlbs || r < 8)
			r = cp0->nr_of_tlbs-1;
		cp0->reg[COP0_RANDOM] = r << R2K3K_RANDOM_SHIFT;
		/*  TODO: Does the R3000 even have a counter register? :)  */
		/*  cp0->reg[COP0_COUNT] ++;  */
	} else {
		cp0->reg[COP0_RANDOM] --;
		if ((int64_t)cp0->reg[COP0_RANDOM] >= cp0->nr_of_tlbs ||
		    (int64_t)cp0->reg[COP0_RANDOM] < (int64_t) cp0->reg[COP0_WIRED])
			cp0->reg[COP0_RANDOM] = cp0->nr_of_tlbs-1;

		/*  TODO: double count blah blah  */
		cp0->reg[COP0_COUNT] ++;

		if (cp0->reg[COP0_COUNT] == cp0->reg[COP0_COMPARE])
			cpu_interrupt(cpu, 7);
	}


#ifdef ENABLE_INSTRUCTION_DELAYS
	if (cpu->instruction_delay > 0) {
		cpu->instruction_delay --;
		return 1;
	}
#endif

	/*  Cache the program counter in a local variable:  */
	cached_pc = cpu->pc;

	/*  Hardwire the zero register to 0:  */
	cpu->gpr[GPR_ZERO] = 0;

	if (cpu->delay_slot) {
		if (cpu->delay_slot == DELAYED) {
			cached_pc = cpu->pc = cpu->delay_jmpaddr;
			cpu->delay_slot = NOT_DELAYED;
		} else /* if (cpu->delay_slot == TO_BE_DELAYED) */ {
			/*  next instruction will be delayed  */
			cpu->delay_slot = DELAYED;
		}
	}

	if (cpu->last_was_jumptoself > 0)
		cpu->last_was_jumptoself --;

	/*  Check PC dumppoints:  */
	for (i=0; i<cpu->emul->n_dumppoints; i++)
		if (cached_pc == cpu->emul->dumppoint_pc[i]) {
			cpu->emul->instruction_trace =
			    instruction_trace_cached = 1;
			quiet_mode = quiet_mode_cached = 0;
			if (cpu->emul->dumppoint_flag_r[i])
				cpu->emul->register_dump = 1;
		}


	/*  Remember where we are, in case of interrupt or exception:  */
	cpu->pc_last = cached_pc;

	/*
	 *  Any pending interrupts?
	 *
	 *  If interrupts are enabled, and any interrupt has arrived (ie its
	 *  bit in the cause register is set) and corresponding enable bits
	 *  in the status register are set, then cause an interrupt exception
	 *  instead of executing the current instruction.
	 *
	 *  NOTE: cached_interrupt_is_possible is set to 1 whenever an
	 *  interrupt bit in the cause register is set to one (in
	 *  cpu_interrupt()) and set to 0 whenever all interrupt bits are
	 *  cleared (in cpu_interrupt_ack()), so we don't need to do a full
	 *  check each time.
	 */
	if (cpu->cached_interrupt_is_possible && !cpu->nullify_next) {
		if (cpu->cpu_type.exc_model == EXC3K) {
			/*  R3000:  */
			int enabled, mask;
			int status = cp0->reg[COP0_STATUS];

			enabled = status & MIPS_SR_INT_IE;
			mask  = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
			if (enabled && mask) {
				cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
				return 0;
			}
		} else {
			/*  R4000 and others:  */
			int enabled, mask;
			int status = cp0->reg[COP0_STATUS];

			enabled = (status & STATUS_IE)
			    && !(status & STATUS_EXL)
			    && !(status & STATUS_ERL);

			mask = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
			if (enabled && mask) {
				cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
				return 0;
			}
		}
	}


	/*
	 *  ROM emulation:
	 *
	 *  This assumes that a jal was made to a ROM address,
	 *  and we should return via gpr ra.
	 */
	if ((cached_pc & 0xfff00000) == 0xbfc00000 &&
	    cpu->emul->prom_emulation) {
		int rom_jal;
		switch (cpu->emul->emulation_type) {
		case EMULTYPE_DEC:
			decstation_prom_emul(cpu);
			rom_jal = 1;
			break;
		case EMULTYPE_PS2:
			playstation2_sifbios_emul(cpu);
			rom_jal = 1;
			break;
		case EMULTYPE_ARC:
		case EMULTYPE_SGI:
			arcbios_emul(cpu);
			rom_jal = 1;
			break;
		default:
			rom_jal = 0;
		}

		if (rom_jal) {
			cpu->pc = cpu->gpr[GPR_RA];
			/*  no need to update cached_pc, as we're returning  */
			cpu->delay_slot = NOT_DELAYED;

			if (!quiet_mode_cached &&
			    cpu->emul->show_trace_tree)
				cpu->trace_tree_depth --;

			/*  TODO: how many instrs should this count as?  */
			return 1;
		}
	}

#ifdef ALWAYS_SIGNEXTEND_32
	/*
	 *  An extra check for 32-bit mode to make sure that all
	 *  registers are sign-extended:   (Slow, but might be useful
	 *  to detect bugs that have to do with sign-extension.)
	 */
	if (cpu->cpu_type.mmu_model == MMU3K) {
		uint64_t x;

		if (cpu->gpr[0] != 0) {
			fatal("\nWARNING: r0 was not zero! (%016llx)\n\n",
			    (long long)cpu->gpr[0]);
			cpu->gpr[0] = 0;
		}

		/*  Sign-extend ALL registers, including coprocessor registers and tlbs:  */
		for (i=1; i<32; i++) {
			x = cpu->gpr[i];
			cpu->gpr[i] &= 0xffffffff;
			if (cpu->gpr[i] & 0x80000000ULL)
				cpu->gpr[i] |= 0xffffffff00000000ULL;
			if (x != cpu->gpr[i]) {
				fatal("\nWARNING: r%i was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->gpr[i]);
			}
		}
		for (i=0; i<32; i++) {
			x = cpu->coproc[0]->reg[i];
			cpu->coproc[0]->reg[i] &= 0xffffffffULL;
			if (cpu->coproc[0]->reg[i] & 0x80000000ULL)
				cpu->coproc[0]->reg[i] |=
				    0xffffffff00000000ULL;
			if (x != cpu->coproc[0]->reg[i]) {
				fatal("\nWARNING: cop0,r%i was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->coproc[0]->reg[i]);
			}
		}
		for (i=0; i<cpu->coproc[0]->nr_of_tlbs; i++) {
			x = cpu->coproc[0]->tlbs[i].hi;
			cpu->coproc[0]->tlbs[i].hi &= 0xffffffffULL;
			if (cpu->coproc[0]->tlbs[i].hi & 0x80000000ULL)
				cpu->coproc[0]->tlbs[i].hi |=
				    0xffffffff00000000ULL;
			if (x != cpu->coproc[0]->tlbs[i].hi) {
				fatal("\nWARNING: tlb[%i].hi was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->coproc[0]->tlbs[i].hi);
			}

			x = cpu->coproc[0]->tlbs[i].lo0;
			cpu->coproc[0]->tlbs[i].lo0 &= 0xffffffffULL;
			if (cpu->coproc[0]->tlbs[i].lo0 & 0x80000000ULL)
				cpu->coproc[0]->tlbs[i].lo0 |=
				    0xffffffff00000000ULL;
			if (x != cpu->coproc[0]->tlbs[i].lo0) {
				fatal("\nWARNING: tlb[%i].lo0 was not sign-extended correctly (%016llx != %016llx)\n\n",
				    i, (long long)x, (long long)cpu->coproc[0]->tlbs[i].lo0);
			}
		}
	}
#endif

	PREFETCH(cpu->pc_last_host_4k_page + (cached_pc & 0xfff));

#ifdef HALT_IF_PC_ZERO
	/*  Halt if PC = 0:  */
	if (cached_pc == 0) {
		debug("cpu%i: pc=0, halting\n", cpu->cpu_id);
		cpu->running = 0;
		return 0;
	}
#endif



#ifdef BINTRANS

        /*  Caches are not very coozy to handle in bintrans:  */
        switch (cpu->cpu_type.mmu_model) {
        case MMU3K:
                if (cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES) {
			/*  cpu->dont_run_next_bintrans = 1;  */
			cpu->vaddr_to_hostaddr_table0 =
			    cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_SWAP_CACHES?
			       cpu->vaddr_to_hostaddr_table0_cacheisol_i
			     : cpu->vaddr_to_hostaddr_table0_cacheisol_d;
		} else {
			cpu->vaddr_to_hostaddr_table0 =
			    cpu->vaddr_to_hostaddr_table0_kernel;

			/*  TODO: cpu->vaddr_to_hostaddr_table0_user;  */
		}
                break;
        /*  TODO: other cache types  */
        }


#endif


	if (!quiet_mode_cached) {
		/*  Dump CPU registers for debugging:  */
		if (cpu->emul->register_dump) {
			debug("\n");
			cpu_register_dump(cpu);
		}

		/*  Trace tree:  */
		if (cpu->emul->show_trace_tree && cpu->show_trace_delay > 0) {
			cpu->show_trace_delay --;
			if (cpu->show_trace_delay == 0)
				show_trace(cpu, cpu->show_trace_addr);
		}
	}

#ifdef MFHILO_DELAY
	/*  Decrease the MFHI/MFLO delays:  */
	if (cpu->mfhi_delay > 0)
		cpu->mfhi_delay--;
	if (cpu->mflo_delay > 0)
		cpu->mflo_delay--;
#endif

	/*  Read an instruction from memory:  */
#ifdef ENABLE_MIPS16
	if (cpu->mips16 && (cached_pc & 1)) {
		/*  16-bit instruction word:  */
		unsigned char instr16[2];
		int mips16_offset = 0;

		if (!memory_rw(cpu, cpu->mem, cached_pc ^ 1, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
			return 0;

		/*  TODO:  If Reverse-endian is set in the status cop0 register, and
			we are in usermode, then reverse endianness!  */

		/*  The rest of the code is written for little endian, so swap if neccessary:  */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
		}

		cpu->mips16_extend = 0;

		/*
		 *  Translate into 32-bit instruction, little endian (instr[3..0]):
		 *
		 *  This ugly loop is neccessary because if we would get an exception between
		 *  reading an extend instruction and the next instruction, and execution
		 *  continues on the second instruction, the extend data would be lost. So the
		 *  entire instruction (the two parts) need to be read in. If an exception is
		 *  caused, it will appear as if it was caused when reading the extend instruction.
		 */
		while (mips16_to_32(cpu, instr16, instr) == 0) {
			if (instruction_trace_cached)
				debug("cpu%i @ %016llx: %02x%02x\t\t\textend\n",
				    cpu->cpu_id, (cpu->pc_last ^ 1) + mips16_offset,
				    instr16[1], instr16[0]);

			/*  instruction with extend:  */
			mips16_offset += 2;
			if (!memory_rw(cpu, cpu->mem, (cached_pc ^ 1) + mips16_offset, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
				return 0;

			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				int tmp;
				tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
			}
		}

		/*  TODO: bintrans like in 32-bit mode?  */

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr16) + mips16_offset;
		cached_pc = cpu->pc;

		if (instruction_trace_cached) {
			uint64_t offset;
			char *symbol = get_symbol_name(
			    &cpu->emul->symbol_context, cpu->pc_last ^ 1,
			    &offset);
			if (symbol != NULL && offset==0)
				debug("<%s>\n", symbol);

			debug("cpu%i @ %016llx: %02x%02x => %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, (cpu->pc_last ^ 1) + mips16_offset,
			    instr16[1], instr16[0],
			    instr[3], instr[2], instr[1], instr[0],
			    cpu_flags(cpu));
		}
	} else
#endif
	    {
		/*
		 *  Fetch a 32-bit instruction word from memory:
		 *
		 *  1)  The special case of reading an instruction from the
		 *      same host RAM page as the last one is handled here,
		 *      to gain a little bit performance.
		 *
		 *  2)  Fallback to reading from memory the usual way.
		 */
		if (cpu->pc_last_host_4k_page != NULL &&
		    (cached_pc & ~0xfff) == cpu->pc_last_virtual_page) {
			/*  NOTE: This only works on the host if offset is
			    aligned correctly!  (TODO)  */
			*(uint32_t *)instr = *(uint32_t *)
			    (cpu->pc_last_host_4k_page + (cached_pc & 0xfff));
#ifdef BINTRANS
			cpu->pc_bintrans_paddr_valid = 1;
			cpu->pc_bintrans_paddr =
			    cpu->pc_last_physical_page | (cached_pc & 0xfff);
			cpu->pc_bintrans_host_4kpage = cpu->pc_last_host_4k_page;
#endif
                } else {
			if (!memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION))
				return 0;
		}

#ifdef BINTRANS
		if (cpu->dont_run_next_bintrans) {
			cpu->dont_run_next_bintrans = 0;
		} else if (cpu->emul->bintrans_enable &&
		    cpu->pc_bintrans_paddr_valid) {
			int res;
			cpu->bintrans_instructions_executed = 0;
			res = bintrans_attempt_translate(cpu,
			    cpu->pc_bintrans_paddr, 1);

			if (res >= 0) {
				/*  debug("BINTRANS translation + hit,"
				    " pc = %016llx\n", (long long)cached_pc);  */
				if (res > 0 || cpu->pc != cached_pc) {
					if (instruction_trace_cached)
						cpu_disassemble_instr(cpu, instr, 1, 0, 1);
					if (res & BINTRANS_DONT_RUN_NEXT)
						cpu->dont_run_next_bintrans = 1;
					res &= BINTRANS_N_MASK;

					if (cpu->cpu_type.exc_model != EXC3K) {
						if (cp0->reg[COP0_COUNT] < cp0->reg[COP0_COMPARE] &&
						    cp0->reg[COP0_COUNT] + (res-1) >= cp0->reg[COP0_COMPARE])
							cpu_interrupt(cpu, 7);

						cp0->reg[COP0_COUNT] += (res-1);
					}

					return res;
				}
			}
		}
#endif

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr);
		cached_pc = cpu->pc;

		/*
		 *  TODO:  If Reverse-endian is set in the status cop0 register
		 *  and we are in usermode, then reverse endianness!
		 */

		/*
		 *  The rest of the code is written for little endian, so
		 *  swap if neccessary:
		 */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			instrword = instr[0]; instr[0] = instr[3];
			    instr[3] = instrword;
			instrword = instr[1]; instr[1] = instr[2];
			    instr[2] = instrword;
		}

		if (instruction_trace_cached)
			cpu_disassemble_instr(cpu, instr, 1, 0, 0);
	}


	/*
	 *  Nullify this instruction?  (Set by a previous branch-likely
	 *  instruction.)
	 *
	 *  Note: The return value is 1, even if no instruction was actually
	 *  executed.
	 */
	if (cpu->nullify_next) {
		cpu->nullify_next = 0;
		return 1;
	}


	/*
	 *  Execute the instruction:
	 */

	/*  Get the top 6 bits of the instruction:  */
	hi6 = instr[3] >> 2;  	/*  & 0x3f  */

	if (cpu->emul->show_opcode_statistics)
		cpu->stats_opcode[hi6] ++;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;

		if (cpu->emul->show_opcode_statistics)
			cpu->stats__special[special6] ++;

		switch (special6) {
		case SPECIAL_SLL:
		case SPECIAL_SRL:
		case SPECIAL_SRA:
		case SPECIAL_DSLL:
		case SPECIAL_DSRL:
		case SPECIAL_DSRA:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRL32:
		case SPECIAL_DSRA32:
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

			/*
			 *  Check for NOP:
			 *
			 *  The R4000 manual says that a shift amount of zero
			 *  is treated as a nop by some assemblers. Checking
			 *  for sa == 0 here would not be correct, though,
			 *  because instructions such as sll r3,r4,0 are
			 *  possible, and are definitely not a nop.
			 *  Instead, check if the destination register is r0.
			 *
			 *  TODO:  ssnop should wait until the _next_
			 *  cycle boundary, or something like that. The
			 *  code here is incorrect.
			 */
			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (sa == 1) {
					/*  ssnop  */
#ifdef ENABLE_INSTRUCTION_DELAYS
					cpu->instruction_delay +=
					    cpu->cpu_type.
					    instrs_per_cycle - 1;
#endif
				}
				return 1;
			}

			if (special6 == SPECIAL_SLL) {
				switch (sa) {
				case 8:	cpu->gpr[rd] = cpu->gpr[rt] << 8; break;
				case 16:cpu->gpr[rd] = cpu->gpr[rt] << 16; break;
				default:cpu->gpr[rd] = cpu->gpr[rt] << sa;
				}
				/*  Sign-extend rd:  */
				cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rd];
			}
			if (special6 == SPECIAL_DSLL) {
				cpu->gpr[rd] = cpu->gpr[rt] << sa;
			}
			if (special6 == SPECIAL_DSRL) {
				cpu->gpr[rd] = cpu->gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSLL32) {
				cpu->gpr[rd] = cpu->gpr[rt] << (sa + 32);
			}
			if (special6 == SPECIAL_SRL) {
				/*
				 *  Three cases:
				 *	shift amount = zero:  just copy
				 *	high bit of rt zero:  plain shift right (of all bits)
				 *	high bit of rt one:   plain shift right (of lowest 32 bits)
				 */
				if (sa == 0)
					cpu->gpr[rd] = cpu->gpr[rt];
				else if (!(cpu->gpr[rt] & 0x80000000ULL)) {
					cpu->gpr[rd] = cpu->gpr[rt] >> sa;
				} else
					cpu->gpr[rd] = (cpu->gpr[rt] & 0xffffffffULL) >> sa;
			}
			if (special6 == SPECIAL_SRA) {
				int topbit = cpu->gpr[rt] & 0x80000000ULL;
				switch (sa) {
				case 8:	cpu->gpr[rd] = cpu->gpr[rt] >> 8; break;
				case 16:cpu->gpr[rd] = cpu->gpr[rt] >> 16; break;
				default:cpu->gpr[rd] = cpu->gpr[rt] >> sa;
				}
				if (topbit)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_DSRL32) {
				cpu->gpr[rd] = cpu->gpr[rt] >> (sa + 32);
			}
			if (special6 == SPECIAL_DSRA32 || special6 == SPECIAL_DSRA) {
				if (special6 == SPECIAL_DSRA32)
					sa += 32;
				cpu->gpr[rd] = cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & ((uint64_t)1 << 62))		/*  old signbit  */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			return 1;
		case SPECIAL_DSRLV:
		case SPECIAL_DSRAV:
		case SPECIAL_DSLLV:
		case SPECIAL_SLLV:
		case SPECIAL_SRAV:
		case SPECIAL_SRLV:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

			if (special6 == SPECIAL_DSRLV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSRAV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & ((uint64_t)1 << 62))		/*  old sign-bit  */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			if (special6 == SPECIAL_DSLLV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] = cpu->gpr[rd] << sa;
			}
			if (special6 == SPECIAL_SLLV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] = cpu->gpr[rd] << sa;
				/*  Sign-extend rd:  */
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRAV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				/*  Sign-extend rd:  */
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
				}
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRLV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				cpu->gpr[rd] = cpu->gpr[rd] >> sa;
				/*  And finally sign-extend rd:  */
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			return 1;
		case SPECIAL_JR:
			if (cpu->delay_slot) {
				fatal("jr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = cpu->gpr[rs];

			if (!quiet_mode_cached && cpu->emul->show_trace_tree
			    && rs == 31) {
				cpu->trace_tree_depth --;
			}

			return 1;
		case SPECIAL_JALR:
			if (cpu->delay_slot) {
				fatal("jalr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;

			tmpvalue = cpu->gpr[rs];
			cpu->gpr[rd] = cached_pc + 4;
			    /*  already increased by 4 earlier  */

			if (!quiet_mode_cached && cpu->emul->show_trace_tree
			    && rd == 31) {
				cpu->show_trace_delay = 2;
				cpu->show_trace_addr = tmpvalue;
			}

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = tmpvalue;
			return 1;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;

			if (special6 == SPECIAL_MFHI) {
				cpu->gpr[rd] = cpu->hi;
#ifdef MFHILO_DELAY
				cpu->mfhi_delay = 3;
#endif
			}
			if (special6 == SPECIAL_MFLO) {
				cpu->gpr[rd] = cpu->lo;
#ifdef MFHILO_DELAY
				cpu->mflo_delay = 3;
#endif
			}
			return 1;
		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_SLT:
		case SPECIAL_SLTU:
		case SPECIAL_MTLO:
		case SPECIAL_MTHI:
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:
		case SPECIAL_DDIVU:
		case SPECIAL_TGE:
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

#ifdef MFHILO_DELAY
			if (cpu->mflo_delay > 0 && (
			    special6 == SPECIAL_DDIV ||   special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||    special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT ||  special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTLO ||   special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying LO too early after mflo!\n");

			if (cpu->mfhi_delay > 0 && (
			    special6 == SPECIAL_DDIV ||  special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||   special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTHI ||  special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying HI too early after mfhi!\n");
#endif

			if (special6 == SPECIAL_ADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_ADD) {
				/*  According to the MIPS64 manual:  */
				uint64_t temp, temp1, temp2;
				temp1 = cpu->gpr[rs] + ((cpu->gpr[rs] & 0x80000000ULL) << 1);
				temp2 = cpu->gpr[rt] + ((cpu->gpr[rt] & 0x80000000ULL) << 1);
				temp = temp1 + temp2;
#if 0
	/*  TODO: apparently this doesn't work (an example of
	something that breaks is NetBSD/sgimips' mips3_TBIA()  */
				/*  If bits 32 and 31 of temp differ, then it's an overflow  */
				temp1 = temp & 0x100000000ULL;
				temp2 = temp & 0x80000000ULL;
				if ((temp1 && !temp2) || (!temp1 && temp2)) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
#endif
				cpu->gpr[rd] = temp & 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_SUBU) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_SUB) {
				/*  According to the MIPS64 manual:  */
				uint64_t temp, temp1, temp2;
				temp1 = cpu->gpr[rs] + ((cpu->gpr[rs] & 0x80000000ULL) << 1);
				temp2 = cpu->gpr[rt] + ((cpu->gpr[rt] & 0x80000000ULL) << 1);
				temp = temp1 - temp2;
#if 0
				/*  If bits 32 and 31 of temp differ, then it's an overflow  */
				temp1 = temp & 0x100000000ULL;
				temp2 = temp & 0x80000000ULL;
				if ((temp1 && !temp2) || (!temp1 && temp2)) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
#endif
				cpu->gpr[rd] = temp & 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}

			if (special6 == SPECIAL_AND) {
				cpu->gpr[rd] = cpu->gpr[rs] & cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_OR) {
				cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_XOR) {
				cpu->gpr[rd] = cpu->gpr[rs] ^ cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_NOR) {
				cpu->gpr[rd] = ~(cpu->gpr[rs] | cpu->gpr[rt]);
				break;
			}
			if (special6 == SPECIAL_SLT) {
				cpu->gpr[rd] = (int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_SLTU) {
				cpu->gpr[rd] = cpu->gpr[rs] < cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MTLO) {
				cpu->lo = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MTHI) {
				cpu->hi = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MULT) {
				int64_t f1, f2, sum;
				f1 = cpu->gpr[rs] & 0xffffffffULL;
				/*  sign extend f1  */
				if (f1 & 0x80000000ULL)
					f1 |= 0xffffffff00000000ULL;
				f2 = cpu->gpr[rt] & 0xffffffffULL;
				/*  sign extend f2  */
				if (f2 & 0x80000000ULL)
					f2 |= 0xffffffff00000000ULL;
				sum = f1 * f2;

				cpu->lo = sum & 0xffffffffULL;
				cpu->hi = ((uint64_t)sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;

				/*
				 *  NOTE:  The stuff about rd!=0 is just a
				 *  guess, judging from how some NetBSD code
				 *  seems to execute.  It is not documented in
				 *  the MIPS64 ISA docs :-/
				 */

				if (rd != 0) {
					if (cpu->cpu_type.rev != MIPS_R5900)
						debug("WARNING! mult_xx is an undocumented instruction!");
					cpu->gpr[rd] = cpu->lo;
				}
				break;
			}
			if (special6 == SPECIAL_MULTU) {
				uint64_t f1, f2, sum;
				/*  zero extend f1 and f2  */
				f1 = cpu->gpr[rs] & 0xffffffffULL;
				f2 = cpu->gpr[rt] & 0xffffffffULL;
				sum = f1 * f2;
				cpu->lo = sum & 0xffffffffULL;
				cpu->hi = (sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			/*
			 *  TODO:  I'm too tired to think now.  DMULT is probably
			 *  correct, but is DMULTU?  (Unsigned 64x64 multiply.)
			 *  Or, hm, perhaps it is dmult which is incorrect.
			 */
			if (special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU) {
				/*  64x64 = 128 bit multiplication:  SLOW!!!  TODO  */
				uint64_t i, low_add, high_add;

				cpu->lo = cpu->hi = 0;
				for (i=0; i<64; i++) {
					uint64_t bit = cpu->gpr[rt] & ((uint64_t)1 << i);
					if (bit) {
						/*  Add cpu->gpr[rs] to hi and lo:  */
						low_add = (cpu->gpr[rs] << i);
						high_add = (cpu->gpr[rs] >> (64-i));
						if (i==0)			/*  WEIRD BUG in the compiler? Or maybe I'm just stupid  */
							high_add = 0;		/*  these lines are neccessary, a >> 64 doesn't seem to do anything  */
						if (cpu->lo + low_add < cpu->lo)
							cpu->hi ++;
						cpu->lo += low_add;
						cpu->hi += high_add;
					}
				}
				break;
			}
			if (special6 == SPECIAL_DIV) {
				int64_t a, b;
				/*  Signextend rs and rt:  */
				a = cpu->gpr[rs] & 0xffffffffULL;
				if (a & 0x80000000ULL)
					a |= 0xffffffff00000000ULL;
				b = cpu->gpr[rt] & 0xffffffffULL;
				if (b & 0x80000000ULL)
					b |= 0xffffffff00000000ULL;

				if (b == 0) {
					/*  undefined  */
					cpu->lo = cpu->hi = 0;
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffffULL;
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				cpu->hi &= 0xffffffffULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DIVU) {
				int64_t a, b;
				/*  Zero-extend rs and rt:  */
				a = cpu->gpr[rs] & 0xffffffffULL;
				b = cpu->gpr[rt] & 0xffffffffULL;
				if (b == 0) {
					/*  undefined  */
					cpu->lo = cpu->hi = 0;
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffffULL;
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				cpu->hi &= 0xffffffffULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DDIV) {
				if (cpu->gpr[rt] == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = (int64_t)cpu->gpr[rs] / (int64_t)cpu->gpr[rt];
					cpu->hi = (int64_t)cpu->gpr[rs] % (int64_t)cpu->gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_DDIVU) {
				if (cpu->gpr[rt] == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = cpu->gpr[rs] / cpu->gpr[rt];
					cpu->hi = cpu->gpr[rs] % cpu->gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_TGE) {
				if ((int64_t)cpu->gpr[rs] >= (int64_t)cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TGEU) {
				if (cpu->gpr[rs] >= cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLT) {
				if ((int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLTU) {
				if (cpu->gpr[rs] < cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TEQ) {
				if (cpu->gpr[rs] == cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TNE) {
				if (cpu->gpr[rs] != cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_DADD) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_DSUB) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DSUBU) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MOVZ) {
				if (cpu->gpr[rt] == 0)
					cpu->gpr[rd] = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MOVN) {
				if (cpu->gpr[rt] != 0)
					cpu->gpr[rd] = cpu->gpr[rs];
				return 1;
			}
			return 1;
		case SPECIAL_SYNC:
			imm = ((instr[1] & 7) << 2) + (instr[0] >> 6);
			/*  TODO: actually sync  */

			/*  Clear the LLbit (at least on R10000):  */
			cpu->rmw = 0;
			return 1;
		case SPECIAL_SYSCALL:
			imm = ((instr[3] << 24) + (instr[2] << 16) +
			    (instr[1] << 8) + instr[0]) >> 6;
			imm &= 0xfffff;

			if (cpu->emul->userland_emul)
				useremul_syscall(cpu, imm);
			else
				cpu_exception(cpu, EXCEPTION_SYS,
				    0, 0, 0, 0, 0, 0);
			return 1;
		case SPECIAL_BREAK:
			cpu_exception(cpu, EXCEPTION_BP, 0, 0, 0, 0, 0, 0);
			return 1;
		case SPECIAL_MFSA:
			/*  R5900? What on earth does this thing do?  */
			rd = (instr[1] >> 3) & 31;
			/*  TODO  */
			return 1;
		case SPECIAL_MTSA:
			/*  R5900? What on earth does this thing do?  */
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			/*  TODO  */
			return 1;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special6 = 0x%02x\n", special6);
			cpu->running = 0;
			return 1;
		}
		return 1;
	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BNEL:
	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_LUI:
	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_LWL:	/*  Unaligned load/store  */
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)		/*  signed 16-bit  */
			imm -= 65536;

		tmpvalue = imm;		/*  used later in several cases  */

		switch (hi6) {
		case HI6_ADDI:
		case HI6_ADDIU:
		case HI6_DADDI:
		case HI6_DADDIU:
			tmpvalue = cpu->gpr[rs];
			result_value = cpu->gpr[rs] + imm;

			if (hi6 == HI6_ADDI || hi6 == HI6_DADDI) {
				/*
				 *  addi and daddi should trap on overflow:
				 *
				 *  TODO:  This is incorrect? The R4000 manual says
				 *  that overflow occurs if the carry bits out of bit
				 *  62 and 63 differ.   The destination register should
				 *  not be modified on overflow.
				 */
				if (imm >= 0) {
					/*  Turn around from 0x7fff.. to 0x800 ?  Then overflow.  */
					if (   ((hi6 == HI6_ADDI && (result_value &
					    0x80000000ULL) && (tmpvalue &
					    0x80000000ULL)==0))
					    || ((hi6 == HI6_DADDI && (result_value &
					    0x8000000000000000ULL) && (tmpvalue &
					    0x8000000000000000ULL)==0)) ) {
						cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
						break;
					}
				} else {
					/*  Turn around from 0x8000.. to 0x7fff.. ?  Then overflow.  */
					if (   ((hi6 == HI6_ADDI && (result_value &
					    0x80000000ULL)==0 && (tmpvalue &
					    0x80000000ULL)))
					    || ((hi6 == HI6_DADDI && (result_value &
					    0x8000000000000000ULL)==0 && (tmpvalue &
					    0x8000000000000000ULL))) ) {
						cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
						break;
					}
				}
			}

			cpu->gpr[rt] = result_value;

			/*
			 *  Super-ugly speed-hack:  (only if speed_tricks != 0)
			 *  NOTE: This makes the emulation less correct.
			 *
			 *  If we encounter a loop such as:
			 *
			 *	8012f5f4: 1c40ffff      bgtz r0,r2,ffffffff8012f5f4
			 *	8012f5f8: 2442ffff (d)  addiu r2,r2,-1
			 *
			 *  then it is a small loop which simply waits for r2
			 *  to become zero.
			 *
			 *  TODO:  increaste the count register, and cause
			 *  interrupts!!!  For now: return as if we just
			 *  executed 1 instruction.
			 */
			ninstrs_executed = 1;
			if (cpu->emul->speed_tricks && cpu->delay_slot &&
			    cpu->last_was_jumptoself &&
			    cpu->jump_to_self_reg == rt &&
			    cpu->jump_to_self_reg == rs) {
				if ((int64_t)cpu->gpr[rt] > 1 && (int64_t)cpu->gpr[rt] < 0x70000000
				    && (imm >= -30000 && imm <= -1)) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);

					while ((int64_t)cpu->gpr[rt] > 0 && ninstrs_executed < 1000
					    && ((int64_t)cpu->gpr[rt] + (int64_t)imm) > 0) {
						cpu->gpr[rt] += (int64_t)imm;
						ninstrs_executed += 2;
					}

					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);

					/*  TODO: return value, cpu->gpr[rt] * 2;  */
				}
				if ((int64_t)cpu->gpr[rt] > -0x70000000 && (int64_t)cpu->gpr[rt] < -1
				     && (imm >= 1 && imm <= 30000)) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);

					while ((int64_t)cpu->gpr[rt] < 0 && ninstrs_executed < 1000
					    && ((int64_t)cpu->gpr[rt] + (int64_t)imm) < 0) {
						cpu->gpr[rt] += (int64_t)imm;
						ninstrs_executed += 2;
					}

					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);
				}
			}

			if (hi6 == HI6_ADDI || hi6 == HI6_ADDIU) {
				/*  Sign-extend:  */
				cpu->gpr[rt] &= 0xffffffffULL;
				if (cpu->gpr[rt] & 0x80000000ULL)
					cpu->gpr[rt] |= 0xffffffff00000000ULL;
			}
			return ninstrs_executed;
		case HI6_BEQ:
		case HI6_BNE:
		case HI6_BGTZ:
		case HI6_BGTZL:
		case HI6_BLEZ:
		case HI6_BLEZL:
		case HI6_BEQL:
		case HI6_BNEL:
			if (cpu->delay_slot) {
				fatal("b*: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}
			likely = cond = 0;
			switch (hi6) {
			case HI6_BNEL:	likely = 1;
			case HI6_BNE:	cond = (cpu->gpr[rt] != cpu->gpr[rs]);
					break;
			case HI6_BEQL:	likely = 1;
			case HI6_BEQ:	cond = (cpu->gpr[rt] == cpu->gpr[rs]);
					break;
			case HI6_BLEZL:	likely = 1;
			case HI6_BLEZ:	cond = ((int64_t)cpu->gpr[rs] <= 0);
					break;
			case HI6_BGTZL:	likely = 1;
			case HI6_BGTZ:	cond = ((int64_t)cpu->gpr[rs] > 0);
					break;
			}

			if (cond) {
				cpu->delay_slot = TO_BE_DELAYED;
				cpu->delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->nullify_next = 1;		/*  nullify delay slot  */
			}

			if (imm==-1 && (hi6 == HI6_BGTZ || hi6 == HI6_BLEZ ||
			    (hi6 == HI6_BGTZL && cond) ||
			    (hi6 == HI6_BLEZL && cond) ||
			    (hi6 == HI6_BNE && (rt==0 || rs==0)) ||
			    (hi6 == HI6_BEQ && (rt==0 || rs==0)))) {
				cpu->last_was_jumptoself = 2;
				if (rs == 0)
					cpu->jump_to_self_reg = rt;
				else
					cpu->jump_to_self_reg = rs;
			}
			return 1;
		case HI6_LUI:
			cpu->gpr[rt] = (imm << 16);
			/*  No sign-extending neccessary, as imm already
			    was sign-extended if it was negative.  */
			break;
		case HI6_SLTI:
			cpu->gpr[rt] = (int64_t)cpu->gpr[rs] < (int64_t)tmpvalue;
			break;
		case HI6_SLTIU:
			cpu->gpr[rt] = cpu->gpr[rs] < (uint64_t)imm;
			break;
		case HI6_ANDI:
			cpu->gpr[rt] = cpu->gpr[rs] & (tmpvalue & 0xffff);
			break;
		case HI6_ORI:
			cpu->gpr[rt] = cpu->gpr[rs] | (tmpvalue & 0xffff);
			break;
		case HI6_XORI:
			cpu->gpr[rt] = cpu->gpr[rs] ^ (tmpvalue & 0xffff);
			break;
		case HI6_LB:
		case HI6_LBU:
		case HI6_LH:
		case HI6_LHU:
		case HI6_LW:
		case HI6_LWU:
		case HI6_LD:
		case HI6_LQ_MDMX:
		case HI6_LWC1:
		case HI6_LWC2:
		case HI6_LWC3:	/*  pref  */
		case HI6_LDC1:
		case HI6_LDC2:
		case HI6_LL:
		case HI6_LLD:
		case HI6_SB:
		case HI6_SH:
		case HI6_SW:
		case HI6_SD:
		case HI6_SQ:
		case HI6_SC:
		case HI6_SCD:
		case HI6_SWC1:
		case HI6_SWC2:
		case HI6_SWC3:
		case HI6_SDC1:
			/*  These are the default "assumptions".  */
			linked = 0;
			st = 1;
			signd = 1;
			wlen = 4;

			switch (hi6) {
			/*  The most common ones:  */
			case HI6_LW:	{           st = 0;            }  break;
			case HI6_SW:	{                   signd = 0; }  break;

			case HI6_LB:	{ wlen = 1; st = 0;            }  break;
			case HI6_LBU:	{ wlen = 1; st = 0; signd = 0; }  break;
			case HI6_SB:	{ wlen = 1;         signd = 0; }  break;

			case HI6_LD:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_SD:	{ wlen = 8;         signd = 0; }  break;

			case HI6_LQ_MDMX:	{ wlen = 16; st = 0; signd = 0; }  break;	/*  R5900, otherwise MDMX (TODO)  */
			case HI6_SQ:		{ wlen = 16;         signd = 0; }  break;	/*  R5900 ?  */

			/*  The rest:  */
			case HI6_LH:	{ wlen = 2; st = 0;            }  break;
			case HI6_LHU:	{ wlen = 2; st = 0; signd = 0; }  break;
			case HI6_LWU:	{           st = 0; signd = 0; }  break;
			case HI6_LWC1:	{           st = 0;            }  break;
			case HI6_LWC2:	{           st = 0;            }  break;
			case HI6_LWC3:	{           st = 0;            }  break;
			case HI6_LDC1:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_LDC2:	{ wlen = 8; st = 0; signd = 0; }  break;

			case HI6_SH:	{ wlen = 2;         signd = 0; }  break;
			case HI6_SWC1:	{                   signd = 0; }  break;
			case HI6_SWC2:	{                   signd = 0; }  break;
			case HI6_SWC3:	{                   signd = 0; }  break;
			case HI6_SDC1:	{ wlen = 8;         signd = 0; }  break;

			case HI6_LL:	{           st = 0; signd = 1; linked = 1; }  break;
			case HI6_LLD:	{ wlen = 8; st = 0; signd = 0; linked = 1; }  break;

			case HI6_SC:	{                   signd = 1; linked = 1; }  break;
			case HI6_SCD:	{ wlen = 8;         signd = 0; linked = 1; }  break;

			default:
				fatal("cannot be here\n");
				wlen = 4; st = 0; signd = 0;
			}

			/*
			 *  In the MIPS IV ISA, the 'lwc3' instruction is changed into 'pref'.
			 *  The pref instruction is emulated by not doing anything. :-)  TODO
			 */
			if (hi6 == HI6_LWC3 && cpu->cpu_type.isa_level >= 4) {
				/*  Clear the LLbit (at least on R10000):  */
				cpu->rmw = 0;
				break;
			}

			addr = cpu->gpr[rs] + imm;

			/*  Check for natural alignment:  */
			if ((addr & (wlen - 1)) != 0) {
				cpu_exception(cpu, st? EXCEPTION_ADES : EXCEPTION_ADEL,
				    0, addr, 0, 0, 0, 0);
				break;
			}

#if 0
			if (cpu->cpu_type.isa_level == 4 && (imm & (wlen - 1)) != 0)
				debug("WARNING: low bits of imm value not zero! (MIPS IV) "
				    "pc=%016llx", (long long)cpu->pc_last);
#endif

			/*
			 *  Load Linked: This initiates a Read-Modify-Write
			 *  sequence.
			 */
			if (linked) {
				if (st==0) {
					/*  st == 0:  Load  */
					cpu->rmw      = 1;
					cpu->rmw_addr = addr;
					cpu->rmw_len  = wlen;

					/*
					 *  COP0_LLADDR is updated for
					 *  diagnostic purposes, except for
					 *  CPUs in the R10000 family.
					 */
					if (cpu->cpu_type.exc_model != MMU10K)
						cp0->reg[COP0_LLADDR] =
						    (addr >> 4) & 0xffffffffULL;
				} else {
					/*
					 *  st == 1:  Store
					 *  If rmw is 0, then the store failed.
					 *  (This cache-line was written to by
					 *  someone else.)
					 */
					if (cpu->rmw == 0) {
						/*  The store failed:  */
						cpu->gpr[rt] = 0;

						/*
						 *  Operating systems that make
						 *  use of ll/sc for synchro-
						 *  nization should implement
						 *  back-off protocols of their
						 *  own, so there's no backoff
						 *  here.
						 */
						break;
					}
				}
			} else {
				/*
				 *  If any kind of load or store occurs between
				 *  an ll and an sc, then the ll-sc sequence
				 *  should fail.  (This is local to each cpu.)
				 */
				cpu->rmw = 0;
			}

			value_hi = 0;

			if (st) {
				/*  store:  */
				int cpnr, success;

				if (hi6 == HI6_SWC3 || hi6 == HI6_SWC2 ||
				    hi6 == HI6_SDC1 || hi6 == HI6_SWC1) {
					cpnr = 1;
					switch (hi6) {
					case HI6_SWC3:	cpnr++;		/*  fallthrough  */
					case HI6_SWC2:	cpnr++;
					case HI6_SDC1:
					case HI6_SWC1:	if (cpu->coproc[cpnr] == NULL ||
							    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
							    ) {
								cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
								cpnr = -1;
								break;
							} else {
								coproc_register_read(cpu, cpu->coproc[cpnr], rt, &value);
							}
							break;
					default:
							;
					}
					if (cpnr < 0)
						break;
				} else
					value = cpu->gpr[rt];

				if (wlen == 4) {
					/*  Special case for 32-bit stores... (perhaps not worth it)  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						d[0] = value & 0xff;         d[1] = (value >> 8) & 0xff;
						d[2] = (value >> 16) & 0xff; d[3] = (value >> 24) & 0xff;
					} else {
						d[3] = value & 0xff;         d[2] = (value >> 8) & 0xff;
						d[1] = (value >> 16) & 0xff; d[0] = (value >> 24) & 0xff;
					}
				} else if (wlen == 16) {
					value_hi = cpu->gpr_quadhi[rt];
					/*  Special case for R5900 128-bit stores:  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
						for (i=0; i<8; i++) {
							d[i] = (value >> (i*8)) & 255;
							d[i+8] = (value_hi >> (i*8)) & 255;
						}
					else
						for (i=0; i<8; i++) {
							d[i] = (value >> ((wlen-1-i)*8)) & 255;
							d[i + 8] = (value_hi >> ((wlen-1-i)*8)) & 255;
						}
				} else if (wlen == 1) {
					d[0] = value & 0xff;
				} else {
					/*  General case:  */
					uint64_t v = value;
					if (cpu->byte_order ==
					    EMUL_LITTLE_ENDIAN)
						for (i=0; i<wlen; i++) {
							d[i] = v & 255;
							v >>= 8;
						}
					else
						for (i=0; i<wlen; i++) {
							d[wlen-1-i] = v & 255;
							v >>= 8;
						}
				}

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_WRITE, CACHE_DATA);
				if (!success) {
					/*  The store failed, and might have caused an exception.  */
					if (instruction_trace_cached)
						debug("(failed)]\n");
					break;
				}
			} else {
				/*  load:  */
				int cpnr = 1;
				int success;

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_READ, CACHE_DATA);
				if (!success) {
					/*  The load failed, and might have caused an exception.  */
					if (instruction_trace_cached)
						debug("(failed)]\n");
					break;
				}

				if (wlen == 1)
					value = d[0] | (signd && (d[0]&128)? (-1 << 8) : 0);
				else if (wlen != 16) {
					/*  General case (except for 128-bit):  */
					int i;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						if (signd && (d[wlen-1] & 128)!=0)	/*  sign extend  */
							value = -1;
						for (i=wlen-1; i>=0; i--) {
							value <<= 8;
							value += d[i];
						}
					} else {
						if (signd && (d[0] & 128)!=0)		/*  sign extend  */
							value = -1;
						for (i=0; i<wlen; i++) {
							value <<= 8;
							value += d[i];
						}
					}
				} else {
					/*  R5900 128-bit quadword:  */
					int i;
					value_hi = 0;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						for (i=wlen-1; i>=0; i--) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					} else {
						for (i=0; i<wlen; i++) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					}
					cpu->gpr_quadhi[rt] = value_hi;
				}

				switch (hi6) {
				case HI6_LWC3:	cpnr++;		/*  fallthrough  */
				case HI6_LDC2:
				case HI6_LWC2:	cpnr++;
				case HI6_LDC1:
				case HI6_LWC1:	if (cpu->coproc[cpnr] == NULL ||
						    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
						    ) {
							cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
						} else {
							coproc_register_write(cpu, cpu->coproc[cpnr], rt, &value,
							    hi6==HI6_LDC1 || hi6==HI6_LDC2);
						}
						break;
				default:	if (rt != 0)
							cpu->gpr[rt] = value;
				}
			}

			if (linked && st==1) {
				/*
				 *  The store succeeded. Invalidate any other
				 *  cpu's store to this cache line, and then
				 *  return 1 in gpr rt:
				 *
				 *  (this is a semi-ugly hack using global
				 * 'cpus')
				 *
				 *  TODO: How about invalidating other CPUs
				 *  stores to this cache line, even if this
				 *  was _NOT_ a linked store?
				 */
				for (i=0; i<cpu->emul->ncpus; i++) {
					if (cpu->emul->cpus[i]->rmw) {
						uint64_t yaddr = addr;
						uint64_t xaddr =
						    cpu->emul->cpus[i]->rmw_addr;
						uint64_t mask;
						mask = ~(cpu->emul->cpus[i]->
						    cache_linesize[CACHE_DATA]
						    - 1);
						xaddr &= mask;
						yaddr &= mask;
						if (xaddr == yaddr) {
							cpu->emul->cpus[i]->rmw = 0;
							cpu->emul->cpus[i]->rmw_addr = 0;
						}
					}
				}

				if (rt != 0)
					cpu->gpr[rt] = 1;
				cpu->rmw = 0;
			}

			if (instruction_trace_cached) {
				char *t;
				switch (wlen) {
				case 2:		t = "0x%04llx"; break;
				case 4:		t = "0x%08llx"; break;
				case 8:		t = "0x%016llx"; break;
				case 16:	debug("0x%016llx", (long long)value_hi);
						t = "%016llx"; break;
				default:	t = "0x%02llx";
				}
				debug(t, (long long)value);
				debug("]\n");
			}
			return 1;
		case HI6_LWL:	/*  Unaligned load/store  */
		case HI6_LWR:
		case HI6_LDL:
		case HI6_LDR:
		case HI6_SWL:
		case HI6_SWR:
		case HI6_SDL:
		case HI6_SDR:
			/*  For L (Left):   address is the most significant byte  */
			/*  For R (Right):  address is the least significant byte  */
			addr = cpu->gpr[rs] + imm;

			is_left = 0;
			if (hi6 == HI6_SWL || hi6 == HI6_LWL ||
			    hi6 == HI6_SDL || hi6 == HI6_LDL)
				is_left = 1;

			wlen = 0; st = 0;
			signd = 0;
			if (hi6 == HI6_LWL || hi6 == HI6_LWR)
				signd = 1;

			if (hi6 == HI6_LWL || hi6 == HI6_LWR)	{ wlen = 4; st = 0; }
			if (hi6 == HI6_SWL || hi6 == HI6_SWR)	{ wlen = 4; st = 1; }
			if (hi6 == HI6_LDL || hi6 == HI6_LDR)	{ wlen = 8; st = 0; }
			if (hi6 == HI6_SDL || hi6 == HI6_SDR)	{ wlen = 8; st = 1; }

			dir = 1;		/*  big endian, Left  */
			reg_dir = -1;
			reg_ofs = wlen - 1;	/*  byte offset in the register  */
			if (!is_left) {
				dir = -dir;
				reg_ofs = 0;
				reg_dir = 1;
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				dir = -dir;

			result_value = cpu->gpr[rt];

			/*  Try to load and store, to make sure that all bytes in this store
			    will be allowed to go through:  */
			if (st) {
				for (i=0; i<wlen; i++) {
					unsigned char databyte;
					int ok;

					tmpaddr = addr + i*dir;
					/*  Have we moved into another word/dword? Then stop:  */
					if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
						break;

					/*  Load and store one byte:  */
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_READ, CACHE_DATA);
					if (!ok)
						return 1;
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_WRITE, CACHE_DATA);
					if (!ok)
						return 1;
				}
			}

			for (i=0; i<wlen; i++) {
				unsigned char databyte;
				int ok;

				tmpaddr = addr + i*dir;
				/*  Have we moved into another word/dword? Then stop:  */
				if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
					break;

				/*  debug("unaligned byte at %016llx, reg_ofs=%i reg=0x%016llx\n",
				    tmpaddr, reg_ofs, (long long)result_value);  */

				/*  Load or store one byte:  */
				if (st) {
					databyte = (result_value >> (reg_ofs * 8)) & 255;
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_WRITE, CACHE_DATA);
					/*  if (instruction_trace_cached)
						debug("%02x ", databyte);  */
				} else {
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_READ, CACHE_DATA);
					/*  if (instruction_trace_cached)
						debug("%02x ", databyte);  */
					result_value &= ~((uint64_t)0xff << (reg_ofs * 8));
					result_value |= (uint64_t)databyte << (reg_ofs * 8);
				}

				/*  Return immediately if exception.  */
				if (!ok)
					return 1;

				reg_ofs += reg_dir;
			}

			if (!st && rt != 0)
				cpu->gpr[rt] = result_value;

			/*  Sign extend for 32-bit load lefts:  */
			if (!st && signd && wlen == 4) {
				cpu->gpr[rt] &= 0xffffffffULL;
				if (cpu->gpr[rt] & 0x80000000ULL)
					cpu->gpr[rt] |= 0xffffffff00000000ULL;
			}

			if (instruction_trace_cached) {
				char *t;
				switch (wlen) {
				case 2:		t = "0x%04llx"; break;
				case 4:		t = "0x%08llx"; break;
				case 8:		t = "0x%016llx"; break;
				default:	t = "0x%02llx";
				}
				debug(t, (long long)cpu->gpr[rt]);
				debug("]\n");
			}

			return 1;
		}
		return 1;
	case HI6_REGIMM:
		regimm5 = instr[2] & 0x1f;

		if (cpu->emul->show_opcode_statistics)
			cpu->stats__regimm[regimm5] ++;

		switch (regimm5) {
		case REGIMM_BLTZ:
		case REGIMM_BGEZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)		/*  signed 16-bit  */
				imm -= 65536;

			cond = and_link = likely = 0;

			switch (regimm5) {
			case REGIMM_BLTZL:	likely = 1;
			case REGIMM_BLTZ:	cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZL:	likely = 1;
			case REGIMM_BGEZ:	cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;

			case REGIMM_BLTZALL:	likely = 1;
			case REGIMM_BLTZAL:	and_link = 1;
						cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZALL:	likely = 1;
			case REGIMM_BGEZAL:	and_link = 1;
						cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;
			}

			if (and_link)
				cpu->gpr[31] = cached_pc + 4;

			if (cond) {
				cpu->delay_slot = TO_BE_DELAYED;
				cpu->delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->nullify_next = 1;		/*  nullify delay slot  */
			}

			return 1;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented regimm5 = 0x%02x\n", regimm5);
			cpu->running = 0;
			return 1;
		}
		/*  NOT REACHED  */
	case HI6_J:
	case HI6_JAL:
		if (cpu->delay_slot) {
			fatal("j/jal: jump inside a jump's delay slot, or similar. TODO\n");
			cpu->running = 0;
			return 1;
		}
		imm = ((instr[3] & 3) << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm <<= 2;

		if (hi6 == HI6_JAL)
			cpu->gpr[31] = cached_pc + 4;		/*  pc already increased by 4 earlier  */

		addr = cached_pc & ~((1 << 28) - 1);
		addr |= imm;

		cpu->delay_slot = TO_BE_DELAYED;
		cpu->delay_jmpaddr = addr;

		if (!quiet_mode_cached && cpu->emul->show_trace_tree &&
		    hi6 == HI6_JAL) {
			cpu->show_trace_delay = 2;
			cpu->show_trace_addr = addr;
		}

		return 1;
	case HI6_COP0:
	case HI6_COP1:
	case HI6_COP2:
	case HI6_COP3:
		imm = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm &= ((1 << 26) - 1);

		cpnr = 0;
		if (hi6 == HI6_COP0)	cpnr = 0;
		if (hi6 == HI6_COP1)	cpnr = 1;
		if (hi6 == HI6_COP2)	cpnr = 2;
		if (hi6 == HI6_COP3)	cpnr = 3;

		/*
		 *  If there is no coprocessor nr cpnr, or we are running in
		 *  userland and the coprocessor is not marked as Useable in
		 *  the status register of CP0, then we get an exception:
		 *
		 *  TODO:  More robust checking for user code (ie R4000 stuff)
		 */
		if (cpu->coproc[cpnr] == NULL ||
		    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
		    ) {
			if (instruction_trace_cached)
				debug("cop%i\t0x%08x => coprocessor unusable\n", cpnr, (int)imm);

			cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		} else {
			/*
			 *  Execute the coprocessor function. The
			 *  coproc_function code outputs instruction
			 *  trace, if neccessary.
			 */
			coproc_function(cpu, cpu->coproc[cpnr], imm, 0, 1);
		}
		return 1;
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5);	/*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];

		cache_op    = copz >> 2;
		which_cache = copz & 3;

		/*
		 *  TODO:  The cache instruction is implementation dependant.
		 *  This is really ugly.
		 */

/*		if (cpu->cpu_type.mmu_model == MMU10K) {  */
/*			printf("taghi=%08lx taglo=%08lx\n",
			    (long)cp0->reg[COP0_TAGDATA_HI],
			    (long)cp0->reg[COP0_TAGDATA_LO]);
*/
			if (cp0->reg[COP0_TAGDATA_HI] == 0 &&
			    cp0->reg[COP0_TAGDATA_LO] == 0) {
				/*  Normal cache operation:  */
				cpu->r10k_cache_disable_TODO = 0;
			} else {
				/*  Dislocate the cache:  */
				cpu->r10k_cache_disable_TODO = 1;
			}
/*		}  */

		/*  Clear the LLbit (at least on R10000):  */
		cpu->rmw = 0;

		return 1;
	case HI6_SPECIAL2:
		special6 = instr[0] & 0x3f;

		if (cpu->emul->show_opcode_statistics)
			cpu->stats__special2[special6] ++;

		instrword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];

		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;

		/*  printf("special2 %08x  rs=0x%02x rt=0x%02x rd=0x%02x\n", instrword, rs,rt,rd);  */

		/*
		 *  Many of these can be found in the R5000 docs, or figured out
		 *  by studying binutils source code for MIPS instructions.
		 */

		if ((instrword & 0xfc0007ffULL) == 0x70000000) {
			{
				int32_t a, b;
				int64_t c;
				a = (int32_t)cpu->gpr[rs];
				b = (int32_t)cpu->gpr[rt];
				c = a * b;
				c += (cpu->lo & 0xffffffffULL)
				    + (cpu->hi << 32);
				cpu->lo = (int64_t)((int32_t)c);
				cpu->hi = (int64_t)((int32_t)(c >> 32));

				/*
				 *  The R5000 manual says that rd should be all zeros,
				 *  but it isn't on R5900.   I'm just guessing here that
				 *  it stores the value in register rd, in addition to hi/lo.
				 *  TODO
				 */
				if (rd != 0)
					cpu->gpr[rd] = cpu->lo;
			}
		} else if ((instrword & 0xffff07ffULL) == 0x70000209
		    || (instrword & 0xffff07ffULL) == 0x70000249) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmfhi/pmflo rd
			 *
			 *  If the lowest 8 bits of the instruction word are 0x09, it's a pmfhi.
			 *  If the lowest bits are 0x49, it's a pmflo.
			 *
			 *  A wild guess is that this is a 128-bit version of mfhi/mflo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x49) {
				cpu->gpr[rd] = cpu->lo;
			} else {
				cpu->gpr[rd] = cpu->hi;
			}
		} else if ((instrword & 0xfc1fffff) == 0x70000269 || (instrword & 0xfc1fffff) == 0x70000229) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmthi/pmtlo rs		(pmtlo = 269, pmthi = 229)
			 *
			 *  A wild guess is that this is a 128-bit version of mthi/mtlo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x69) {
				cpu->lo = cpu->gpr[rs];
			} else {
				cpu->hi = cpu->gpr[rs];
			}
		} else if ((instrword & 0xfc0007ff) == 0x700004a9) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	por dst,src,src2  ==> rs=src rt=src2 rd=dst
			 *
			 *  A wild guess is that this is a 128-bit "or" between two registers.
			 *  For now, let's just or using 64-bits.  (TODO)
			 */
			cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
		} else if ((instrword & 0xfc0007ff) == 0x70000488) {
			/*
			 *  R5900 "undocumented" pextlw. TODO: find out if this is correct.
			 *  It seems that this instruction is used to combine two 32-bit
			 *  words into a 64-bit dword, typically before a sd (store dword).
			 */
			cpu->gpr[rd] =
			    ((cpu->gpr[rs] & 0xffffffffULL) << 32)		/*  TODO: switch rt and rs?  */
			    | (cpu->gpr[rt] & 0xffffffffULL);
		} else if (special6 == SPECIAL2_MUL) {
			cpu->gpr[rd] = (int64_t)cpu->gpr[rt] *
			    (int64_t)cpu->gpr[rs];
		} else if (special6 == SPECIAL2_CLZ) {
			/*  clz: count leading zeroes  */
			int i, n=0;
			for (i=31; i>=0; i--) {
				if (cpu->gpr[rs] & ((uint32_t)1 << i))
					break;
				else
					n++;
			}
			cpu->gpr[rd] = n;
		} else if (special6 == SPECIAL2_CLO) {
			/*  clo: count leading ones  */
			int i, n=0;
			for (i=31; i>=0; i--) {
				if (cpu->gpr[rs] & ((uint32_t)1 << i))
					n++;
				else
					break;
			}
			cpu->gpr[rd] = n;
		} else if (special6 == SPECIAL2_DCLZ) {
			/*  dclz: count leading zeroes  */
			int i, n=0;
			for (i=63; i>=0; i--) {
				if (cpu->gpr[rs] & ((uint64_t)1 << i))
					break;
				else
					n++;
			}
			cpu->gpr[rd] = n;
		} else if (special6 == SPECIAL2_DCLO) {
			/*  dclo: count leading ones  */
			int i, n=0;
			for (i=63; i>=0; i--) {
				if (cpu->gpr[rs] & ((uint64_t)1 << i))
					n++;
				else
					break;
			}
			cpu->gpr[rd] = n;
		} else {
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special_2 = 0x%02x, rs=0x%02x rt=0x%02x rd=0x%02x\n",
			    special6, rs, rt, rd);
			cpu->running = 0;
			return 1;
		}
		return 1;
	default:
		if (!instruction_trace_cached) {
			fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
		fatal("unimplemented hi6 = 0x%02x\n", hi6);
		cpu->running = 0;
		return 1;
	}

	/*  NOTREACHED  */
}


/*
 *  cpu_show_cycles():
 *
 *  If automatic adjustment of clock interrupts is turned on, then recalculate
 *  emulated_hz.  Also, if show_nr_of_instructions is on, then print a
 *  line to stdout about how many instructions/cycles have been executed so
 *  far.
 */
void cpu_show_cycles(struct emul *emul,
	struct timeval *starttime, int64_t ncycles, int forced)
{
	uint64_t offset;
	char *symbol;
	int64_t mseconds, ninstrs;
	struct timeval tv;
	int h, m, s, ms, d;

	static int64_t mseconds_last = 0;
	static int64_t ninstrs_last = -1;

	gettimeofday(&tv, NULL);
	mseconds = (tv.tv_sec - starttime->tv_sec) * 1000
	         + (tv.tv_usec - starttime->tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	if (mseconds - mseconds_last == 0)
		mseconds ++;

	ninstrs = ncycles * emul->cpus[emul->bootstrap_cpu]->cpu_type.instrs_per_cycle;

	if (emul->automatic_clock_adjustment) {
		static int first_adjustment = 1;

		/*  Current nr of cycles per second:  */
		int64_t cur_cycles_per_second = 1000 *
		    (ninstrs-ninstrs_last) / (mseconds-mseconds_last)
		    / emul->cpus[emul->bootstrap_cpu]->cpu_type.instrs_per_cycle;

		if (cur_cycles_per_second < 1500000)
			cur_cycles_per_second = 1500000;

		if (first_adjustment) {
			emul->emulated_hz = cur_cycles_per_second;
			first_adjustment = 0;
		} else
			emul->emulated_hz = (31 * emul->emulated_hz +
			    cur_cycles_per_second) / 32;

		debug("[ updating emulated_hz to %lli Hz ]\n",
		    (long long)emul->emulated_hz);
	}


	/*  RETURN here, unless show_nr_of_instructions (-N) is turned on:  */
	if (!emul->show_nr_of_instructions && !forced)
		goto do_return;


	printf("[ ");

	if (!emul->automatic_clock_adjustment) {
		d = emul->emulated_hz / 1000;
		if (d < 1)
			d = 1;
		ms = ncycles / d;
		h = ms / 3600000;
		ms -= 3600000 * h;
		m = ms / 60000;
		ms -= 60000 * m;
		s = ms / 1000;
		ms -= 1000 * s;

		printf("emulated time = %02i:%02i:%02i.%03i; ", h, m, s, ms);
	}

	printf("cycles=%lli", (long long) ncycles);

	if (emul->cpus[emul->bootstrap_cpu]->cpu_type.instrs_per_cycle > 1)
		printf(" (%lli instrs)", (long long) ninstrs);

	/*  Instructions per second, and average so far:  */
	printf("; i/s=%lli avg=%lli",
	    (long long) ((long long)1000 * (ninstrs-ninstrs_last)
		/ (mseconds-mseconds_last)),
	    (long long) ((long long)1000 * ninstrs / mseconds));

	symbol = get_symbol_name(&emul->symbol_context,
	    emul->cpus[emul->bootstrap_cpu]->pc, &offset);

	if (emul->cpus[emul->bootstrap_cpu]->cpu_type.isa_level < 3 ||
	    emul->cpus[emul->bootstrap_cpu]->cpu_type.isa_level == 32)
		printf("; pc=%08x",
		    (int)emul->cpus[emul->bootstrap_cpu]->pc);
	else
		printf("; pc=%016llx",
		    (long long)emul->cpus[emul->bootstrap_cpu]->pc);

	printf(" <%s> ]\n", symbol? symbol : "no symbol");

do_return:
	ninstrs_last = ninstrs;
	mseconds_last = mseconds;
}


/*
 *  cpu_run():
 *
 *  Run instructions from all CPUs, until all CPUs have halted.
 */
int cpu_run(struct emul *emul, struct cpu **cpus, int ncpus)
{
	int te;
	int64_t max_instructions_cached = emul->max_instructions;
	int64_t max_random_cycles_per_chunk_cached =
	    emul->max_random_cycles_per_chunk;
	int64_t ncycles = 0, ncycles_chunk_end, ncycles_show = 0;
	int64_t ncycles_flush = 0, ncycles_flushx11 = 0;
		/*  TODO: how about overflow of ncycles?  */
	int running, ncpus_cached = ncpus;
	struct timeval starttime;
	int a_few_cycles = 1048576, a_few_instrs;

	/*
	 *  Instead of doing { one cycle, check hardware ticks }, we
	 *  can do { n cycles, check hardware ticks }, as long as
	 *  n is at most as much as the lowest number of cycles/tick
	 *  for any hardware device.
	 */
	for (te=0; te<cpus[0]->n_tick_entries; te++) {
		if (cpus[0]->ticks_reset_value[te] < a_few_cycles)
			a_few_cycles = cpus[0]->ticks_reset_value[te];
	}

	a_few_cycles >>= 1;
	if (a_few_cycles < 1)
		a_few_cycles = 1;

	if (ncpus > 1 && max_random_cycles_per_chunk_cached == 0)
		a_few_cycles = 1;

	/*  debug("cpu_run(): a_few_cycles = %i\n", a_few_cycles);  */

	/*  For performance measurement:  */
	gettimeofday(&starttime, NULL);

	/*  The main loop:  */
	running = 1;
	while (running) {
		ncycles_chunk_end = ncycles + (1 << 15);

		a_few_instrs = a_few_cycles *
		    cpus[0]->cpu_type.instrs_per_cycle;

		/*  Do a chunk of cycles:  */
		do {
			int i, j, te, cpu0instrs, a_few_instrs2;

			running = 0;
			cpu0instrs = 0;

			/*
			 *  Run instructions from each CPU:
			 */

			/*  Is any cpu alive?  */
			for (i=0; i<ncpus_cached; i++)
				if (cpus[i]->running)
					running = 1;

			if (emul->single_step) {
				if (emul->single_step == 1) {
					old_instruction_trace =
					    emul->instruction_trace;
					old_quiet_mode =
					    quiet_mode;
					old_show_trace_tree =
					    emul->show_trace_tree;
					emul->instruction_trace = 1;
					emul->show_trace_tree = 1;
					quiet_mode = 0;
					emul->single_step = 2;
				}

				for (i=0; i<ncpus_cached; i++) {
					for (j=0;
					  j<cpus[i]->cpu_type.instrs_per_cycle;
					    j++) {
						int instrs_run = cpu_run_instr(cpus[i]);
						if (i == 0)
							cpu0instrs += instrs_run;
						if (emul->single_step)
							debugger();
					}
				}
			} else if (max_random_cycles_per_chunk_cached > 0) {
				for (i=0; i<ncpus_cached; i++)
					if (cpus[i]->running) {
						a_few_instrs2 = a_few_cycles;
						if (a_few_instrs2 >= max_random_cycles_per_chunk_cached)
							a_few_instrs2 = max_random_cycles_per_chunk_cached;
						j = (random() % a_few_instrs2) + 1;
						j *= cpus[i]->cpu_type.instrs_per_cycle;
						while (j-- >= 1 && cpus[i]->running) {
							int instrs_run = cpu_run_instr(cpus[i]);
							if (i == 0)
								cpu0instrs += instrs_run;
						}
					}
			} else {
				/*  CPU 0 is special, cpu0instr must be updated.  */
				for (j=0; j<a_few_instrs; ) {
					int instrs_run;
					if (!cpus[0]->running)
						break;
					do {
						instrs_run =
						    cpu_run_instr(cpus[0]);
					} while (instrs_run == 0);
					j += instrs_run;
					cpu0instrs += instrs_run;
				}

				/*  CPU 1 and up:  */
				for (i=1; i<ncpus_cached; i++) {
					a_few_instrs2 = a_few_cycles *
					    cpus[i]->cpu_type.instrs_per_cycle;
					for (j=0; j<a_few_instrs2; )
						if (cpus[i]->running) {
							int instrs_run = 0;
							while (!instrs_run)
								instrs_run = cpu_run_instr(cpus[i]);
							j += instrs_run;
						} else
							break;
				}
			}

			/*
			 *  Hardware 'ticks':  (clocks, interrupt sources...)
			 *
			 *  Here, cpu0instrs is the number of instructions
			 *  executed on cpu0.  (TODO: don't use cpu 0 for this,
			 *  use some kind of "mainbus" instead.)  Hardware
			 *  ticks are not per instruction, but per cycle,
			 *  so we divide by the number of
			 *  instructions_per_cycle for cpu0.
			 *
			 *  TODO:  This doesn't work in a machine with, say,
			 *  a mixture of R3000, R4000, and R10000 CPUs, if
			 *  there ever was such a thing.
			 *
			 *  TODO 2:  A small bug occurs if cpu0instrs isn't
			 *  evenly divisible by instrs_per_cycle. We then
			 *  cause hardware ticks a fraction of a cycle too
			 *  often.
			 */
			i = cpus[0]->cpu_type.instrs_per_cycle;
			switch (i) {
			case 1:	break;
			case 2:	cpu0instrs >>= 1; break;
			case 4:	cpu0instrs >>= 2; break;
			default:
				cpu0instrs /= i;
			}

			for (te=0; te<cpus[0]->n_tick_entries; te++) {
				cpus[0]->ticks_till_next[te] -= cpu0instrs;

				if (cpus[0]->ticks_till_next[te] <= 0) {
					while (cpus[0]->ticks_till_next[te] <= 0)
						cpus[0]->ticks_till_next[te] +=
						    cpus[0]->ticks_reset_value[te];
					cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
				}
			}

			ncycles += cpu0instrs;
		} while (running && (ncycles < ncycles_chunk_end));

		/*  Check for X11 events:  */
		if (emul->use_x11) {
			if (ncycles > ncycles_flushx11 + (1<<16)) {
				x11_check_event();
				ncycles_flushx11 = ncycles;
			}
		}

		/*  If we've done buffered console output,
		    the flush stdout every now and then:  */
		if (ncycles > ncycles_flush + (1<<16)) {
			console_flush();
			ncycles_flush = ncycles;
		}

		if (ncycles > ncycles_show + (1<<22)) {
			cpu_show_cycles(emul, &starttime, ncycles, 0);
			ncycles_show = ncycles;
		}

		if (max_instructions_cached != 0 &&
		    ncycles >= max_instructions_cached)
			running = 0;
	}

	/*
	 *  Two last ticks of every hardware device.  This will allow
	 *  framebuffers to draw the last updates to the screen before
	 *  halting.
	 *  (TODO: do this per cpu?)
	 */
        for (te=0; te<cpus[0]->n_tick_entries; te++) {
		cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
		cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
	}

	debug("All CPUs halted.\n");

	if (emul->show_nr_of_instructions || !quiet_mode)
		cpu_show_cycles(emul, &starttime, ncycles, 1);

	if (emul->show_opcode_statistics)
		cpu_show_full_statistics(emul, cpus);

	fflush(stdout);

	return 0;
}

