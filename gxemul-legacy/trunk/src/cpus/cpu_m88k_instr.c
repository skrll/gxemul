/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_m88k_instr.c,v 1.7 2007-05-01 15:36:57 debug Exp $
 *
 *  M88K instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


#define SYNCH_PC                {                                       \
                int low_pc = ((size_t)ic - (size_t)cpu->cd.m88k.cur_ic_page) \
                    / sizeof(struct m88k_instr_call);                   \
                cpu->pc &= ~((M88K_IC_ENTRIES_PER_PAGE-1)               \
                    << M88K_INSTR_ALIGNMENT_SHIFT);                     \
                cpu->pc += (low_pc << M88K_INSTR_ALIGNMENT_SHIFT);      \
        }


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  br_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new instr_call
 */
X(br_samepage)
{
	cpu->cd.m88k.next_ic = (struct m88k_instr_call *) ic->arg[0];
}


/*
 *  br:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset from start of page
 */
X(br)
{
	cpu->pc = (uint32_t)((cpu->pc & 0xfffff000) + (int32_t)ic->arg[0]);

	/*  Find the new physical page and update the translation pointers:  */
	quick_pc_to_pointers(cpu);
}


/*
 *  or_imm:     d = s1 | imm
 *  or_r0_imm:  d = imm		(optimized case when s1 = r0)
 *
 *  arg[0] = pointer to register d
 *  arg[1] = pointer to register s1
 *  arg[2] = imm
 */
X(or_imm)
{
	reg(ic->arg[0]) = reg(ic->arg[1]) | ic->arg[2];
}
X(or_r0_imm)
{
	reg(ic->arg[0]) = ic->arg[2];
}


/*
 *  st:  Store a 32-bit word in memory.
 *
 *  arg[0] = pointer to register d (containing the value to store)
 *  arg[1] = pointer to register s1 (base register)
 *  arg[2] = offset
 *
 *  TODO: Build-time generated shorter versions, without the
 *        conditional endianness check etc.
 */
X(st)
{
	uint32_t addr = reg(ic->arg[1]) + ic->arg[2];
	uint32_t *p = (uint32_t *) cpu->cd.m88k.host_store[addr >> 12];
	uint32_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 2] = data;
		reg(ic->arg[1]) = addr;
	} else {
		SYNCH_PC;
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  Exception.  */
			return;
		}
	}
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((M88K_IC_ENTRIES_PER_PAGE-1) <<
	    M88K_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (M88K_IC_ENTRIES_PER_PAGE << M88K_INSTR_ALIGNMENT_SHIFT);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;

	/*
	 *  Find the new physpage and update translation pointers.
	 *
	 *  Note: This may cause an exception, if e.g. the new page is
	 *  not accessible.
	 */
	quick_pc_to_pointers(cpu);

	/*  Simple jump to the next page (if we are lucky):  */
	if (cpu->delay_slot == NOT_DELAYED)
		return;

	/*
	 *  If we were in a delay slot, and we got an exception while doing
	 *  quick_pc_to_pointers, then return. The function which called
	 *  end_of_page should handle this case.
	 */
	if (cpu->delay_slot == EXCEPTION_IN_DELAY_SLOT)
		return;

	/*
	 *  Tricky situation; the delay slot is on the next virtual page.
	 *  Calling to_be_translated will translate one instruction manually,
	 *  execute it, and then discard it.
	 */
	/*  fatal("[ end_of_page: delay slot across page boundary! ]\n");  */

	instr(to_be_translated)(cpu, cpu->cd.m88k.next_ic);

	/*  The instruction in the delay slot has now executed.  */
	/*  fatal("[ end_of_page: back from executing the delay slot, %i ]\n",
	    cpu->delay_slot);  */

	/*  Find the physpage etc of the instruction in the delay slot
	    (or, if there was an exception, the exception handler):  */
	quick_pc_to_pointers(cpu);
}


X(end_of_page2)
{
	/*  Synchronize PC on the _second_ instruction on the next page:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.m88k.cur_ic_page)
	    / sizeof(struct m88k_instr_call);
	cpu->pc &= ~((M88K_IC_ENTRIES_PER_PAGE-1)
	    << M88K_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << M88K_INSTR_ALIGNMENT_SHIFT);

	/*  This doesn't count as an executed instruction.  */
	cpu->n_translated_instrs --;

	quick_pc_to_pointers(cpu);

	if (cpu->delay_slot == NOT_DELAYED)
		return;

	fatal("end_of_page2: fatal error, we're in a delay slot\n");
	exit(1);
}


/*****************************************************************************/


/*
 *  m88k_instr_to_be_translated():
 *
 *  Translate an instruction word into a m88k_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint32_t addr, low_pc, iword;
	unsigned char *page;
	unsigned char ib[4];
	uint32_t op26, op10, op11, d, s1, s2, w5, imm16;
	int32_t d16, d26, simm16;
	int offset, shift;
	int in_crosspage_delayslot = 0;
	void (*samepage_function)(struct cpu *, struct m88k_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.m88k.cur_ic_page)
	    / sizeof(struct m88k_instr_call);

	/*  Special case for branch with delayslot on the next page:  */
	if (cpu->delay_slot == TO_BE_DELAYED && low_pc == 0) {
		/*  fatal("[ delay-slot translation across page "
		    "boundary ]\n");  */
		in_crosspage_delayslot = 1;
	}

	addr = cpu->pc & ~((M88K_IC_ENTRIES_PER_PAGE-1)
	    << M88K_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << M88K_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (MODE_int_t)addr;
	addr &= ~((1 << M88K_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.m88k.host_load[(uint32_t)addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xffc), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	iword = *((uint32_t *)&ib[0]);
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = LE32_TO_HOST(iword);
	else
		iword = BE32_TO_HOST(iword);


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
 	 *
	 *  NOTE: _NEVER_ allow writes to the zero register; all instructions
	 *  that use the zero register as their destination should be treated
	 *  as NOPs, except those that access memory (they should use the
	 *  scratch register instead).
	 */

	op26   = (iword >> 26) & 0x3f;
	op11   = (iword >> 11) & 0x1f;
	op10   = (iword >> 10) & 0x3f;
	d      = (iword >> 21) & 0x1f;
	s1     = (iword >> 16) & 0x1f;
	s2     =  iword        & 0x1f;
	imm16  =  iword        & 0xffff;
	simm16 = (int16_t) (iword & 0xffff);
	w5     = (iword >>  5) & 0x1f;
	d16    = ((int16_t) (iword & 0xffff)) * 4;
	d26    = ((int32_t)((iword & 0x03ffffff) << 6)) >> 4;

	switch (op26) {

	case 0x09:
		ic->arg[0] = (size_t) &cpu->cd.m88k.r[d];
		ic->arg[1] = (size_t) &cpu->cd.m88k.r[s1];
		ic->arg[2] = imm16;
		switch (op26) {
		case 0x09: ic->f = instr(st); break;
		}
		break;

	case 0x16:	/*  or   imm  */
	case 0x17:	/*  or.u imm  */
		shift = 0;
		switch (op26) {
		case 0x16: ic->f = instr(or_imm); break;
		case 0x17: ic->f = instr(or_imm); shift = 16; break;
		}

		ic->arg[0] = (size_t) &cpu->cd.m88k.r[d];
		ic->arg[1] = (size_t) &cpu->cd.m88k.r[s1];
		ic->arg[2] = imm16 << shift;

		/*  Optimization for  or d,r0,imm  */
		if (s1 == M88K_ZERO_REG && ic->f == instr(or_imm))
			ic->f = instr(or_r0_imm);

		if (d == M88K_ZERO_REG)
			ic->f = instr(nop);
		break;

	case 0x30:	/*  br  */
		ic->f = instr(br);
		samepage_function = instr(br_samepage);

		offset = (addr & 0xffc) + d26;
		if (offset >= 0 && offset <= 0xffc) {
			/*  Same page:  */
			ic->arg[0] = (size_t) ( cpu->cd.m88k.cur_ic_page +
			    (offset >> M88K_INSTR_ALIGNMENT_SHIFT) );
			ic->f = samepage_function;
		} else {
			/*  Different page:  */
			ic->arg[0] = offset;
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}
