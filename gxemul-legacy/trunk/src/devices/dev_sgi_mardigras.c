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
 *  $Id: dev_sgi_mardigras.c,v 1.4 2004-07-26 00:24:40 debug Exp $
 *  
 *  "MardiGras" graphics controller on SGI IP30 (Octane).
 *
 *  Most of this is just guesses based on the behaviour of Linux/Octane and
 *  definitions in mgras.h.
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "memory.h"
#include "devices.h"

#include "mgras.h"

#define debug fatal


#define	MARDIGRAS_FAKE_OFFSET	0x500000000ULL	/*  hopefully available  */
#define	MARDIGRAS_DEFAULT_XSIZE	1280
#define	MARDIGRAS_DEFAULT_YSIZE	1024

#define	MICROCODE_START		0x50000
#define	MICROCODE_END		0x55000

static int mardigras_xsize = MARDIGRAS_DEFAULT_XSIZE;
static int mardigras_ysize = MARDIGRAS_DEFAULT_YSIZE;

struct sgi_mardigras_data {
	struct vfb_data		*fb;
	unsigned char		microcode_ram[MICROCODE_END - MICROCODE_START];
	uint64_t		palette_reg_select;
	int			currentx;
	int			currenty;
	int			color;
	int			startx;
	int			starty;
	int			stopx;
	int			stopy;
	uint64_t		draw_mode;
};


/*
 *  mardigras_20400():
 */
void mardigras_20400(struct cpu *cpu, struct sgi_mardigras_data *d,
	uint64_t idata)
{
	int i, x, y, r,g,b, len, addr;
	unsigned char pixels[3 * 8000];

	/*  Get rgb from palette:  */
	r = d->fb->rgb_palette[d->color * 3 + 0];
	g = d->fb->rgb_palette[d->color * 3 + 1];
	b = d->fb->rgb_palette[d->color * 3 + 2];

	if ((idata & 0x00ffffff00000000ULL) == MGRAS_SET_COLOR) {
		int color = (idata >> 12) & 0xff;
		d->color = color;
		return;
	}

	if ((idata & 0x00ffffff00000000ULL) == MGRAS_SET_STARTXY) {
		d->startx = (idata >> 16) & 0xffff;
		d->starty = idata & 0xffff;
		if (d->startx >= mardigras_xsize)
			d->startx = 0;
		if (d->starty >= mardigras_ysize)
			d->starty = 0;
		d->currentx = d->startx;
		d->currenty = d->starty;
		return;
	}

	if ((idata & 0x00ffffff00000000ULL) == MGRAS_SET_STOPXY) {
		d->stopx = (idata >> 16) & 0xffff;
		d->stopy = idata & 0xffff;
		if (d->stopx >= mardigras_xsize)
			d->stopx = 0;
		if (d->stopy >= mardigras_ysize)
			d->stopy = 0;
		return;
	}

	if (idata == MGRAS_DRAW_RECT || idata == MGRAS_DRAW_BITMAP) {
		d->draw_mode = idata;
		return;
	}

	if (idata == MGRAS_SEND_CMD) {
		switch (d->draw_mode) {
		case MGRAS_DRAW_RECT:
			/*  Fill pixels[] with pixels:  */
			len = 0;
			for (x=d->startx; x<=d->stopx; x++) {
				pixels[len + 0] = r;
				pixels[len + 1] = g;
				pixels[len + 2] = b;
				len += 3;
			}
			if (len == 0)
				break;
			for (y=d->starty; y<=d->stopy; y++) {
				addr = (mardigras_xsize * (mardigras_ysize -
				    1 - y) + d->startx) * 3;
				/*  printf("addr = %i\n", addr);  */

				/*  Write a line:  */
				dev_fb_access(cpu, cpu->mem,
				    addr, pixels, len, MEM_WRITE, d->fb);
			}
			break;
		case MGRAS_DRAW_BITMAP:
			break;
		default:
			fatal("[ sgi_mardigras: unknown draw mode ]\n");
		}
		return;
	}

	if ((idata & 0x00ffffff00000000ULL) == MGRAS_SEND_LINE) {
		addr = (mardigras_xsize * (mardigras_ysize - 1 - d->currenty)
		    + d->currentx) * 3;
/*
		printf("addr=%08x curx,y=%4i,%4i startx,y=%4i,%4i stopx,y=%4i,%4i\n",
		    addr, d->currentx, d->currenty,
		    d->startx, d->starty, d->stopx, d->stopy);
*/
		len = 8*3;

		if (addr > mardigras_xsize * mardigras_ysize * 3 || addr < 0)
			return;

		/*  Read a line:  */
		dev_fb_access(cpu, cpu->mem,
		    addr, pixels, len, MEM_READ, d->fb);

		i = 0;
		while (i < 8) {
			if ((idata >> (24 + (7-i))) & 1) {
				pixels[i*3 + 0] = r;
				pixels[i*3 + 1] = g;
				pixels[i*3 + 2] = b;
			}
			i ++;

			d->currentx ++;
			if (d->currentx > d->stopx) {
				d->currentx = d->startx;
				d->currenty ++;
				if (d->currenty > d->stopy)
					d->currenty = d->starty;
			}
		}

		/*  Write a line:  */
		dev_fb_access(cpu, cpu->mem,
		    addr, pixels, len, MEM_WRITE, d->fb);

		return;
	}

	debug("mardigras_20400(): 0x%016llx\n", (long long)idata);
}


/*
 *  dev_sgi_mardigras_access():
 */
int dev_sgi_mardigras_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct sgi_mardigras_data *d = extra;
	int i;

	idata = memory_readmax64(cpu, data, len);

	/*  Accessing the microcode_ram works like ordinary ram:  */
	if (relative_addr >= MICROCODE_START &&
	    relative_addr <  MICROCODE_END) {
		relative_addr -= MICROCODE_START;
		if (writeflag == MEM_WRITE)
			memcpy(d->microcode_ram + relative_addr, data, len);
		else
			memcpy(data, d->microcode_ram + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	case 0x20200:
		break;
	case 0x20400:
		if (writeflag == MEM_WRITE)
			mardigras_20400(cpu, d, idata);
		else
			debug("[ sgi_mardigras: read from 0x20400? ]\n");
		break;
	case 0x58040:
		/*  HQ4 microcode stuff  */
		break;
	case 0x70c30:
		/*  Palette register select?  */
		if (writeflag == MEM_WRITE)
			d->palette_reg_select = idata;
		else
			odata = d->palette_reg_select;
		break;
	case 0x70d18:
		/*  Palette register read/write?  */
		i = 3 * ((d->palette_reg_select >> 8) & 0xff);
		if (writeflag == MEM_WRITE) {
			d->fb->rgb_palette[i + 0] = (idata >> 24) & 0xff;
			d->fb->rgb_palette[i + 1] = (idata >> 16) & 0xff;
			d->fb->rgb_palette[i + 2] = (idata >>  8) & 0xff;
		} else {
			odata = (d->fb->rgb_palette[i+0] << 24) +
				(d->fb->rgb_palette[i+1] << 16) +
				(d->fb->rgb_palette[i+2] << 8);
		}
		break;
	case 0x71208:
		odata = 8;
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ sgi_mardigras: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ sgi_mardigras: write to  0x%08lx: 0x%016llx ]\n", (long)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mardigras_init():
 */
void dev_sgi_mardigras_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct sgi_mardigras_data *d;

	d = malloc(sizeof(struct sgi_mardigras_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mardigras_data));

	d->fb = dev_fb_init(cpu, mem, MARDIGRAS_FAKE_OFFSET,
	    VFB_GENERIC, mardigras_xsize, mardigras_ysize,
	    mardigras_xsize, mardigras_ysize, 24, "SGI MardiGras");
	if (d->fb == NULL) {
		fprintf(stderr, "dev_sgi_mardigras_init(): out of memory\n");
		exit(1);
	}

	memory_device_register(mem, "sgi_mardigras", baseaddr,
	    DEV_SGI_MARDIGRAS_LENGTH, dev_sgi_mardigras_access, d);
}

