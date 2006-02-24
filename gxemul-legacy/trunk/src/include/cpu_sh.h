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
 *  $Id: cpu_sh.h,v 1.16 2006-02-24 00:20:42 debug Exp $
 */

#include "misc.h"


struct cpu_family;


#define	SH_N_IC_ARGS			3
#define	SH_INSTR_ALIGNMENT_SHIFT	2
#define	SH_IC_ENTRIES_SHIFT		10
#define	SH_IC_ENTRIES_PER_PAGE		(1 << SH_IC_ENTRIES_SHIFT)
#define	SH_PC_TO_IC_ENTRY(a)		(((a)>>SH_INSTR_ALIGNMENT_SHIFT) \
					& (SH_IC_ENTRIES_PER_PAGE-1))
#define	SH_ADDR_TO_PAGENR(a)		((a) >> (SH_IC_ENTRIES_SHIFT \
					+ SH_INSTR_ALIGNMENT_SHIFT))

#define	SH_L2N		17
#define	SH_L3N		18

DYNTRANS_MISC_DECLARATIONS(sh,SH,uint64_t)
DYNTRANS_MISC64_DECLARATIONS(sh,SH,uint8_t)

#define	SH_MAX_VPH_TLB_ENTRIES		128


struct sh_cpu {
	int		bits;
	int		compact;

	uint64_t	r[64];


	/*
	 *  Instruction translation cache and Virtual->Physical->Host
	 *  address translation:
	 */
	DYNTRANS_ITC(sh)
	VPH_TLBS(sh,SH)
	VPH32(sh,SH,uint64_t,uint8_t)
	VPH64(sh,SH,uint8_t)
};


/*  cpu_sh.c:  */
void sh_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sh_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sh_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void sh32_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void sh32_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void sh32_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
void sh_init_64bit_dummy_tables(struct cpu *cpu);
int sh_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int sh_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_SH_H  */
