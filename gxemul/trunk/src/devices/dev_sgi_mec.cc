/*
 *  Copyright (C) 2003-2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: SGI IP32 "mec" ethernet
 *
 *  Used by the SGI O2 emulation mode.
 *  Based on how NetBSD and OpenBSD use the hardware.
 *
 *  http://www.openbsd.org/cgi-bin/cvsweb/src/sys/arch/sgi/dev/if_mec.c
 *
 *  TODO:
 *
 *	x)  tx and rx interrupts/ring/slot stuff
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "thirdparty/if_mecreg.h"


#define	MEC_TICK_SHIFT		14

#define	MAX_TX_PACKET_LEN	1700
#define	N_RX_ADDRESSES		16

struct sgi_mec_data {
	uint64_t	reg[DEV_SGI_MEC_LENGTH / sizeof(uint64_t)];

	struct interrupt irq;
	unsigned char	macaddr[6];

	unsigned char	cur_tx_packet[MAX_TX_PACKET_LEN];
	int		cur_tx_packet_len;

	unsigned char	*cur_rx_packet;
	int		cur_rx_packet_len;

	uint64_t	rx_addr[N_RX_ADDRESSES];
	int		cur_rx_addr_index_write;
	int		cur_rx_addr_index;
};


/*
 *  mec_reset():
 */
static void mec_reset(struct sgi_mec_data *d)
{
	if (d->cur_rx_packet != NULL)
		free(d->cur_rx_packet);

	memset(d->reg, 0, sizeof(d->reg));
}


/*
 *  mec_control_write():
 */
static void mec_control_write(struct cpu *cpu, struct sgi_mec_data *d,
	uint64_t x)
{
	if (x & MEC_MAC_CORE_RESET) {
		debug("[ sgi_mec: CORE RESET ]\n");
		mec_reset(d);
	}
}


/*
 *  mec_try_rx():
 */
static int mec_try_rx(struct cpu *cpu, struct sgi_mec_data *d)
{
	uint64_t base;
	unsigned char data[8];
	int i, res, retval = 0;

	base = d->rx_addr[d->cur_rx_addr_index];
	if (base & 0xfff)
		fatal("[ mec_try_rx(): WARNING! lowest bits of base are "
		    "non-zero (0x%3x). TODO ]\n", (int)(base & 0xfff));
	base &= 0xfffff000ULL;
	if (base == 0)
		goto skip;

	/*  printf("rx base = 0x%016llx\n", (long long)base);  */

	/*  Read an rx descriptor from memory:  */
	res = cpu->memory_rw(cpu, cpu->mem, base,
	    &data[0], sizeof(data), MEM_READ, PHYSICAL);
	if (!res)
		return 0;

#if 0
	printf("{ mec: rxdesc %i: ", d->cur_rx_addr_index);
	for (i=0; i<sizeof(data); i++) {
		if ((i & 3) == 0)
			printf(" ");
		printf("%02x", data[i]);
	}
	printf(" }\n");
#endif

	/*  Is this descriptor already in use?  */
	if (data[0] & 0x80) {
		/*  printf("INTERRUPT for base = 0x%x\n", (int)base);  */
		goto skip_and_advance;
	}

	if (d->cur_rx_packet == NULL &&
	    net_ethernet_rx_avail(cpu->machine->emul->net, d))
		net_ethernet_rx(cpu->machine->emul->net, d,
		    &d->cur_rx_packet, &d->cur_rx_packet_len);

	if (d->cur_rx_packet == NULL)
		goto skip;

	/*  Copy the packet data:  */
	/*  printf("RX: ");  */
	for (i=0; i<d->cur_rx_packet_len; i++) {
		res = cpu->memory_rw(cpu, cpu->mem, base + 32 + i + 2,
		    d->cur_rx_packet + i, 1, MEM_WRITE, PHYSICAL);
		/*  printf(" %02x", d->cur_rx_packet[i]);  */
	}
	/*  printf("\n");  */

#if 0
	printf("RX: %i bytes, index %i, base = 0x%x\n",
	    d->cur_rx_packet_len, d->cur_rx_addr_index, (int)base);
#endif

	/*  4 bytes of CRC at the end. Hm. TODO  */
	d->cur_rx_packet_len += 4;

	memset(data, 0, sizeof(data));
	data[6] = (d->cur_rx_packet_len >> 8) & 255;
	data[7] = d->cur_rx_packet_len & 255;
	/*  TODO: lots of bits :-)  */
	data[4] = 0x04;		/*  match MAC  */
	data[0] = 0x80;		/*  0x80 = received.  */
	res = cpu->memory_rw(cpu, cpu->mem, base,
	    &data[0], sizeof(data), MEM_WRITE, PHYSICAL);

	/*  Free the packet from memory:  */
	free(d->cur_rx_packet);
	d->cur_rx_packet = NULL;

	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= MEC_INT_RX_THRESHOLD;
skip_and_advance:
	d->cur_rx_addr_index ++;
	d->cur_rx_addr_index %= N_RX_ADDRESSES;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] &= ~MEC_INT_RX_MCL_FIFO_ALIAS;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |=
	    (d->cur_rx_addr_index & 0x1f) << 8;
	retval = 1;

skip:
	return retval;
}


/*
 *  mec_try_tx():
 */
static int mec_try_tx(struct cpu *cpu, struct sgi_mec_data *d)
{
	uint64_t base, addr, dma_base;
	int tx_ring_ptr, ringread, ringwrite, res, i, j;
	unsigned char data[32];
	int len, start_offset, dma_ptr_nr, dma_len;

	base = d->reg[MEC_TX_RING_BASE / sizeof(uint64_t)];
	tx_ring_ptr = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];

	if (base == 0)
		return 0;

	/*  printf("base = 0x%016llx\n", base);  */

	ringread = tx_ring_ptr & MEC_TX_RING_READ_PTR;
	ringwrite = tx_ring_ptr & MEC_TX_RING_WRITE_PTR;
	ringread >>= 16;
	/*  All done? Then abort.  */
	if (ringread == ringwrite)
		return 0;

	tx_ring_ptr &= MEC_TX_RING_READ_PTR;
	tx_ring_ptr >>= 16;

	/*  Each tx descriptor (+ buffer) is 128 bytes:  */
	addr = base + tx_ring_ptr*128;
	res = cpu->memory_rw(cpu, cpu->mem, addr,
	    &data[0], sizeof(data), MEM_READ, PHYSICAL);
	if (!res)
		return 0;

	/*  Is this packet transmitted already?  */
	if (data[0] & 0x80) {
		fatal("[ mec_try_tx: tx_ring_ptr = %i, already"
		    " transmitted? ]\n", tx_ring_ptr);
		goto advance_tx;
	}

	len = data[6] * 256 + data[7];
	start_offset = data[5] & 0x7f;

	/*  Is this packet empty? Then don't transmit.  */
	if (len == 0)
		return 0;

	/*  Hm. Is len one too little?  TODO  */
	len ++;

#if 0
	printf("{ mec: txdesc %i: ", tx_ring_ptr);
	for (i=0; i<sizeof(data); i++) {
		if ((i & 3) == 0)
			printf(" ");
		printf("%02x", data[i]);
	}
	printf(" }\n");
#endif
	dma_ptr_nr = 0;

	j = 0;
	d->cur_tx_packet_len = len;

	for (i=start_offset; i<start_offset+len; i++) {
		unsigned char ch;

		if ((i & 0x7f) == 0x00)
			break;

		res = cpu->memory_rw(cpu, cpu->mem, addr + i,
		    &ch, sizeof(ch), MEM_READ, PHYSICAL);
		/*  printf(" %02x", ch);  */

		d->cur_tx_packet[j++] = ch;
		if (j >= MAX_TX_PACKET_LEN) {
			fatal("[ mec_try_tx: packet too large? ]\n");
			break;
		}
	}
	/*  printf("\n");  */

	if (j < len) {
		/*  Continue with DMA:  */
		for (;;) {
			dma_ptr_nr ++;
			if (dma_ptr_nr >= 4)
				break;
			if (!(data[4] & (0x01 << dma_ptr_nr)))
				break;
			dma_base = (data[dma_ptr_nr * 8 + 4] << 24)
			         + (data[dma_ptr_nr * 8 + 5] << 16)
			         + (data[dma_ptr_nr * 8 + 6] <<  8)
			         + (data[dma_ptr_nr * 8 + 7]);
			dma_base &= 0xfffffff8ULL;
			dma_len = (data[dma_ptr_nr * 8 + 2] << 8)
			        + (data[dma_ptr_nr * 8 + 3]) + 1;

			/*  printf("dma_base = %08x, dma_len = %i\n",
			    (int)dma_base, dma_len);  */

			while (dma_len > 0) {
				unsigned char ch;
				res = cpu->memory_rw(cpu, cpu->mem, dma_base,
				    &ch, sizeof(ch), MEM_READ, PHYSICAL);
				/*  printf(" %02x", ch);  */

				d->cur_tx_packet[j++] = ch;
				if (j >= MAX_TX_PACKET_LEN) {
					fatal("[ mec_try_tx: packet too large?"
					    " ]\n");
					break;
				}
				dma_base ++;
				dma_len --;
			}
		}
	}

	if (j < len)
		fatal("[ mec_try_tx: not enough data? ]\n");

	net_ethernet_tx(cpu->machine->emul->net, d,
	    d->cur_tx_packet, d->cur_tx_packet_len);

	/*  see openbsd's if_mec.c for details  */
	if (data[4] & 0x01) {
		d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |=
		    MEC_INT_TX_PACKET_SENT;
	}
	memset(data, 0, 6);	/*  last 2 bytes are len  */
	data[0] = 0x80;
	data[5] = 0x80;

	res = cpu->memory_rw(cpu, cpu->mem, addr,
	    &data[0], sizeof(data), MEM_WRITE, PHYSICAL);

	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= MEC_INT_TX_EMPTY;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= MEC_INT_TX_PACKET_SENT;

advance_tx:
	/*  Advance the ring Read ptr.  */
	tx_ring_ptr = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];
	ringread = tx_ring_ptr & MEC_TX_RING_READ_PTR;
	ringwrite = tx_ring_ptr & MEC_TX_RING_WRITE_PTR;

	ringread = (ringread >> 16) + 1;
	ringread &= 63;
	ringread <<= 16;

	d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)] =
	    (ringwrite & MEC_TX_RING_WRITE_PTR) |
	    (ringread & MEC_TX_RING_READ_PTR);

	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] &=
	    ~MEC_INT_TX_RING_BUFFER_ALIAS;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |=
	    (d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)] &
	    MEC_INT_TX_RING_BUFFER_ALIAS);

	return 1;
}


DEVICE_TICK(sgi_mec)
{
	struct sgi_mec_data *d = (struct sgi_mec_data *) extra;
	int n = 0;

	while (mec_try_tx(cpu, d))
		;

	while (mec_try_rx(cpu, d) && n < 16)
		n++;

	/*  Interrupts:  (TODO: only when enabled)  */
	if (d->reg[MEC_INT_STATUS / sizeof(uint64_t)] & MEC_INT_STATUS_MASK) {
#if 0
		printf("[%02x]", (int)(d->reg[MEC_INT_STATUS /
		    sizeof(uint64_t)] & MEC_INT_STATUS_MASK));
		fflush(stdout);
#endif
		INTERRUPT_ASSERT(d->irq);
	} else
		INTERRUPT_DEASSERT(d->irq);
}


DEVICE_ACCESS(sgi_mec)
{
	struct sgi_mec_data *d = (struct sgi_mec_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat most registers as read/write, by default.  */
	if (writeflag == MEM_WRITE) {
		switch (relative_addr) {
		case MEC_INT_STATUS:	/*  0x08  */
			/*  Clear bits on write:  (This is just a guess)  */
			d->reg[regnr] = (d->reg[regnr] & ~0xff)
			    | ((d->reg[regnr] & ~idata) & 0xff);
			break;
		case MEC_TX_RING_PTR:	/*  0x30  */
			idata &= MEC_TX_RING_WRITE_PTR;
			d->reg[regnr] = (d->reg[regnr] &
			    ~MEC_TX_RING_WRITE_PTR) | idata;
			/*  TODO  */
			break;
		default:
			d->reg[regnr] = idata;
		}
	} else
		odata = d->reg[regnr];

	switch (relative_addr) {
	case MEC_MAC_CONTROL:	/*  0x00  */
		if (writeflag)
			mec_control_write(cpu, d, idata);
		else {
			/*  Fake "revision 1":  */
			odata &= ~MEC_MAC_REVISION;
			odata |= 1 << MEC_MAC_REVISION_SHIFT;
		}
		break;
	case MEC_INT_STATUS:	/*  0x08  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_INT_STATUS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_DMA_CONTROL:	/*  0x10  */
		if (writeflag) {
			debug("[ sgi_mec: write to MEC_DMA_CONTROL: "
			    "0x%016llx ]\n", (long long)idata);
			if (!(idata & MEC_DMA_TX_INT_ENABLE)) {
				/*  This should apparently stop the
				    TX Empty interrupt.  */
				d->reg[MEC_INT_STATUS / sizeof(uint64_t)] &=
				    ~MEC_INT_TX_EMPTY;
			}
		}
		break;
	case MEC_TX_ALIAS:	/*  0x20  */
		if (writeflag) {
			debug("[ sgi_mec: write to MEC_TX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
		} else {
			debug("[ sgi_mec: read from MEC_TX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
			odata = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];
		}
		break;
	case MEC_RX_ALIAS:	/*  0x28  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_RX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_TX_RING_PTR:	/*  0x30  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_TX_RING_PTR: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_PHY_DATA:	/*  0x64  */
		if (writeflag)
			fatal("[ sgi_mec: write to MEC_PHY_DATA: "
			    "0x%016llx ]\n", (long long)idata);
		else
			odata = 0;	/*  ?  */
		break;
	case MEC_PHY_ADDRESS:	/*  0x6c  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_PHY_ADDRESS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_PHY_READ_INITIATE:	/*  0x70  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_PHY_READ_INITIATE: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case 0x74:
		if (writeflag)
			debug("[ sgi_mec: write to 0x74: 0x%016llx ]\n",
			    (long long)idata);
		else
			debug("[ sgi_mec: read from 0x74 ]\n");
		break;
	case MEC_STATION:	/*  0xa0  */
		if (writeflag)
			debug("[ sgi_mec: setting the MAC address to "
			    "%02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	case MEC_STATION_ALT:	/*  0xa8  */
		if (writeflag)
			debug("[ sgi_mec: setting the ALTERNATIVE MAC address"
			    " to %02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	case MEC_MULTICAST:	/*  0xb0  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_MULTICAST: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_TX_RING_BASE:	/*  0xb8  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_TX_RING_BASE: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_MCL_RX_FIFO:	/*  0x100  */
		if (writeflag) {
			debug("[ sgi_mec: write to MEC_MCL_RX_FIFO: 0x"
			    "%016llx ]\n", (long long)idata);
			d->rx_addr[d->cur_rx_addr_index_write] = idata;
			d->cur_rx_addr_index_write ++;
			d->cur_rx_addr_index_write %= N_RX_ADDRESSES;
		}
		break;
	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_mec: unimplemented write to address"
			    " 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_mec: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_sgi_mec_tick(cpu, extra);

	return 1;
}


/*
 *  dev_sgi_mec_init():
 */
void dev_sgi_mec_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, char *irq_path, unsigned char *macaddr)
{
	char *name2;
	size_t nlen = 55;
	struct sgi_mec_data *d;

	CHECK_ALLOCATION(d = (struct sgi_mec_data *) malloc(sizeof(struct sgi_mec_data)));
	memset(d, 0, sizeof(struct sgi_mec_data));

	INTERRUPT_CONNECT(irq_path, d->irq);
	memcpy(d->macaddr, macaddr, 6);
	mec_reset(d);

	CHECK_ALLOCATION(name2 = (char *) malloc(nlen));
	snprintf(name2, nlen, "mec [%02x:%02x:%02x:%02x:%02x:%02x]",
	    d->macaddr[0], d->macaddr[1], d->macaddr[2],
	    d->macaddr[3], d->macaddr[4], d->macaddr[5]);

	memory_device_register(mem, name2, baseaddr,
	    DEV_SGI_MEC_LENGTH, dev_sgi_mec_access, (void *)d,
	    DM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_sgi_mec_tick, d,
	    MEC_TICK_SHIFT);

	net_add_nic(machine->emul->net, d, macaddr);
}


