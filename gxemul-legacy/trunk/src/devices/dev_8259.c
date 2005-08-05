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
 *  $Id: dev_8259.c,v 1.13 2005-08-05 07:50:37 debug Exp $
 *  
 *  8259 Programmable Interrupt Controller.
 *
 *  See the following URL for more details:
 *	http://www.nondot.org/sabre/os/files/MiscHW/8259pic.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_8259_LENGTH		2

/*  #define DEV_8259_DEBUG  */


/*
 *  dev_8259_access():
 */
int dev_8259_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct pic8259_data *d = (struct pic8259_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i;

	idata = memory_readmax64(cpu, data, len);

#ifdef DEV_8259_DEBUG
	if (writeflag == MEM_READ)
		fatal("[ 8259: read from 0x%x ]\n", (int)relative_addr);
	else
		fatal("[ 8259: write to 0x%x: 0x%x ]\n",
		    (int)relative_addr, (int)idata);
#endif

	switch (relative_addr) {
	case 0x00:
		if (writeflag == MEM_WRITE) {
			if ((idata & 0x10) == 0x10) {
				/*  Bit 3: 0=edge, 1=level  */
				if (idata & 0x08)
					fatal("[ 8259: TODO: Level "
					    "triggered (MCA bus) ]\n");
				if (idata & 0x04)
					fatal("[ 8259: WARNING: Bit 2 set ]\n");
				/*  Bit 1: 0=cascade, 1=single  */
				/*  Bit 0: 1=4th init byte  */
				if (!(idata & 0x01))
					fatal("[ 8259: WARNING: Bit 0 NOT set!"
					    "!! ]\n");
				d->init_state = 1;
				break;
			}

			/*  TODO: Is it ok to abort init state when there
			    is a non-init command?  */
			if (d->init_state)
				fatal("[ 8259: WARNING: Was in init-state, but"
				    " it was aborted? ]\n");
			d->init_state = 0;

			switch (idata) {
			case 0x0a:
				d->current_command = 0x0a;
				break;
			case 0x0b:
				d->current_command = 0x0b;
				break;
			case 0x0c:
				/*  Put Master in Buffered Mode  */
				d->current_command = 0x0c;
				break;
			case 0x20:	/*  End Of Interrupt  */
				/*
				 *  TODO: in buffered mode, is this an EOI 0?
				 */
				d->irr &= ~d->isr;
				d->isr = 0;
				/*  Recalculate interrupt assertions:  */
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case 0x21:	/*  Specific EOI  */
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
			case 0x26:
			case 0x27:
			case 0x60:
			case 0x61:
			case 0x62:
			case 0x63:
			case 0x64:
			case 0x65:
			case 0x66:
			case 0x67:	/*  Specific EOI  */
				d->irr &= ~(1 << (idata & 7));
				d->isr &= ~(1 << (idata & 7));
				/*  Recalculate interrupt assertions:  */
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case 0x68:	/*  Set Special Mask Mode  */
				/*  TODO  */
				break;
			case 0xc0:
			case 0xc1:
			case 0xc2:
			case 0xc3:
			case 0xc4:
			case 0xc5:
			case 0xc6:
			case 0xc7:	/*  Set IRQ Priority Order  */
				/*  TODO  */
				break;
			default:
				fatal("[ 8259: unimplemented command 0x%02x"
				    " ]\n", idata);
				cpu->running = 0;
			}
		} else {
			switch (d->current_command) {
			case 0x0a:
				odata = d->irr;
				break;
			case 0x0b:
				odata = d->isr;
				break;
			case 0x0c:
				/*  Buffered mode.  */
			default:
				odata = 0x00;
				for (i=0; i<8; i++)
					if ((d->irr >> i) & 1) {
						odata = 0x80 | i;
						break;
					}
				break;
			/*
			 *  TODO: The "default" label should really do
			 *  something like this:
			 *
			 *	fatal("[ 8259: unimplemented command 0x%02x"
			 *	    " while reading ]\n", d->current_command);
			 *	cpu->running = 0;
			 *
			 *  but Linux seems to read from the secondary PIC
			 *  in a manner which works better the way things
			 *  are coded right now.
			 */
			}
		}
		break;
	case 0x01:
		if (d->init_state > 0) {
			if (d->init_state == 1) {
				d->irq_base = idata & 0xf8;
				if (idata & 7)
					fatal("[ 8259: WARNING! Lowest"
					    " bits in Init Cmd 1 are"
					    " non-zero! ]\n");
				d->init_state = 2;
			} else if (d->init_state == 2) {
				/*  Slave attachment. TODO  */
				d->init_state = 3;
			} else if (d->init_state == 3) {
				if (idata & 0x02)
					fatal("[ 8259: WARNING! Bit 1 i"
					    "n Init Cmd 4 is set! ]\n");
				if (!(idata & 0x01))
					fatal("[ 8259: WARNING! Bit 0 "
					    "in Init Cmd 4 is not"
					    " set! ]\n");
				d->init_state = 0;
			}
			break;
		}

		if (writeflag == MEM_WRITE) {
			d->ier = idata;
			/*  Recalculate interrupt assertions:  */
			cpu_interrupt(cpu, d->irq_nr);
		} else {
			odata = d->ier;
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ 8259: unimplemented write to address 0x%x"
			    " data=0x%02x ]\n", (int)relative_addr, (int)idata);
			cpu->running = 0;
		} else {
			fatal("[ 8259: unimplemented read from address 0x%x "
			    "]\n", (int)relative_addr);
			cpu->running = 0;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_8259():
 *
 *  Initialize an 8259 PIC. Important notes:
 *
 *	x)  Most systems use _TWO_ 8259 PICs. These should be registered
 *	    as separate devices.
 *
 *	x)  The irq number specified is the number used to re-calculate
 *	    CPU interrupt assertions.  It is _not_ the irq number at
 *	    which the PIC is connected. (That is left to machine specific
 *	    code in src/machine.c.)
 */
int devinit_8259(struct devinit *devinit)
{
	struct pic8259_data *d = malloc(sizeof(struct pic8259_data));
	char *name2;
	size_t nlen = strlen(devinit->name) + 20;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pic8259_data));
	d->irq_nr = devinit->irq_nr;

	name2 = malloc(nlen);
	snprintf(name2, nlen, "%s", devinit->name);
	if ((devinit->addr & 0xfff) == 0xa0) {
		strlcat(name2, " [secondary]", nlen);
		d->irq_base = 8;
	}

	memory_device_register(devinit->machine->memory, name2,
	    devinit->addr, DEV_8259_LENGTH, dev_8259_access, (void *)d,
	    MEM_DEFAULT, NULL);

	devinit->return_ptr = d;
	return 1;
}

