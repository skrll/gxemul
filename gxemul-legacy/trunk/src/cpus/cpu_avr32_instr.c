/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_avr32_instr.c,v 1.1 2006-10-25 09:24:06 debug Exp $
 *
 *  AVR32 instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (n_translated_instrs is automatically increased by 1 for each function
 *  call. If no instruction was executed, then it should be decreased. If, say,
 *  4 instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((AVR32_IC_ENTRIES_PER_PAGE-1) << 1);
	cpu->pc += (AVR32_IC_ENTRIES_PER_PAGE << 1);

	/*  Find the new physical page and update the translation pointers:  */
	avr32_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  avr32_instr_to_be_translated():
 *
 *  Translate an instruction word into an avr32_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc;
	uint16_t iword;
	unsigned char *page;
	unsigned char ib[2];
	/* void (*samepage_function)(struct cpu *, struct avr32_instr_call *);*/

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.avr32.cur_ic_page)
	    / sizeof(struct avr32_instr_call);
	addr = cpu->pc & ~((AVR32_IC_ENTRIES_PER_PAGE-1) <<
	    AVR32_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << AVR32_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << AVR32_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.avr32.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	iword = (ib[0] << 8) + ib[1];


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 *
	 *  NOTE: The instruction length is assumed to be 2 bytes (1 slot)
	 *        if nothing else is specified.
	 */
	ic->arg[0] = 1;

#if 0
	switch (main opcode) {

	default:goto bad;
	}
#endif


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}
