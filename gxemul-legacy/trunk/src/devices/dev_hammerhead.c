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
 *  $Id: dev_hammerhead.c,v 1.3 2006-02-09 20:02:59 debug Exp $
 *  
 *  Hammerhead controller, for the secondary CPU on MacPPC machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*  #define debug fatal  */

void ppc_pc_to_pointers(struct cpu *);
void ppc32_pc_to_pointers(struct cpu *);

#define	DEV_HAMMERHEAD_LENGTH		4

struct hammerhead_data {
	int	dummy;
};


/*
 *  dev_hammerhead_access():
 */
DEVICE_ACCESS(hammerhead)
{
	uint64_t idata = 0, odata=0;
	/*  struct hammerhead_data *d = extra;  */

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_WRITE) {
		int my_id = cpu->cpu_id;
		struct cpu *other_cpu = cpu->machine->cpus[!my_id];

		debug("[ HAMMERHEAD: from cpu%i to cpu%i: new pc = 0x%llx ]\n",
		    my_id, !my_id, (long long)idata);

if (idata <= 0x100)
return 1;

		other_cpu->running = 1;
		other_cpu->pc = idata;
#ifdef ENABLE_PPC
		if (other_cpu->is_32bit)
			ppc32_pc_to_pointers(other_cpu);
		else
			ppc_pc_to_pointers(other_cpu);
#else
		fatal("Hm. PPC not enabled.\n");
		exit(1);
#endif
	} else {
		fatal("[ HAMMERHEAD read: TODO ]\n");
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(hammerhead)
{
	struct hammerhead_data *d = malloc(sizeof(struct hammerhead_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct hammerhead_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_HAMMERHEAD_LENGTH, dev_hammerhead_access, d,
	    DM_DEFAULT, NULL);

	return 1;
}

