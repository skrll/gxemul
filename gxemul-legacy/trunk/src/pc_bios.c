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
 *  $Id: pc_bios.c,v 1.2 2005-04-16 04:12:33 debug Exp $
 *
 *  Generic PC BIOS emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "cpu_x86.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


extern int quiet_mode;


/*
 *  output_char():
 */
static void output_char(struct cpu *cpu, int x, int y, int ch, int color)
{
	uint64_t addr = (y * 80 + x) * 2;
	unsigned char w[2];
	uint64_t ctrlregs = 0x1000003c0ULL;

	w[0] = ch; w[1] = color;
	cpu->cd.x86.cursegment = 0xb800;
	cpu->memory_rw(cpu, cpu->mem, addr, &w[0], sizeof(w), MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  set_cursor_pos():
 */
static void set_cursor_pos(struct cpu *cpu, int x, int y)
{
	int addr = y * 80 + x;
	unsigned char byte;
	uint64_t ctrlregs = 0x1000003c0ULL;

	byte = 0x0e;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = (addr >> 8) & 255;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = 0x0f;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = addr & 255;
	cpu->memory_rw(cpu, cpu->mem, ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
}


/*
 *  pc_bios_int10():
 *
 *  Video functions.
 */
static void pc_bios_int10(struct cpu *cpu)
{
	int x,y;
	int ah = (cpu->cd.x86.eax >> 8) & 0xff;
	int al = cpu->cd.x86.eax & 0xff;

	switch (ah) {
	case 0x00:	/*  Switch video mode.  */
		switch (al) {
		case 0x03:	/*  80x25 color textmode  */
			/*  Simply clear the screen and home the cursor
			    for now. TODO: More advanced stuff.  */
			set_cursor_pos(cpu, 0, 0);
			for (y=0; y<25; y++)
				for (x=0; x<80; x++)
					output_char(cpu, x,y, ' ', 0x07);
			break;
		default:
			fatal("pc_bios_int10(): unimplemented video mode "
			    "0x%02x\n", al);
			cpu->running = 0;
			cpu->dead = 1;
		}
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x10 function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int13():
 *
 *  Disk-related functions.
 */
static void pc_bios_int13(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.eax >> 8) & 0xff;
	int al = cpu->cd.x86.eax & 0xff;
	int dl = cpu->cd.x86.edx & 0xff;

	switch (ah) {
	case 0x00:	/*  Reset disk, dl = drive  */
		/*  Do nothing. :-)  */
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x1a function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_int1a():
 *
 *  Time of Day stuff.
 */
static void pc_bios_int1a(struct cpu *cpu)
{
	int ah = (cpu->cd.x86.eax >> 8) & 0xff;
	int al = cpu->cd.x86.eax & 0xff;

	switch (ah) {
	case 0x00:
		/*  Return tick count? TODO  */
		cpu->cd.x86.ecx = 0;
		cpu->cd.x86.edx = 0;
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x1a function"
		    " 0x%02x.\n", ah);
		cpu->running = 0;
		cpu->dead = 1;
	}
}


/*
 *  pc_bios_emul():
 */
int pc_bios_emul(struct cpu *cpu)
{
	uint32_t addr = (cpu->cd.x86.cs << 4) + cpu->pc;
	int int_nr;

	int_nr = addr & 0xfff;

	switch (int_nr) {
	case 0x10:
		pc_bios_int10(cpu);
		break;
	case 0x13:
		pc_bios_int13(cpu);
		break;
	case 0x1a:
		pc_bios_int1a(cpu);
		break;
	default:
		fatal("FATAL: Unimplemented PC BIOS interrupt 0x%02x.\n",
		    int_nr);
		cpu->running = 0;
		cpu->dead = 1;
	}

	/*
	 *  Return from the interrupt:  Pop ip (pc), cs, and flags.
	 */
	cpu->cd.x86.cursegment = cpu->cd.x86.ss;
	cpu->pc = load_16bit_word(cpu, cpu->cd.x86.esp);
	cpu->cd.x86.cs = load_16bit_word(cpu, cpu->cd.x86.esp + 2);
	cpu->cd.x86.eflags = (cpu->cd.x86.eflags & ~0xffff)
	    | load_16bit_word(cpu, cpu->cd.x86.esp + 4);

	cpu->cd.x86.esp = (cpu->cd.x86.esp & ~0xffff)
	    | (cpu->cd.x86.esp + 6) & 0xffff;

	return 1;
}

