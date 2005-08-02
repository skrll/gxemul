/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_ns16550.c,v 1.36 2005-08-02 18:58:09 debug Exp $
 *  
 *  NS16550 serial controller.
 *
 *
 *  TODO:
 *
 *	x)  Implement the FIFO.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "comreg.h"


/*  #define debug fatal  */

#define	TICK_SHIFT		14


struct ns_data {
	int		addrmult;
	int		in_use;
	int		irqnr;
	int		console_handle;
	int		enable_fifo;

	unsigned char	reg[8];
	unsigned char	fcr;		/*  FIFO control register  */

	int		dlab;		/*  Divisor Latch Access bit  */
	int		divisor;

	int		databits;
	char		parity;
	const char	*stopbits;
};


/*
 *  dev_ns16550_tick():
 *
 *  This function is called at regular intervals. An interrupt is caused to the
 *  CPU if there is a character available for reading, or if the transmitter
 *  slot is empty (i.e. the ns16550 is ready to transmit).
 */
void dev_ns16550_tick(struct cpu *cpu, void *extra)
{
	struct ns_data *d = extra;

	d->reg[com_iir] &= ~IIR_RXRDY;
	if (d->in_use && console_charavail(d->console_handle))
		d->reg[com_iir] |= IIR_RXRDY;

	/*
	 *  If interrupts are enabled, and interrupts are pending, then
	 *  cause a CPU interrupt.
 	 */
	if (((d->reg[com_ier] & IER_ETXRDY) && (d->reg[com_iir] & IIR_TXRDY)) ||
	    ((d->reg[com_ier] & IER_ERXRDY) && (d->reg[com_iir] & IIR_RXRDY))) {
		d->reg[com_iir] &= ~IIR_NOPEND;
		if (d->reg[com_mcr] & MCR_IENABLE)
			cpu_interrupt(cpu, d->irqnr);
	} else {
		d->reg[com_iir] |= IIR_NOPEND;
		cpu_interrupt_ack(cpu, d->irqnr);
	}
}


/*
 *  dev_ns16550_access():
 */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata=0;
	int i;
	struct ns_data *d = extra;

	idata = memory_readmax64(cpu, data, len);
	if (len != 1)
		fatal("[ ns16550: len=%i, idata=0x%16llx! ]\n",
		    len, (long long)idata);

	/*
	 *  Always ready to transmit:
	 */
	d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
	d->reg[com_msr] |= MSR_DCD | MSR_DSR | MSR_CTS;

	d->reg[com_iir] &= ~0xf0;
	if (d->enable_fifo)
		d->reg[com_iir] |= ((d->fcr << 5) & 0xc0);

	d->reg[com_lsr] &= ~LSR_RXRDY;
	if (d->in_use && console_charavail(d->console_handle))
		d->reg[com_lsr] |= LSR_RXRDY;

	relative_addr /= d->addrmult;

	if (relative_addr >= 8) {
		fatal("[ ns16550: outside register space? relative_addr="
		    "0x%llx. bad addrmult? bad device length? ]\n",
		    (long long)relative_addr);
		return 0;
	}

	switch (relative_addr) {

	case com_data:	/*  data AND low byte of the divisor  */
		/*  Read/write of the Divisor value:  */
		if (d->dlab) {
			/*  Write or read the low byte of the divisor:  */
			if (writeflag == MEM_WRITE)
				d->divisor = d->divisor & 0xff00 | idata;
			else
				odata = d->divisor & 0xff;
			break;
		}

		/*  Read/write of data:  */
		if (writeflag == MEM_WRITE) {
			if (d->reg[com_mcr] & MCR_LOOPBACK)
				console_makeavail(d->console_handle, idata);
			else
				console_putchar(d->console_handle, idata);
			d->reg[com_iir] |= IIR_TXRDY;
		} else {
			if (d->in_use)
				odata = console_readchar(d->console_handle);
			else
				odata = 0;
		}
		dev_ns16550_tick(cpu, d);
		break;

	case com_ier:	/*  interrupt enable AND high byte of the divisor  */
		/*  Read/write of the Divisor value:  */
		if (d->dlab) {
			if (writeflag == MEM_WRITE) {
				/*  Set the high byte of the divisor:  */
				d->divisor = (d->divisor & 0xff) | (idata << 8);
				debug("[ ns16550: speed set to %i bps ]\n",
				    (int)(115200 / d->divisor));
			} else
				odata = d->divisor >> 8;
			break;
		}

		/*  IER:  */
		if (writeflag == MEM_WRITE) {
			/*  This is to supress Linux' behaviour  */
			if (idata != 0)
				debug("[ ns16550: write to ier: 0x%02x ]\n",
				    (int)idata);

			/*  Needed for NetBSD 2.0.x, but not 1.6.2?  */
			if (!(d->reg[com_ier] & IER_ETXRDY)
			    && (idata & IER_ETXRDY))
				d->reg[com_iir] |= IIR_TXRDY;

			d->reg[com_ier] = idata;
			dev_ns16550_tick(cpu, d);
		} else
			odata = d->reg[com_ier];
		break;

	case com_iir:	/*  interrupt identification (r), fifo control (w)  */
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550: write to fifo control ]\n");
			d->fcr = idata;
		} else {
			odata = d->reg[com_iir];
			debug("[ ns16550: read from iir: 0x%02x ]\n",
			    (int)odata);
			dev_ns16550_tick(cpu, d);
		}
		break;

	case com_lsr:
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550: write to lsr ]\n");
			d->reg[com_lsr] = idata;
		} else
			odata = d->reg[com_lsr];
		break;

	case com_msr:
		if (writeflag == MEM_WRITE) {
			debug("[ ns16550: write to msr ]\n");
			d->reg[com_msr] = idata;
		} else
			odata = d->reg[com_msr];
		break;

	case com_lctl:
		if (writeflag == MEM_WRITE) {
			d->reg[com_lctl] = idata;
			switch (idata & 0x7) {
			case 0:	d->databits = 5; d->stopbits = "1"; break;
			case 1:	d->databits = 6; d->stopbits = "1"; break;
			case 2:	d->databits = 7; d->stopbits = "1"; break;
			case 3:	d->databits = 8; d->stopbits = "1"; break;
			case 4:	d->databits = 5; d->stopbits = "1.5"; break;
			case 5:	d->databits = 6; d->stopbits = "2"; break;
			case 6:	d->databits = 7; d->stopbits = "2"; break;
			case 7:	d->databits = 8; d->stopbits = "2"; break;
			}
			switch ((idata & 0x38) / 0x8) {
			case 0:	d->parity = 'N'; break;		/*  none  */
			case 1:	d->parity = 'O'; break;		/*  odd  */
			case 2:	d->parity = '?'; break;
			case 3:	d->parity = 'E'; break;		/*  even  */
			case 4:	d->parity = '?'; break;
			case 5:	d->parity = 'Z'; break;		/*  zero  */
			case 6:	d->parity = '?'; break;
			case 7:	d->parity = 'o'; break;		/*  one  */
			}

			d->dlab = idata & 0x80? 1 : 0;

			debug("[ ns16550: write to lctl: 0x%02x (%s%s"
			    "setting mode %i%c%s) ]\n",
			    (int)idata,
			    d->dlab? "Divisor Latch access, " : "",
			    idata&0x40? "sending BREAK, " : "",
			    d->databits, d->parity, d->stopbits);
		} else {
			odata = d->reg[com_lctl];
			debug("[ ns16550: read from lctl: 0x%02x ]\n",
			    (int)odata);
		}
		break;

	case com_mcr:
		if (writeflag == MEM_WRITE) {
			d->reg[com_mcr] = idata;
			debug("[ ns16550: write to mcr: 0x%02x ]\n",
			    (int)idata);
		} else {
			odata = d->reg[com_mcr];
			debug("[ ns16550: read from mcr: 0x%02x ]\n",
			    (int)odata);
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			debug("[ ns16550: read from reg %i ]\n",
			    (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ ns16550: write to reg %i:",
			    (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ns16550_init():
 */
int dev_ns16550_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int addrmult, int in_use,
	char *name)
{
	struct ns_data *d;
	size_t nlen;
	char *name2;

	d = malloc(sizeof(struct ns_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ns_data));
	d->irqnr        = irq_nr;
	d->addrmult     = addrmult;
	d->in_use       = in_use;
	d->enable_fifo  = 1;
	d->dlab         = 0;
	d->divisor      = 115200 / 9600;
	d->databits     = 8;
	d->parity       = 'N';
	d->stopbits     = "1";
	d->console_handle = console_start_slave(machine, name);

	nlen = strlen(name) + 20;
	name2 = malloc(nlen);
	if (name2 == NULL) {
		fprintf(stderr, "out of memory in dev_ns16550_init()\n");
		exit(1);
	}
	if (name != NULL && name[0])
		snprintf(name2, nlen, "ns16550 [%s]", name);
	else
		snprintf(name2, nlen, "ns16550");

	memory_device_register(mem, name2, baseaddr, DEV_NS16550_LENGTH
	    * addrmult, dev_ns16550_access, d, MEM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_ns16550_tick, d, TICK_SHIFT);

	return d->console_handle;
}

