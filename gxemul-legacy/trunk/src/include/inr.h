#ifndef	INR_H
#define	INR_H

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
 *  $Id: inr.h,v 1.2 2007-03-31 15:11:26 debug Exp $
 *
 *  Intermediate Native Representation
 *
 *  Data structures etc. used during native code generation.
 */

#ifndef NATIVE_CODE_GENERATION
#error Huh? inr.h should not be included.
#endif


struct inr_entry {
	int		opcode;
	size_t		arg1;
	size_t		arg2;
	size_t		arg3;
};

/*
 *  The way these defines are named works as follows:
 *
 *  INR_OPCODE_opcodename[_args[..]]
 *
 *  where args usually is something like this:
 *
 *	xCR32	32-bit register offset, struct cpu relative
 *	xCR64	64-bit register offset, struct cpu relative
 *	IS16	immediate signed 16-bit
 *	IS32	immediate signed 32-bit
 *	IU16	immediate unsigned 16-bit
 *	IU32	immediate unsigned 32-bit
 *
 *  Prefixes are:
 *
 *	S	source
 *	D	destination
 */

#define	INR_OPCODE_UNKNOWN			0

/*  Misc.:  */
#define	INR_OPCODE_NOP				1

/*  Arithmetic:  */
#define	INR_OPCODE_XOR_DCR32_SCR32_IS16		2
#define	INR_OPCODE_OR_DCR32_SCR32_IS16		3


/*
 *  Max nr of intermediate opcodes in the inr_entries array. The most common
 *  case is that one "basic block" of the emulated machine code is translated,
 *  so this should be large enough to hold a large basic block plus margin.
 */
#define	INR_MAX_ENTRIES		128

struct inr {
	struct inr_entry	*inr_entries;
	int			nr_inr_entries_used;

	uint64_t		paddr;
};


#endif	/*  INR_H  */