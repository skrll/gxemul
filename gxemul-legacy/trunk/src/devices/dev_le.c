/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_le.c,v 1.13 2004-07-05 21:08:41 debug Exp $
 *  
 *  LANCE ethernet, as used in DECstations.
 *
 *  This is based on "PMAD-AA TURBOchannel Ethernet Module Functional
 *  Specification". I've tried to keep symbol names in this file to what
 *  the specs use.
 *
 *  This is what the memory layout looks like on a DECstation 5000/200:
 *
 *	0x000000 - 0x0fffff	Ethernet SRAM buffer  (should be 128KB)
 *	0x100000 - 0x17ffff	LANCE registers
 *	0x1c0000 - 0x1fffff	Ethernet Diagnostic ROM and Station
 *				Address ROM
 *
 *  TODO:  Make sure that this device works with both NetBSD and Ultrix
 *         and on as many DECstation models as possible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "if_lereg.h"


/*  #define LE_DEBUG  */
/*  #define debug fatal  */


#define	LE_MODE_LOOP		4
#define	LE_MODE_DTX		2
#define	LE_MODE_DRX		1


#define	N_REGISTERS		4
#define	SRAM_SIZE		(128*1024)
#define	ROM_SIZE		32


struct le_data {
	int		irq_nr;

	uint64_t	buf_start;
	uint64_t	buf_end;
	int		len;

	uint8_t		rom[ROM_SIZE];

	int		reg_select;
	uint16_t	reg[N_REGISTERS];

	unsigned char	sram[SRAM_SIZE];

	/*  Initialization block:  */
	uint32_t	init_block_addr;

	uint16_t	mode;
	uint64_t	padr;	/*  MAC address  */
	uint64_t	ladrf;
	uint32_t	rdra;	/*  receive descriptor ring address  */
	int		rlen;	/*  nr of rx descriptors  */
	uint32_t	tdra;	/*  transmit descriptor ring address  */
	int		tlen;	/*  nr ot tx descriptors  */

	/*  Current rx and tx descriptor indices:  */
	int		rxp;
	int		txp;

	unsigned char	*tx_packet;
	int		tx_packet_len;

	unsigned char	*rx_packet;
	int		rx_packet_len;
	int		rx_packet_offset;
	int		rx_middle_bit;
};


/*
 *  le_read_16bit():
 *
 *  Read a 16-bit word from the SRAM.
 */
uint64_t le_read_16bit(struct le_data *d, int addr)
{
	/*  TODO: This is for little endian only  */
	int x = d->sram[addr & (SRAM_SIZE-1)] +
	       (d->sram[(addr+1) & (SRAM_SIZE-1)] << 8);
	return x;
}


/*
 *  le_write_16bit():
 *
 *  Write a 16-bit word to the SRAM.
 */
void le_write_16bit(struct le_data *d, int addr, uint16_t x)
{
	/*  TODO: This is for little endian only  */
	d->sram[addr & (SRAM_SIZE-1)] = x & 0xff;
	d->sram[(addr+1) & (SRAM_SIZE-1)] = (x >> 8) & 0xff;
}


/*
 *  le_chip_init():
 *
 *  Initialize data structures by reading an 'initialization block' from the
 *  SRAM.
 */
void le_chip_init(struct le_data *d)
{
	d->init_block_addr = (d->reg[1] & 0xffff) + ((d->reg[2] & 0xff) << 16);
	if (d->init_block_addr & 1)
		fatal("[ le: WARNING! initialization block address not word aligned? ]\n");

	debug("[ le: d->init_block_addr = 0x%06x ]\n", d->init_block_addr);

	d->mode = le_read_16bit(d, d->init_block_addr + 0);
	d->padr = le_read_16bit(d, d->init_block_addr + 2);
	d->padr += (le_read_16bit(d, d->init_block_addr + 4) << 16);
	d->padr += (le_read_16bit(d, d->init_block_addr + 6) << 32);
	d->ladrf = le_read_16bit(d, d->init_block_addr + 8);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 10) << 16);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 12) << 32);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 14) << 48);
	d->rdra = le_read_16bit(d, d->init_block_addr + 16);
	d->rdra += ((le_read_16bit(d, d->init_block_addr + 18) & 0xff) << 16);
	d->rlen = 1 << ((le_read_16bit(d, d->init_block_addr + 18) >> 13) & 7);
	d->tdra = le_read_16bit(d, d->init_block_addr + 20);
	d->tdra += ((le_read_16bit(d, d->init_block_addr + 22) & 0xff) << 16);
	d->tlen = 1 << ((le_read_16bit(d, d->init_block_addr + 22) >> 13) & 7);

	debug("[ le: DEBUG: mode              %04x ]\n", d->mode);
	debug("[ le: DEBUG: padr  %016llx ]\n", (long long)d->padr);
	debug("[ le: DEBUG: ladrf %016llx ]\n", (long long)d->ladrf);
	debug("[ le: DEBUG: rdra            %06llx ]\n", d->rdra);
	debug("[ le: DEBUG: rlen               %3i ]\n", d->rlen);
	debug("[ le: DEBUG: tdra            %06llx ]\n", d->tdra);
	debug("[ le: DEBUG: tlen               %3i ]\n", d->tlen);

	/*  Set TXON and RXON, unless they are disabled by 'mode':  */
	if (d->mode & LE_MODE_DTX)
		d->reg[0] &= ~LE_TXON;
	else
		d->reg[0] |= LE_TXON;

	if (d->mode & LE_MODE_DRX)
		d->reg[0] &= ~LE_RXON;
	else
		d->reg[0] |= LE_RXON;

	/*  Go to the start of the descriptor rings:  */
	d->rxp = d->txp = 0;

	/*  Set IDON and reset the INIT bit when we are done.  */
	d->reg[0] |= LE_IDON;
	d->reg[0] &= ~LE_INIT;

	/*  Free any old packets:  */
	if (d->tx_packet != NULL)
		free(d->tx_packet);
	d->tx_packet = NULL;
	d->tx_packet_len = 0;

	if (d->rx_packet != NULL)
		free(d->rx_packet);
	d->rx_packet = NULL;
	d->rx_packet_len = 0;
	d->rx_packet_offset = 0;
	d->rx_middle_bit = 0;
}


/*
 *  le_tx():
 *
 *  Check the transmitter descriptor ring for buffers that are owned by the
 *  Lance chip (that is, buffers that are to be transmitted).
 *
 *  This routine should only be called if TXON is enabled.
 */
void le_tx(struct le_data *d)
{
	int start_txp = d->txp;
	uint16_t tx_descr[4];
	int stp, enp, i, cur_packet_offset;
	uint32_t bufaddr, buflen;

	do {
		/*  Load the 8 descriptor bytes:  */
		tx_descr[0] = le_read_16bit(d, d->tdra + d->txp*8 + 0);
		tx_descr[1] = le_read_16bit(d, d->tdra + d->txp*8 + 2);
		tx_descr[2] = le_read_16bit(d, d->tdra + d->txp*8 + 4);
		tx_descr[3] = le_read_16bit(d, d->tdra + d->txp*8 + 6);

		bufaddr = tx_descr[0] + ((tx_descr[1] & 0xff) << 16);
		stp = tx_descr[1] & LE_STP? 1 : 0;
		enp = tx_descr[1] & LE_ENP? 1 : 0;
		buflen = 4096 - (tx_descr[2] & 0xfff);

		/*
		 *  Check the OWN bit. If it is zero, then this buffer is
		 *  not ready to be transmitted yet.  Also check the '1111'
		 *  mark, and make sure that byte-count is reasonable.
		 */
		if (!(tx_descr[1] & LE_OWN))
			return;
		if ((tx_descr[2] & 0xf000) != 0xf000)
			return;
		if (buflen < 12 || buflen > 1900)  /* TODO: eth frame size  */
			return;

		debug("[ le: tx descr %3i DUMP: 0x%04x 0x%04x 0x%04x 0x%04x => addr=0x%06x, len=%i bytes, STP=%i ENP=%i ]\n",
		    d->txp, tx_descr[0], tx_descr[1], tx_descr[2], tx_descr[3],
		    bufaddr, buflen, stp, enp);

		if (d->tx_packet == NULL && !stp) {
			fatal("[ le: le_tx(): !stp but tx_packet == NULL ]\n");
			return;
		}

		if (d->tx_packet != NULL && stp) {
			fatal("[ le: le_tx(): stp but tx_packet != NULL ]\n");
			free(d->tx_packet);
			d->tx_packet = NULL;
			d->tx_packet_len = 0;
		}

		/*  Where to write to in the tx_packet:  */
		cur_packet_offset = d->tx_packet_len;

		/*  Start of a new packet:  */
		if (stp) {
			d->tx_packet_len = buflen;
			d->tx_packet = malloc(buflen);
			if (d->tx_packet == NULL) {
				fprintf(stderr, "out of memory (1) in le_tx()\n");
				exit(1);
			}
		} else {
			d->tx_packet_len += buflen;
			d->tx_packet = realloc(d->tx_packet, d->tx_packet_len);
			if (d->tx_packet == NULL) {
				fprintf(stderr, "out of memory (2) in le_tx()\n");
				exit(1);
			}
		}

		/*  Copy data from SRAM into the tx packet:  */
		for (i=0; i<buflen; i++) {
			unsigned char ch;
			ch = d->sram[(bufaddr + i) & (SRAM_SIZE-1)];
			d->tx_packet[cur_packet_offset + i] = ch;
		}

		/*
		 *  Is this the last buffer in a packet? Then transmit
		 *  it, cause an interrupt, and free the memory used by
		 *  the packet.
		 */
		if (enp) {
			net_ethernet_tx(d, d->tx_packet, d->tx_packet_len);

			free(d->tx_packet);
			d->tx_packet = NULL;
			d->tx_packet_len = 0;

			d->reg[0] |= LE_TINT;
		}

		/*  Clear the OWN bit:  */
		tx_descr[1] &= ~LE_OWN;

		/*  Write back the descriptor to SRAM:  */
		le_write_16bit(d, d->tdra + d->txp*8 + 2, tx_descr[1]);
		le_write_16bit(d, d->tdra + d->txp*8 + 4, tx_descr[2]);
		le_write_16bit(d, d->tdra + d->txp*8 + 6, tx_descr[3]);

		/*  Go to the next descriptor:  */
		d->txp ++;
		if (d->txp >= d->tlen)
			d->txp = 0;
	} while (d->txp != start_txp);

	/*  We are here if all descriptors were taken care of.  */
	fatal("[ le: le_tx(): all TX descriptors used up? ]\n");
}


/*
 *  le_rx():
 *
 *  This routine should only be called if RXON is enabled.
 */
void le_rx(struct le_data *d)
{
	int i, start_rxp = d->rxp;
	uint16_t rx_descr[4];
	uint32_t bufaddr, buflen;

	do {
		if (d->rx_packet == NULL)
			return;

		/*  Load the 8 descriptor bytes:  */
		rx_descr[0] = le_read_16bit(d, d->rdra + d->rxp*8 + 0);
		rx_descr[1] = le_read_16bit(d, d->rdra + d->rxp*8 + 2);
		rx_descr[2] = le_read_16bit(d, d->rdra + d->rxp*8 + 4);
		rx_descr[3] = le_read_16bit(d, d->rdra + d->rxp*8 + 6);

		bufaddr = rx_descr[0] + ((rx_descr[1] & 0xff) << 16);
		buflen = 4096 - (rx_descr[2] & 0xfff);

		/*
		 *  Check the OWN bit. If it is zero, then this buffer is
		 *  not ready to receive data yet.  Also check the '1111'
		 *  mark, and make sure that byte-count is reasonable.
		 */
		if (!(rx_descr[1] & LE_OWN))
			return;
		if ((rx_descr[2] & 0xf000) != 0xf000)
			return;
		if (buflen < 12 || buflen > 1900)  /* TODO: eth frame size  */
			return;

fatal
("[ le: rx descr %3i DUMP: 0x%04x 0x%04x 0x%04x 0x%04x => addr=0x%06x, len=%i bytes ]\n",
		    d->rxp, rx_descr[0], rx_descr[1], rx_descr[2], rx_descr[3],
		    bufaddr, buflen);

		/*  Copy data from the packet into SRAM:  */
		for (i=0; i<buflen; i++) {
			if (d->rx_packet_offset + i >= d->rx_packet_len)
				break;
			d->sram[(bufaddr + i) & (SRAM_SIZE-1)] =
			    d->rx_packet[d->rx_packet_offset + i];
		}

		/*  Here, i is the number of bytes copied.  */
		d->rx_packet_offset += i;

		/*  Set the ENP bit if this was the end of a packet:  */
		if (d->rx_packet_offset >= d->rx_packet_len) {
			rx_descr[1] |= LE_ENP;
			rx_descr[3] &= ~0xfff;
			rx_descr[3] |= d->rx_packet_len;

			free(d->rx_packet);
			d->rx_packet = NULL;
			d->rx_packet_len = 0;
			d->rx_packet_offset = 0;
			d->rx_middle_bit = 0;

			d->reg[0] |= LE_RINT;
		}

		/*  Set the STP bit if this was the start of a packet:  */
		if (!d->rx_middle_bit) {
			rx_descr[1] |= LE_STP;

			/*  Are we continuing on this packet?  */
			if (d->rx_packet != NULL)
				d->rx_middle_bit = 1;
		}

		/*  Clear the OWN bit:  */
		rx_descr[1] &= ~LE_OWN;

		/*  Write back the descriptor to SRAM:  */
		le_write_16bit(d, d->rdra + d->rxp*8 + 2, rx_descr[1]);
		le_write_16bit(d, d->rdra + d->rxp*8 + 4, rx_descr[2]);
		le_write_16bit(d, d->rdra + d->rxp*8 + 6, rx_descr[3]);

		/*  Go to the next descriptor:  */
		d->rxp ++;
		if (d->rxp >= d->rlen)
			d->rxp = 0;
	} while (d->rxp != start_rxp);

	/*  We are here if all descriptors were taken care of.  */
	fatal("[ le: le_rx(): all RX descriptors used up? ]\n");
}


/*
 *  le_register_fix():
 */
void le_register_fix(struct le_data *d)
{
	/*  Should the chip be initialized with a new Initialization block?  */
	if (d->reg[0] & LE_INIT)
		le_chip_init(d);

	/*  If the transmitter is on, check for outgoing buffers:  */
	if (d->reg[0] & LE_TXON)
		le_tx(d);

	/*
	 *  If the receiver is on:
	 *  If there is a current rx_packet, try to receive it into the
	 *  Lance buffers.  Then try to receive any additional packets.
	 */
	if (d->reg[0] & LE_RXON) {
		do {
			if (d->rx_packet != NULL)
				/*  Try to receive the packet:  */
				le_rx(d);

			if (d->rx_packet != NULL)
				/*  If the packet wasn't fully received, 
				    then abort for now.  */
				break;

			if (d->rx_packet == NULL && net_ethernet_rx_avail(d))
				net_ethernet_rx(d, &d->rx_packet,
				    &d->rx_packet_len);
		} while (d->rx_packet != NULL);
	}

	/*  SERR should be the OR of BABL, CERR, MISS, and MERR:  */
	d->reg[0] &= ~LE_SERR;
	if (d->reg[0] & (LE_BABL | LE_CERR | LE_MISS | LE_MERR))
		d->reg[0] |= LE_SERR;

	/*  INTR should be the OR of BABL, MISS, MERR, RINT, TINT, IDON:  */
	d->reg[0] &= ~LE_INTR;
	if (d->reg[0] & (LE_BABL | LE_MISS | LE_MERR | LE_RINT |
	    LE_TINT | LE_IDON))
		d->reg[0] |= LE_INTR;

	/*  The MERR bit clears some bits:  */
	if (d->reg[0] & LE_MERR)
		d->reg[0] &= ~(LE_RXON | LE_TXON);

	/*  The STOP bit clears a lot of stuff:  */
	if (d->reg[0] & LE_STOP)
		d->reg[0] &= ~(LE_SERR | LE_BABL | LE_CERR | LE_MISS | LE_MERR
		    | LE_RINT | LE_TINT | LE_IDON | LE_INTR | LE_INEA
		    | LE_RXON | LE_TXON | LE_TDMD);
}


/*
 *  dev_le_tick():
 */
void dev_le_tick(struct cpu *cpu, void *extra)
{
	struct le_data *d = (struct le_data *) extra;

	if (d->reg[0] & LE_INTR && d->reg[0] & LE_INEA) {
		debug("[ le: interrupt ]\n");
		cpu_interrupt(cpu, d->irq_nr);
	} else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  le_register_write():
 *
 *  This function is called when the value 'x' is written to register 'r'.
 */
void le_register_write(struct le_data *d, int r, uint32_t x)
{
	switch (r) {
	case 0:	/*  CSR0:  */
		/*  Some bits are write-one-to-clear:  */
		if (x & LE_BABL)
			d->reg[r] &= ~LE_BABL;
		if (x & LE_CERR)
			d->reg[r] &= ~LE_CERR;
		if (x & LE_MISS)
			d->reg[r] &= ~LE_MISS;
		if (x & LE_MERR)
			d->reg[r] &= ~LE_MERR;
		if (x & LE_RINT)
			d->reg[r] &= ~LE_RINT;
		if (x & LE_TINT)
			d->reg[r] &= ~LE_TINT;
		if (x & LE_IDON)
			d->reg[r] &= ~LE_IDON;

		/*  Some bits are write-only settable, not clearable:  */
		if (x & LE_TDMD)
			d->reg[r] |= LE_TDMD;
		if (x & LE_STRT) {
			d->reg[r] |= LE_STRT;
			d->reg[r] &= ~LE_STOP;
		}
		if (x & LE_INIT) {
			if (!(d->reg[r] & LE_STOP))
				fatal("[ le: attempt to INIT before STOPped! ]\n");
			d->reg[r] |= LE_INIT;
			d->reg[r] &= ~LE_STOP;
		}
		if (x & LE_STOP) {
			d->reg[r] |= LE_STOP;
			/*  STOP takes precedence over STRT and INIT:  */
			d->reg[r] &= ~(LE_STRT | LE_INIT);
		}

		/*  Some bits get through, both settable and clearable:  */
		d->reg[r] &= ~LE_INEA;
		d->reg[r] |= (x & LE_INEA);
		break;

	default:
		/*  CSR1, CSR2, and CSR3:  */
		d->reg[r] = x;
	}
}


/*
 *  dev_le_access():
 */
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, retval = 1;
	struct le_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

#ifdef LE_DEBUG
	if (writeflag == MEM_WRITE)
		fatal("[ le: write to addr 0x%06x: 0x%08x ]\n",
		    relative_addr, idata);
#endif

	/*  Automatic update of some registers:  */
	le_register_fix(d);

	/*  Read/write of the SRAM:  */
	if (relative_addr < SRAM_SIZE && relative_addr + len <= SRAM_SIZE) {
		if (writeflag == MEM_READ) {
			memcpy(data, d->sram + relative_addr, len);
			debug("[ le: read from SRAM offset 0x%05x:",
			    relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			retval = 9;	/*  9 cycles  */
		} else {
			memcpy(d->sram + relative_addr, data, len);
			debug("[ le: write to SRAM offset 0x%05x:",
			    relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			retval = 6;	/*  6 cycles  */
		}
		return 1;
	}


	/*  Read from station's ROM (ethernet address):  */
	if (relative_addr >= 0x1c0000 && relative_addr <= 0x1c0017) {
		i = (relative_addr & 0xff) / 4;
		i = d->rom[i & (ROM_SIZE-1)];

		if (writeflag == MEM_READ) {
			odata = (i << 24) + (i << 16) + (i << 8) + i;
		} else {
			fatal("[ le: WRITE to ethernet addr (%08lx):",
			    (long)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
		}

		retval = 13;	/*  13 cycles  */
		goto do_return;
	}


	switch (relative_addr) {

	/*  Register read/write:  */
	case 0x100000:
		if (writeflag==MEM_READ) {
			odata = d->reg[d->reg_select];
			debug("[ le: read from register 0x%02x: 0x%02x ]\n",
			    d->reg_select, (int)odata);
			/*
			 *  A read from csr1..3 should return "undefined"
			 *  result if the stop bit is set.  However, Ultrix
			 *  seems to do just that, so let's _not_ print
			 *  a warning here.
			 */
		} else {
			debug("[ le: write to register 0x%02x: 0x%02x ]\n",
			    d->reg_select, (int)idata);
			/*
			 *  A write to from csr1..3 when the stop bit is
			 *  set should be ignored. However, Ultrix writes
			 *  even if the stop bit is set, so let's _not_
			 *  print a warning about it.
			 */
			le_register_write(d, d->reg_select, idata);
		}
		break;

	/*  Register select:  */
	case 0x100004:
		if (writeflag==MEM_READ) {
			odata = d->reg_select;
			debug("[ le: read from register select: 0x%02x ]\n",
			    (int)odata);
		} else {
			debug("[ le: write to register select: 0x%02x ]\n",
			    (int)idata);
			d->reg_select = idata & (N_REGISTERS - 1);
			if (idata >= N_REGISTERS)
				fatal("[ le: WARNING! register select %i (max is %i) ]\n",
				    idata, N_REGISTERS - 1);
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			fatal("[ le: read from UNIMPLEMENTED addr 0x%06x ]\n",
			    relative_addr);
		} else {
			fatal("[ le: write to UNIMPLEMENTED addr 0x%06x: 0x%08x ]\n",
			    relative_addr, (int)idata);
		}
	}


do_return:

	if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
#ifdef LE_DEBUG
		fatal("[ le: read from addr 0x%06x: 0x%08x ]\n",
		    relative_addr, odata);
#endif
	}

	le_register_fix(d);
	dev_le_tick(cpu, extra);

	return retval;
}


/*
 *  dev_le_init():
 */
void dev_le_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	uint64_t buf_start, uint64_t buf_end, int irq_nr, int len)
{
	struct le_data *d = malloc(sizeof(struct le_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct le_data));
	d->irq_nr    = irq_nr;

	/*  TODO:  Are these actually used yet?  */
	d->len       = len;
	d->buf_start = buf_start;
	d->buf_end   = buf_end;

	/*  Initial register contents:  */
	d->reg[0] = LE_STOP;

	d->tx_packet = NULL;
	d->rx_packet = NULL;

	/*  ROM (including the MAC address):  */
	d->rom[0] = 0x11;
	d->rom[1] = 0x22;
	d->rom[2] = 0x33;
	d->rom[3] = 0x44;
	d->rom[4] = 0x55;
	d->rom[5] = 0x66;

	/*  Low order bit of a physical MAC address should be clear:  */
	d->rom[5] &= ~1;

	/*  Copies of the MAC address and a test pattern:  */
	d->rom[10] = d->rom[21] = d->rom[5];
	d->rom[11] = d->rom[20] = d->rom[4];
	d->rom[12] = d->rom[19] = d->rom[3];
	d->rom[7] =  d->rom[8]  = d->rom[23] =
		     d->rom[13] = d->rom[18] = d->rom[2];
	d->rom[6] =  d->rom[9]  = d->rom[22] =
		     d->rom[14] = d->rom[17] = d->rom[1];
	d->rom[15] = d->rom[16] = d->rom[0];
	d->rom[24] = d->rom[28] = 0xff;
	d->rom[25] = d->rom[29] = 0x00;
	d->rom[26] = d->rom[30] = 0x55;
	d->rom[27] = d->rom[31] = 0xaa;

	memory_device_register(mem, "le", baseaddr, len, dev_le_access,
	    (void *)d);

	cpu_add_tickfunction(cpu, dev_le_tick, d, 12);
}

