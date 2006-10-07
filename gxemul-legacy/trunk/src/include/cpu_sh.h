#ifndef	CPU_SH_H
#define	CPU_SH_H

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
 *  $Id: cpu_sh.h,v 1.26 2006-10-07 00:36:29 debug Exp $
 */

#include "misc.h"


struct cpu_family;

/*  SH CPU types:  */
struct sh_cpu_type_def {
	char		*name;
	int		bits;
};

#define	SH_CPU_TYPE_DEFS	{	\
	{ "SH3", 32 },			\
	{ "SH4", 32 },			\
	{ "SH5", 64 },			\
	{ NULL, 0 } }


/*
 *  TODO: Figure out how to nicely support multiple instruction encodings!
 *  For now, I'm reverting this to SH4. SH5 will have to wait until later.
 */

#define	SH_N_IC_ARGS			2	/*  3 for SH5/SH64  */
#define	SH_INSTR_ALIGNMENT_SHIFT	1	/*  2 for SH5/SH64  */
#define	SH_IC_ENTRIES_SHIFT		11	/*  10 for SH5/SH64  */
#define	SH_IC_ENTRIES_PER_PAGE		(1 << SH_IC_ENTRIES_SHIFT)
#define	SH_PC_TO_IC_ENTRY(a)		(((a)>>SH_INSTR_ALIGNMENT_SHIFT) \
					& (SH_IC_ENTRIES_PER_PAGE-1))
#define	SH_ADDR_TO_PAGENR(a)		((a) >> (SH_IC_ENTRIES_SHIFT \
					+ SH_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(sh,SH,uint32_t)

#define	SH_MAX_VPH_TLB_ENTRIES		128


/*  For SH5:  #define	SH_N_GPRS		64  */
/*  For pre-SH5:  */
#define	SH_N_GPRS		16
#define	SH_N_GPRS_BANKED	8

#define	SH_N_UTLB_ENTRIES	64


struct sh_cpu {
	struct sh_cpu_type_def cpu_type;

	/*  compact = 1 if currently executing 16-bit long opcodes  */
	int		compact;

	/*  General Purpose Registers:  */
	uint32_t	r[SH_N_GPRS];

	/*  Saved Banked registers (during mode switch):  */
	uint32_t	r_bank[SH_N_GPRS_BANKED];

	uint32_t	mach;		/*  Multiply-Accumulate High  */
	uint32_t	macl;		/*  Multiply-Accumulate Low  */
	uint32_t	pr;		/*  Procedure Register  */
	uint32_t	fpscr;		/*  Floating-point Status/Control  */
	uint32_t	fpul;		/*  Floating-point Communication Reg  */
	uint32_t	sr;		/*  Status Register  */
	uint32_t	ssr;		/*  Saved Status Register  */
	uint32_t	spc;		/*  Saved PC  */
	uint32_t	gbr;		/*  Global Base Register  */
	uint32_t	vbr;		/*  Vector Base Register  */
	uint32_t	sgr;		/*  Saved General Register  */
	uint32_t	dbr;		/*  Debug Base Register  */

	/*  Cache control:  */
	uint32_t	ccr;		/*  Cache Control Register  */

	/*  MMU/TLB registers:  */
	uint32_t	pteh;		/*  Page Table Entry High  */
	uint32_t	ptel;		/*  Page Table Entry Low  */
	uint32_t	ptea;		/*  Page Table Entry A  */
	uint32_t	ttb;		/*  Translation Table Base  */
	uint32_t	tea;		/*  TLB Exception Address Register  */
	uint32_t	mmucr;		/*  MMU Control Register  */
	uint32_t	utlb_hi[SH_N_UTLB_ENTRIES];
	uint32_t	utlb_lo[SH_N_UTLB_ENTRIES];

	/*  Exception handling:  */
	uint32_t	tra;		/*  TRAPA Exception Register  */
	uint32_t	expevt;		/*  Exception Event Register  */
	uint32_t	intevt;		/*  Interrupt Event Register  */


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(sh)
	VPH_TLBS(sh,SH)
	VPH32(sh,SH,uint64_t,uint8_t)
};


/*  Status register bits:  */
#define	SH_SR_T			0x00000001	/*  True/false  */
#define	SH_SR_S			0x00000002	/*  Saturation  */
#define	SH_SR_IMASK		0x000000f0	/*  Interrupt mask  */
#define	SH_SR_IMASK_SHIFT		4
#define	SH_SR_Q			0x00000100	/*  State for Divide Step  */
#define	SH_SR_M			0x00000200	/*  State for Divide Step  */
#define	SH_SR_FD		0x00008000	/*  FPU Disable  */
#define	SH_SR_BL		0x10000000	/*  Exception/Interrupt Block */
#define	SH_SR_RB		0x20000000	/*  Register Bank 0/1  */
#define	SH_SR_MD		0x40000000	/*  Privileged Mode  */


/*  cpu_sh.c:  */
int sh_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib);
int sh_run_instr(struct cpu *cpu);
void sh_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sh_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sh_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int sh32_run_instr(struct cpu *cpu);
void sh32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sh32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sh32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void sh_init_64bit_dummy_tables(struct cpu *cpu);
int sh_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int sh_cpu_family_init(struct cpu_family *);

void sh_update_sr(struct cpu *cpu, uint32_t new_sr);
void sh_exception(struct cpu *cpu, int expevt, uint32_t vaddr);

/*  memory_sh.c:  */
int sh_translate_v2p(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);


#endif	/*  CPU_SH_H  */
