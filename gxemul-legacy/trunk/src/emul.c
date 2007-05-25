/*
 *  Copyright (C) 2003-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.c,v 1.287 2007-05-25 21:16:40 debug Exp $
 *
 *  Emulation startup and misc. routines.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "arcbios.h"
#include "cpu.h"
#include "emul.h"
#include "console.h"
#include "debugger.h"
#include "device.h"
#include "diskimage.h"
#include "exec_elf.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu_types.h"
#include "misc.h"
#include "net.h"
#include "settings.h"
#include "sgi_arcbios.h"
#include "timer.h"
#include "x11.h"


extern int extra_argc;
extern char **extra_argv;

extern int verbose;
extern int quiet_mode;
extern int force_debugger_at_exit;
extern int single_step;
extern int old_show_trace_tree;
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;
extern int native_code_translation_enabled;

extern struct emul *debugger_emul;
extern struct diskimage *diskimages[];


static void print_separator(void)
{
	int i = 79;
	while (i-- > 0)
		debug("-");
	debug("\n");
}


/*
 *  add_dump_points():
 *
 *  Take the strings breakpoint_string[] and convert to addresses
 *  (and store them in breakpoint_addr[]).
 *
 *  TODO: This function should be moved elsewhere.
 */
static void add_dump_points(struct machine *m)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<m->n_breakpoints; i++) {
		string_flag = 0;
		dp = strtoull(m->breakpoint_string[i], NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&m->symbol_context,
			    m->breakpoint_string[i], &addr);
			if (!res) {
				fprintf(stderr,
				    "ERROR! Breakpoint '%s' could not be"
					" parsed\n",
				    m->breakpoint_string[i]);
				exit(1);
			} else {
				dp = addr;
				string_flag = 1;
			}
		}

		/*
		 *  TODO:  It would be nice if things like   symbolname+0x1234
		 *  were automatically converted into the correct address.
		 */

		if (m->arch == ARCH_MIPS) {
			if ((dp >> 32) == 0 && ((dp >> 31) & 1))
				dp |= 0xffffffff00000000ULL;
		}

		m->breakpoint_addr[i] = dp;

		debug("breakpoint %i: 0x%llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", m->breakpoint_string[i]);
		debug("\n");
	}
}


/*
 *  fix_console():
 */
static void fix_console(void)
{
	console_deinit_main();
}


/*
 *  emul_new():
 *
 *  Returns a reasonably initialized struct emul.
 */
struct emul *emul_new(char *name, int id)
{
	struct emul *e;
	e = malloc(sizeof(struct emul));
	if (e == NULL) {
		fprintf(stderr, "out of memory in emul_new()\n");
		exit(1);
	}

	memset(e, 0, sizeof(struct emul));

	e->path = malloc(15);
	if (e->path == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	snprintf(e->path, 15, "emul[%i]", id);

	e->settings = settings_new();

	settings_add(e->settings, "n_machines", 0,
	    SETTINGS_TYPE_INT, SETTINGS_FORMAT_DECIMAL,
	    (void *) &e->n_machines);

	/*  TODO: More settings?  */

	/*  Sane default values:  */
	e->n_machines = 0;
	e->next_serial_nr = 1;

	if (name != NULL) {
		e->name = strdup(name);
		if (e->name == NULL) {
			fprintf(stderr, "out of memory in emul_new()\n");
			exit(1);
		}

		settings_add(e->settings, "name", 0,
		    SETTINGS_TYPE_STRING, SETTINGS_FORMAT_STRING,
		    (void *) &e->name);
	}

	return e;
}


/*
 *  emul_destroy():
 *
 *  Destroys a previously created emul object.
 */
void emul_destroy(struct emul *emul)
{
	int i;

	if (emul->name != NULL) {
		settings_remove(emul->settings, "name");
		free(emul->name);
	}

	for (i=0; i<emul->n_machines; i++)
		machine_destroy(emul->machines[i]);

	if (emul->machines != NULL)
		free(emul->machines);

	/*  Remove any remaining level-1 settings:  */
	settings_remove_all(emul->settings);
	settings_destroy(emul->settings);

	free(emul);
}


/*
 *  emul_add_machine():
 *
 *  Calls machine_new(), adds the new machine into the emul struct, and
 *  returns a pointer to the new machine.
 *
 *  This function should be used instead of manually calling machine_new().
 */
struct machine *emul_add_machine(struct emul *e, char *name)
{
	struct machine *m;
	char tmpstr[20];
	int i;

	m = machine_new(name, e, e->n_machines);
	m->serial_nr = (e->next_serial_nr ++);

	i = e->n_machines;

	e->n_machines ++;
	e->machines = realloc(e->machines,
	    sizeof(struct machine *) * e->n_machines);
	if (e->machines == NULL) {
		fprintf(stderr, "emul_add_machine(): out of memory\n");
		exit(1);
	}

	e->machines[i] = m;

	snprintf(tmpstr, sizeof(tmpstr), "machine[%i]", i);
	settings_add(e->settings, tmpstr, 1, SETTINGS_TYPE_SUBSETTINGS, 0,
	    e->machines[i]->settings);

	return m;
}


/*
 *  add_arc_components():
 *
 *  This function adds ARCBIOS memory descriptors for the loaded program,
 *  and ARCBIOS components for SCSI devices.
 */
static void add_arc_components(struct machine *m)
{
	struct cpu *cpu = m->cpus[m->bootstrap_cpu];
	uint64_t start = cpu->pc & 0x1fffffff;
	uint64_t len = 0xc00000 - start;
	struct diskimage *d;
	uint64_t scsicontroller, scsidevice, scsidisk;

	if ((cpu->pc >> 60) != 0xf) {
		start = cpu->pc & 0xffffffffffULL;
		len = 0xc00000 - start;
	}

	len += 1048576 * m->memory_offset_in_mb;

	/*
	 *  NOTE/TODO: magic 12MB end of load program area
	 *
	 *  Hm. This breaks the old FreeBSD/MIPS snapshots...
	 */
#if 0
	arcbios_add_memory_descriptor(cpu,
	    0x60000 + m->memory_offset_in_mb * 1048576,
	    start-0x60000 - m->memory_offset_in_mb * 1048576,
	    ARCBIOS_MEM_FreeMemory);
#endif
	arcbios_add_memory_descriptor(cpu,
	    start, len, ARCBIOS_MEM_LoadedProgram);

	scsicontroller = arcbios_get_scsicontroller(m);
	if (scsicontroller == 0)
		return;

	/*  TODO: The device 'name' should defined be somewhere else.  */

	d = m->first_diskimage;
	while (d != NULL) {
		if (d->type == DISKIMAGE_SCSI) {
			int a, b, flags = COMPONENT_FLAG_Input;
			char component_string[100];
			char *name = "DEC     RZ58     (C) DEC2000";

			/*  Read-write, or read-only?  */
			if (d->writable)
				flags |= COMPONENT_FLAG_Output;
			else
				flags |= COMPONENT_FLAG_ReadOnly;

			a = COMPONENT_TYPE_DiskController;
			b = COMPONENT_TYPE_DiskPeripheral;

			if (d->is_a_cdrom) {
				flags |= COMPONENT_FLAG_Removable;
				a = COMPONENT_TYPE_CDROMController;
				b = COMPONENT_TYPE_FloppyDiskPeripheral;
				name = "NEC     CD-ROM CDR-210P 1.0 ";
			}

			scsidevice = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_ControllerClass,
			    a, flags, 1, 2, d->id, 0xffffffff,
			    name, scsicontroller, NULL, 0);

			scsidisk = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_PeripheralClass,
			    b, flags, 1, 2, 0, 0xffffffff, NULL,
			    scsidevice, NULL, 0);

			/*
			 *  Add device string to component address mappings:
			 *  "scsi(0)disk(0)rdisk(0)partition(0)"
			 */

			if (d->is_a_cdrom) {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)fdisk(0)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidisk);
			} else {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)rdisk(0)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidisk);
			}
		}

		d = d->next;
	}
}


/*
 *  emul_machine_setup():
 *
 *	o)  Initialize the hardware (RAM, devices, CPUs, ...) which
 *	    will be emulated in this machine.
 *
 *	o)  Load ROM code and/or other programs into emulated memory.
 *
 *	o)  Special hacks needed after programs have been loaded.
 */
void emul_machine_setup(struct machine *m, int n_load, char **load_names,
	int n_devices, char **device_names)
{
	struct cpu *cpu;
	int i, iadd = DEBUG_INDENTATION;
	uint64_t memory_amount, entrypoint = 0, gp = 0, toc = 0;
	int byte_order;

	debug("machine \"%s\":\n", m->name);
	debug_indentation(iadd);

	/*  For userland-only, this decides which ARCH/cpu_name to use:  */
	if (m->machine_type == MACHINE_USERLAND && m->userland_emul != NULL) {
		useremul_name_to_useremul(NULL, m->userland_emul,
		    &m->arch, &m->machine_name, &m->cpu_name);
		if (m->arch == ARCH_NOARCH) {
			printf("Unsupported userland emulation mode.\n");
			exit(1);
		}
	}

	if (m->machine_type == MACHINE_NONE) {
		fatal("No machine type specified?\n");
		exit(1);
	}

	m->cpu_family = cpu_family_ptr_by_number(m->arch);

	if (m->arch == ARCH_ALPHA)
		m->arch_pagesize = 8192;

	machine_memsize_fix(m);

	/*
	 *  Create the system's memory:
	 *
	 *  (Don't print the amount for userland-only emulation; the
	 *  size doesn't matter.)
	 */
	if (m->machine_type != MACHINE_USERLAND)
		debug("memory: %i MB", m->physical_ram_in_mb);
	memory_amount = (uint64_t)m->physical_ram_in_mb * 1048576;
	if (m->memory_offset_in_mb > 0) {
		/*
		 *  A special hack is used for some SGI models,
		 *  where memory is offset by 128MB to leave room for
		 *  EISA space and other things.
		 */
		debug(" (offset by %iMB)", m->memory_offset_in_mb);
		memory_amount += 1048576 * m->memory_offset_in_mb;
	}
	m->memory = memory_new(memory_amount, m->arch);
	if (m->machine_type != MACHINE_USERLAND)
		debug("\n");

	/*  Create CPUs:  */
	if (m->cpu_name == NULL)
		machine_default_cputype(m);
	if (m->ncpus == 0) {
		/*  TODO: This should be moved elsewhere...  */
		if (m->machine_type == MACHINE_BEBOX)
			m->ncpus = 2;
		else if (m->machine_type == MACHINE_ARC &&
		    m->machine_subtype == MACHINE_ARC_NEC_R96)
			m->ncpus = 2;
		else if (m->machine_type == MACHINE_ARC &&
		    m->machine_subtype == MACHINE_ARC_NEC_R98)
			m->ncpus = 4;
		else
			m->ncpus = 1;
	}
	m->cpus = malloc(sizeof(struct cpu *) * m->ncpus);
	if (m->cpus == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(m->cpus, 0, sizeof(struct cpu *) * m->ncpus);

	debug("cpu0");
	if (m->ncpus > 1)
		debug(" .. cpu%i", m->ncpus - 1);
	debug(": ");
	for (i=0; i<m->ncpus; i++) {
		m->cpus[i] = cpu_new(m->memory, m, i, m->cpu_name);
		if (m->cpus[i] == NULL) {
			fprintf(stderr, "Unable to create CPU object. "
			    "Aborting.");
			exit(1);
		}
	}
	debug("\n");

#if 0
	/*  Special case: The Playstation Portable has an additional CPU:  */
	if (m->machine_type == MACHINE_PSP) {
		debug("cpu%i: ", m->ncpus);
		m->cpus[m->ncpus] = cpu_new(m->memory, m,
		    0  /*  use 0 here to show info with debug()  */,
		    "Allegrex" /*  TODO  */);
		debug("\n");
		m->ncpus ++;
	}
#endif

	if (m->use_random_bootstrap_cpu)
		m->bootstrap_cpu = random() % m->ncpus;
	else
		m->bootstrap_cpu = 0;

	cpu = m->cpus[m->bootstrap_cpu];

	/*  Only allow native code generation to be used for those
	    emulated architectures that really support it:  */
	if (native_code_translation_enabled) {
		switch (m->arch) {
		case ARCH_M88K:
			break;
		default:fprintf(stderr, "Sorry, native code generation (-b)"
			    " cannot be used to emulate this specific\n"
			    "architecture yet. Aborting.\n");
			exit(1);
		}

		debug("***\n***  Enabling experimental native "
		    "code generation.\n***\n");
	}

	/*  Set cpu->useremul_syscall, and use userland_memory_rw:  */
	if (m->userland_emul != NULL) {
		useremul_name_to_useremul(cpu,
		    m->userland_emul, NULL, NULL, NULL);

		switch (m->arch) {
#ifdef ENABLE_ALPHA
		case ARCH_ALPHA:
			cpu->memory_rw = alpha_userland_memory_rw;
			break;
#endif
		default:cpu->memory_rw = userland_memory_rw;
		}
	}

	if (m->use_x11)
		x11_init(m);

	/*  Fill memory with random bytes:  */
	if (m->random_mem_contents) {
		for (i=0; i<m->physical_ram_in_mb * 1048576; i+=256) {
			unsigned char data[256];
			unsigned int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			cpu->memory_rw(cpu, m->memory, i, data, sizeof(data),
			    MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS | PHYSICAL);
		}
	}

	if (m->userland_emul != NULL) {
		/*
		 *  For userland-only emulation, no machine emulation
		 *  is needed.
		 */
	} else {
		for (i=0; i<n_devices; i++)
			device_add(m, device_names[i]);

		machine_setup(m);
	}

	diskimage_dump_info(m);
	console_debug_dump(m);

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (n_load == 0) {
		if (m->first_diskimage != NULL) {
			if (!load_bootblock(m, cpu, &n_load, &load_names)) {
				fprintf(stderr, "\nNo executable files were"
				    " specified, and booting directly from disk"
				    " failed.\n");
				exit(1);
			}
		} else {
			fprintf(stderr, "No executable file(s) loaded, and "
			    "we are not booting directly from a disk image."
			    "\nAborting.\n");
			exit(1);
		}
	}

	while (n_load > 0) {
		FILE *tmp_f;
		char *name_to_load = *load_names;
		int remove_after_load = 0;

		/*  Special hack for removing temporary files:  */
		if (name_to_load[0] == 8) {
			name_to_load ++;
			remove_after_load = 1;
		}

		/*
		 *  gzipped files are automagically gunzipped:
		 *  NOTE/TODO: This isn't secure. system() is used.
		 */
		tmp_f = fopen(name_to_load, "r");
		if (tmp_f != NULL) {
			unsigned char buf[2];		/*  gzip header  */
			memset(buf, 0, sizeof(buf));
			fread(buf, 1, sizeof(buf), tmp_f);
			if (buf[0]==0x1f && buf[1]==0x8b) {
				size_t zzlen = strlen(name_to_load)*2 + 100;
				char *zz = malloc(zzlen);
				debug("gunziping %s\n", name_to_load);
				/*
				 *  gzip header found.  If this was a file
				 *  extracted from, say, a CDROM image, then it
				 *  already has a temporary name. Otherwise we
				 *  have to gunzip into a temporary file.
				 */
				if (remove_after_load) {
					snprintf(zz, zzlen, "mv %s %s.gz",
					    name_to_load, name_to_load);
					system(zz);
					snprintf(zz, zzlen, "gunzip %s.gz",
					    name_to_load);
					system(zz);
				} else {
					/*  gunzip into new temp file:  */
					int tmpfile_handle;
					char *new_temp_name =
					    strdup("/tmp/gxemul.XXXXXXXXXXXX");
					tmpfile_handle = mkstemp(new_temp_name);
					close(tmpfile_handle);
					snprintf(zz, zzlen, "gunzip -c '%s' > "
					    "%s", name_to_load, new_temp_name);
					system(zz);
					name_to_load = new_temp_name;
					remove_after_load = 1;
				}
				free(zz);
			}
			fclose(tmp_f);
		}

		/*
		 *  Ugly (but usable) hack for Playstation Portable:  If the
		 *  filename ends with ".pbp" and the file contains an ELF
		 *  header, then extract the ELF file into a temporary file.
		 */
		if (strlen(name_to_load) > 4 && strcasecmp(name_to_load +
		    strlen(name_to_load) - 4, ".pbp") == 0 &&
		    (tmp_f = fopen(name_to_load, "r")) != NULL) {
			off_t filesize, j, found=0;
			unsigned char *buf;
			fseek(tmp_f, 0, SEEK_END);
			filesize = ftello(tmp_f);
			fseek(tmp_f, 0, SEEK_SET);
			buf = malloc(filesize);
			if (buf == NULL) {
				fprintf(stderr, "out of memory while trying"
				    " to read %s\n", name_to_load);
				exit(1);
			}
			fread(buf, 1, filesize, tmp_f);
			fclose(tmp_f);
			/*  Search for the ELF header, from offset 1 (!):  */
			for (j=1; j<filesize - 4; j++)
				if (memcmp(buf + j, ELFMAG, SELFMAG) == 0) {
					found = j;
					break;
				}
			if (found != 0) {
				int tmpfile_handle;
				char *new_temp_name =
				    strdup("/tmp/gxemul.XXXXXXXXXXXX");
				debug("extracting ELF from %s (offset 0x%x)\n",
				    name_to_load, (int)found);
				tmpfile_handle = mkstemp(new_temp_name);
				write(tmpfile_handle, buf + found,
				    filesize - found);
				close(tmpfile_handle);
				name_to_load = new_temp_name;
				remove_after_load = 1;
			}
		}

		byte_order = NO_BYTE_ORDER_OVERRIDE;

		/*
		 *  Load the file:  :-)
		 */
		file_load(m, m->memory, name_to_load, &entrypoint,
		    m->arch, &gp, &byte_order, &toc);

		if (remove_after_load) {
			debug("removing %s\n", name_to_load);
			unlink(name_to_load);
		}

		if (byte_order != NO_BYTE_ORDER_OVERRIDE)
			cpu->byte_order = byte_order;

		cpu->pc = entrypoint;

		switch (m->arch) {

		case ARCH_ALPHA:
			/*  For position-independent code:  */
			cpu->cd.alpha.r[ALPHA_T12] = cpu->pc;
			break;

		case ARCH_ARM:
			if (cpu->pc & 3) {
				fatal("ARM: lowest bits of pc set: TODO\n");
				exit(1);
			}
			cpu->pc &= 0xfffffffc;
			break;

		case ARCH_M88K:
			if (cpu->pc & 3) {
				fatal("M88K: lowest bits of pc set: TODO\n");
				exit(1);
			}
			cpu->pc &= 0xfffffffc;
			break;

		case ARCH_MIPS:
			if ((cpu->pc >> 32) == 0 && (cpu->pc & 0x80000000ULL))
				cpu->pc |= 0xffffffff00000000ULL;

			cpu->cd.mips.gpr[MIPS_GPR_GP] = gp;

			if ((cpu->cd.mips.gpr[MIPS_GPR_GP] >> 32) == 0 &&
			    (cpu->cd.mips.gpr[MIPS_GPR_GP] & 0x80000000ULL))
				cpu->cd.mips.gpr[MIPS_GPR_GP] |=
				    0xffffffff00000000ULL;
			break;

		case ARCH_PPC:
			/*  See http://www.linuxbase.org/spec/ELF/ppc64/
			    spec/x458.html for more info.  */
			cpu->cd.ppc.gpr[2] = toc;
			/*  TODO  */
			if (cpu->cd.ppc.bits == 32)
				cpu->pc &= 0xffffffffULL;
			break;

		case ARCH_SH:
			if (cpu->cd.sh.cpu_type.bits == 32)
				cpu->pc &= 0xffffffffULL;
			cpu->pc &= ~1;
			break;

		case ARCH_SPARC:
			break;

		default:
			fatal("emul_machine_setup(): Internal error: "
			    "Unimplemented arch %i\n", m->arch);
			exit(1);
		}

		/*
		 *  For userland emulation, the remaining items
		 *  on the command line will be passed as parameters
		 *  to the emulated program, and will not be treated
		 *  as filenames to load into the emulator.
		 *  The program's name will be in load_names[0], and the
		 *  rest of the parameters in load_names[1] and up.
		 */
		if (m->userland_emul != NULL)
			break;

		n_load --;
		load_names ++;
	}

	if (m->byte_order_override != NO_BYTE_ORDER_OVERRIDE)
		cpu->byte_order = m->byte_order_override;

	/*  Same byte order and entrypoint for all CPUs:  */
	for (i=0; i<m->ncpus; i++)
		if (i != m->bootstrap_cpu) {
			m->cpus[i]->byte_order = cpu->byte_order;
			m->cpus[i]->pc = cpu->pc;
		}

	if (m->userland_emul != NULL)
		useremul_setup(cpu, n_load, load_names);

	/*  Startup the bootstrap CPU:  */
	cpu->running = 1;

	/*  ... or pause all CPUs, if start_paused is set:  */
	if (m->start_paused) {
		for (i=0; i<m->ncpus; i++)
			m->cpus[i]->running = 0;
	}

	/*  Add PC dump points:  */
	add_dump_points(m);

	/*  TODO: This is MIPS-specific!  */
	if (m->machine_type == MACHINE_PMAX &&
	    cpu->cd.mips.cpu_type.mmu_model == MMU3K)
		add_symbol_name(&m->symbol_context,
		    0x9fff0000, 0x10000, "r2k3k_cache", 0, 0);

	symbol_recalc_sizes(&m->symbol_context);

	/*  Special hack for ARC/SGI emulation:  */
	if ((m->machine_type == MACHINE_ARC ||
	    m->machine_type == MACHINE_SGI) && m->prom_emulation)
		add_arc_components(m);

	debug("starting cpu%i at ", m->bootstrap_cpu);
	switch (m->arch) {

	case ARCH_ARM:
		/*  ARM cpus aren't 64-bit:  */
		debug("0x%08"PRIx32, (uint32_t) entrypoint);
		break;

	case ARCH_MIPS:
		if (cpu->is_32bit) {
			debug("0x%08"PRIx32, (uint32_t)
			    m->cpus[m->bootstrap_cpu]->pc);
			if (cpu->cd.mips.gpr[MIPS_GPR_GP] != 0)
				debug(" (gp=0x%08"PRIx32")", (uint32_t)
				    m->cpus[m->bootstrap_cpu]->cd.mips.gpr[
				    MIPS_GPR_GP]);
		} else {
			debug("0x%016"PRIx64, (uint64_t)
			    m->cpus[m->bootstrap_cpu]->pc);
			if (cpu->cd.mips.gpr[MIPS_GPR_GP] != 0)
				debug(" (gp=0x%016"PRIx64")", (uint64_t)
				    cpu->cd.mips.gpr[MIPS_GPR_GP]);
		}
		break;

	case ARCH_PPC:
		if (cpu->cd.ppc.bits == 32)
			debug("0x%08"PRIx32, (uint32_t) entrypoint);
		else
			debug("0x%016"PRIx64, (uint64_t) entrypoint);
		break;

	default:
		if (cpu->is_32bit)
			debug("0x%08"PRIx32, (uint32_t) cpu->pc);
		else
			debug("0x%016"PRIx64, (uint64_t) cpu->pc);
	}
	debug("\n");

	debug_indentation(-iadd);
}


/*
 *  emul_dumpinfo():
 *
 *  Dump info about all machines in an emul.
 */
void emul_dumpinfo(struct emul *e)
{
	int j, nm, iadd = DEBUG_INDENTATION;

	if (e->net != NULL)
		net_dumpinfo(e->net);

	nm = e->n_machines;
	for (j=0; j<nm; j++) {
		debug("machine %i: \"%s\"\n", j, e->machines[j]->name);
		debug_indentation(iadd);
		machine_dumpinfo(e->machines[j]);
		debug_indentation(-iadd);
	}
}


/*
 *  emul_simple_init():
 *
 *  For a normal setup:
 *
 *	o)  Initialize a network.
 *	o)  Initialize one machine.
 *
 *  For a userland-only setup:
 *
 *	o)  Initialize a "pseudo"-machine.
 */
void emul_simple_init(struct emul *emul)
{
	int iadd = DEBUG_INDENTATION;
	struct machine *m;

	if (emul->n_machines != 1) {
		fprintf(stderr, "emul_simple_init(): n_machines != 1\n");
		exit(1);
	}

	m = emul->machines[0];

	if (m->userland_emul == NULL) {
		debug("Simple setup...\n");
		debug_indentation(iadd);

		/*  Create a simple network:  */
		emul->net = net_init(emul, NET_INIT_FLAG_GATEWAY,
		    NET_DEFAULT_IPV4_MASK,
		    NET_DEFAULT_IPV4_LEN,
		    NULL, 0, 0, NULL);
	} else {
		/*  Userland pseudo-machine:  */
		debug("Syscall emulation (userland-only) setup...\n");
		debug_indentation(iadd);
	}

	/*  Create the machine:  */
	emul_machine_setup(m, extra_argc, extra_argv, 0, NULL);

	debug_indentation(-iadd);
}


/*
 *  emul_create_from_configfile():
 *
 *  Create an emul struct by reading settings from a configuration file.
 */
struct emul *emul_create_from_configfile(char *fname, int id)
{
	int iadd = DEBUG_INDENTATION;
	struct emul *e = emul_new(fname, id);

	debug("Creating emulation from configfile \"%s\":\n", fname);
	debug_indentation(iadd);

	emul_parse_config(e, fname);

	debug_indentation(-iadd);
	return e;
}


/*
 *  emul_run():
 *
 *	o)  Set up things needed before running emulations.
 *
 *	o)  Run emulations (one or more, in parallel).
 *
 *	o)  De-initialize things.
 */
void emul_run(struct emul **emuls, int n_emuls)
{
	struct emul *e;
	int i = 0, j, go = 1, n, anything;

	if (n_emuls < 1) {
		fprintf(stderr, "emul_run(): no thing to do\n");
		return;
	}

	atexit(fix_console);

	/*  Initialize the interactive debugger:  */
	debugger_init(emuls, n_emuls);

	/*  Run any additional debugger commands before starting:  */
	for (i=0; i<n_emuls; i++) {
		struct emul *emul = emuls[i];
		if (emul->n_debugger_cmds > 0) {
			int j;
			if (i == 0)
				print_separator();
			for (j = 0; j < emul->n_debugger_cmds; j ++) {
				debug("> %s\n", emul->debugger_cmds[j]);
				debugger_execute_cmd(emul->debugger_cmds[j],
				    strlen(emul->debugger_cmds[j]));
			}
		}
	}

	print_separator();
	debug("\n");


	/*
	 *  console_init_main() makes sure that the terminal is in a
	 *  reasonable state.
	 *
	 *  The SIGINT handler is for CTRL-C  (enter the interactive debugger).
	 *
	 *  The SIGCONT handler is invoked whenever the user presses CTRL-Z
	 *  (or sends SIGSTOP) and then continues. It makes sure that the
	 *  terminal is in an expected state.
	 */
	console_init_main(emuls[0]);	/*  TODO: what is a good argument?  */
	signal(SIGINT, debugger_activate);
	signal(SIGCONT, console_sigcont);

	/*  Not in verbose mode? Then set quiet_mode.  */
	if (!verbose)
		quiet_mode = 1;

	/*  Initialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_init(e->machines[j]);
	}

	/*  TODO: Generalize:  */
	if (emuls[0]->machines[0]->show_trace_tree)
		cpu_functioncall_trace(emuls[0]->machines[0]->cpus[0],
		    emuls[0]->machines[0]->cpus[0]->pc);

	/*  Start emulated clocks:  */
	timer_start();

	/*
	 *  MAIN LOOP:
	 *
	 *  Run all emulations in parallel, running each machine in
	 *  each emulation.
	 */
	while (go) {
		go = 0;

		/*  Flush X11 and serial console output every now and then:  */
		if (emuls[0]->machines[0]->ninstrs >
		    emuls[0]->machines[0]->ninstrs_flush + (1<<19)) {
			x11_check_event(emuls, n_emuls);
			console_flush();
			emuls[0]->machines[0]->ninstrs_flush =
			    emuls[0]->machines[0]->ninstrs;
		}

		if (emuls[0]->machines[0]->ninstrs >
		    emuls[0]->machines[0]->ninstrs_show + (1<<25)) {
			emuls[0]->machines[0]->ninstrs_since_gettimeofday +=
			    (emuls[0]->machines[0]->ninstrs -
			     emuls[0]->machines[0]->ninstrs_show);
			cpu_show_cycles(emuls[0]->machines[0], 0);
			emuls[0]->machines[0]->ninstrs_show =
			    emuls[0]->machines[0]->ninstrs;
		}

		if (single_step == ENTER_SINGLE_STEPPING) {
			/*  TODO: Cleanup!  */
			old_instruction_trace =
			    emuls[0]->machines[0]->instruction_trace;
			old_quiet_mode = quiet_mode;
			old_show_trace_tree =
			    emuls[0]->machines[0]->show_trace_tree;
			emuls[0]->machines[0]->instruction_trace = 1;
			emuls[0]->machines[0]->show_trace_tree = 1;
			quiet_mode = 0;
			single_step = SINGLE_STEPPING;
		}

		if (single_step == SINGLE_STEPPING)
			debugger();

		for (i=0; i<n_emuls; i++) {
			e = emuls[i];

			for (j=0; j<e->n_machines; j++) {
				anything = machine_run(e->machines[j]);
				if (anything)
					go = 1;
			}
		}
	}

	/*  Stop any running timers:  */
	timer_stop();

	/*  Deinitialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_deinit(e->machines[j]);
	}

	/*  force_debugger_at_exit flag set? Then enter the debugger:  */
	if (force_debugger_at_exit) {
		quiet_mode = 0;
		debugger_reset();
		debugger();
	}

	/*  Any machine using X11? Then wait before exiting:  */
	n = 0;
	for (i=0; i<n_emuls; i++)
		for (j=0; j<emuls[i]->n_machines; j++)
			if (emuls[i]->machines[j]->use_x11)
				n++;
	if (n > 0) {
		printf("Press enter to quit.\n");
		while (!console_charavail(MAIN_CONSOLE)) {
			x11_check_event(emuls, n_emuls);
			usleep(10000);
		}
		console_readchar(MAIN_CONSOLE);
	}

	console_deinit_main();
}

