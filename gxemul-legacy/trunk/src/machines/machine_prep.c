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
 *  $Id: machine_prep.c,v 1.5 2006-01-16 00:51:15 debug Exp $
 *
 *  Machines conforming to the PowerPC Reference Platform specs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"



MACHINE_SETUP(prep)
{
	struct pci_data *pci_data;
	char *model_name = "";

	switch (machine->machine_subtype) {

	case MACHINE_PREP_IBM6050:
		machine->machine_name =
		    "PowerPC Reference Platform, IBM 6050/6070";
		machine->stable = 1;
		model_name = "IBM PPS Model 6050/6070 (E)";

		if (machine->emulated_hz == 0)
			machine->emulated_hz = 20000000;

		machine->md_int.prep_data = device_add(machine, "prep");
		machine->isa_pic_data.native_irq = 1;	/*  Semi-bogus  */
		machine->md_interrupt = isa32_interrupt;

		pci_data = dev_eagle_init(machine, machine->memory,
		    32 /*  isa irq base */, 0 /*  pci irq: TODO */);

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1,
		    0x80000000, 0xc0000000, 32, 48);

		bus_pci_add(machine, pci_data, machine->memory,
		    0, 13, 0, "dec21143");

		if (machine->use_x11) {
			bus_pci_add(machine, pci_data, machine->memory,
			    0, 14, 0, "s3_virge");
		}
		break;

	case MACHINE_PREP_MVME2400:
		machine->machine_name = "PowerPC Reference Platform, MVME2400";

		/*  TODO: _EXACT_ model name for mvme2400?  */
		model_name = "MOT MVME2400";

		machine->md_int.prep_data = device_add(machine, "prep");
		machine->isa_pic_data.native_irq = 1;	/*  Semi-bogus  */
		machine->md_interrupt = isa32_interrupt;

		pci_data = dev_eagle_init(machine, machine->memory,
		    32 /*  isa irq base */, 0 /*  pci irq: TODO */);

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1,
		    0x80000000, 0xc0000000, 32, 48);

		break;

	default:fatal("Unimplemented PReP machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}

	if (!machine->prom_emulation)
		return;


	/*  Linux on PReP has 0xdeadc0de at address 0? (See
	    http://joshua.raleigh.nc.us/docs/linux-2.4.10_html/
	    113568.html)  */
	store_32bit_word(cpu, 0, 0xdeadc0de);

	/*
	 *  r4 should point to first free byte after the loaded kernel.
	 *  r6 should point to bootinfo.
	 */
	cpu->cd.ppc.gpr[4] = 6 * 1048576;
	cpu->cd.ppc.gpr[6] = machine->physical_ram_in_mb*1048576-0x8000;

	/*
	 *  (See NetBSD's prep/include/bootinfo.h for details.)
	 *
	 *  32-bit "next" offset;
	 *  32-bit "type";
	 *  type-specific data...
	 */

	/*  type: clock  */
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 0, 12);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 4, 2);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 8, machine->emulated_hz);

	/*  type: console  */
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+12, 20);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+16, 1);
	store_buf(cpu, cpu->cd.ppc.gpr[6]+20, machine->use_x11? "vga":"com", 4);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+24, 0x3f8);/*  addr  */
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+28, 9600);/*  speed  */

	/*  type: residual  */
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+32, 0);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+36, 0);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+40,/*  addr of data  */
	    cpu->cd.ppc.gpr[6] + 0x100);

	/*  Residual data:  (TODO)  */
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+0x100, 0x200);
	store_string(cpu, cpu->cd.ppc.gpr[6]+0x100+0x8, model_name);
	store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+0x100+0x1f8,
	    machine->physical_ram_in_mb * 1048576);  /*  memsize  */
}


MACHINE_DEFAULT_CPU(prep)
{
	switch (machine->machine_subtype) {

	case MACHINE_PREP_IBM6050:
		machine->cpu_name = strdup("PPC604");
		break;

	case MACHINE_PREP_MVME2400:
		machine->cpu_name = strdup("PPC750");
		break;

	default:fatal("Unimplemented PReP machine subtype %i\n",
		    machine->machine_subtype);
		exit(1);
	}
}


MACHINE_DEFAULT_RAM(prep)
{
	machine->physical_ram_in_mb = 64;
}


MACHINE_REGISTER(prep)
{
	MR_DEFAULT(prep, "PowerPC Reference Platform", ARCH_PPC,
	    MACHINE_PREP, 1, 2);

	me->aliases[0] = "prep";

	me->subtype[0] = machine_entry_subtype_new(
	    "IBM 6050/6070", MACHINE_PREP_IBM6050, 1);
	me->subtype[0]->aliases[0] = "ibm6050";
	me->subtype[0]->aliases[1] = "ibm6070";

	me->subtype[1] = machine_entry_subtype_new(
	    "MVME2400", MACHINE_PREP_MVME2400, 1);
	me->subtype[1]->aliases[0] = "mvme2400";

	me->set_default_ram = machine_default_ram_prep;

	machine_entry_add(me, ARCH_PPC);
}
