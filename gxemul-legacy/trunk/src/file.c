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
 *  $Id: file.c,v 1.47 2005-01-19 14:42:11 debug Exp $
 *
 *  This file contains functions which load executable images into (emulated)
 *  memory.  File formats recognized so far:
 *
 *	ELF		32-bit and 64-bit MSB and LSB MIPS
 *	a.out		some format used by OpenBSD 2.x pmax kernels
 *	ecoff		old format used by Ultrix, Windows NT, etc
 *	srec		Motorola SREC format
 *	raw		raw binaries ("address:[skiplen:]filename")
 *
 *  If a file is not of one of the above mentioned formats, it is assumed
 *  to be symbol data generated by 'nm' or 'nm -S'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "misc.h"

#include "exec_aout.h"
#include "exec_ecoff.h"
#include "exec_elf.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


#ifdef HACK_STRTOLL
#define strtoll strtol
#define strtoull strtoul
#endif


/*
 *  This should be increased by every routine here that actually loads an
 *  executable file into memory.  (For example, loading a symbol file should
 *  NOT increase this.)
 */
static int n_executables_loaded = 0;


struct aout_symbol {
	uint32_t	strindex;
	uint32_t	type;
	uint32_t	addr;
};


extern uint64_t file_loaded_end_addr;


#define	unencode(var,dataptr,typ)	{				\
		int Wi;  unsigned char Wb;				\
		unsigned char *Wp = (unsigned char *) dataptr;		\
		int Wlen = sizeof(typ);					\
		var = 0;						\
		for (Wi=0; Wi<Wlen; Wi++) {				\
			if (encoding == ELFDATA2LSB)			\
				Wb = Wp[Wlen-1 - Wi];			\
			else						\
				Wb = Wp[Wi];				\
			if (Wi == 0 && (Wb & 0x80)) {			\
				var --;	/*  set var to -1 :-)  */	\
				var <<= 8;				\
			}						\
			var |= Wb;					\
			if (Wi < Wlen-1)				\
				var <<= 8;				\
		}							\
	}


/*
 *  file_load_aout():
 *
 *  Loads an a.out binary image into the emulated memory.  The entry point
 *  (read from the a.out header) is stored in the specified CPU's registers.
 *
 *  TODO:  This has to be rewritten / corrected to support multiple a.out
 *         formats, where text/data are aligned differently.
 */
static void file_load_aout(struct machine *m, struct memory *mem,
	char *filename, struct cpu *cpu, int osf1_hack)
{
	struct exec aout_header;
	FILE *f;
	int len;
	int encoding = ELFDATA2LSB;
	uint32_t entry, datasize, textsize;
	int32_t symbsize;
	uint32_t vaddr, total_len;
	unsigned char buf[1024];
	unsigned char *syms;

	f = fopen(filename, "r");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	if (osf1_hack) {
		fread(&buf, 1, 32, f);
		vaddr = buf[16] + (buf[17] << 8) + (buf[18] << 16) + (buf[19] << 24);
		entry = buf[20] + (buf[21] << 8) + (buf[22] << 16) + (buf[23] << 24);
		debug("OSF1 a.out-ish hack, load address 0x%08lx, entry point 0x%08x\n",
		    (long)vaddr, (long)entry);
		symbsize = 0;
		fseek(f, 0, SEEK_END);
		textsize = ftell(f) - 512;	/*  This is of course wrong, but should work anyway  */
		datasize = 0;
		fseek(f, 512, SEEK_SET);
	} else {
		len = fread(&aout_header, 1, sizeof(aout_header), f);
		if (len != sizeof(aout_header)) {
			fprintf(stderr, "%s: not a complete a.out image\n", filename);
			exit(1);
		}

		unencode(entry, &aout_header.a_entry, uint32_t);
		vaddr = entry;
		debug("a.out, entry point 0x%08lx\n", (long)entry);

		unencode(textsize, &aout_header.a_text, uint32_t);
		unencode(datasize, &aout_header.a_data, uint32_t);
		debug("text+data = %i+%i bytes\n", textsize, datasize);

		unencode(symbsize, &aout_header.a_syms, uint32_t);
	}

	/*  Load text and data:  */
	total_len = textsize + datasize;
	while (total_len != 0) {
		len = total_len > sizeof(buf) ? sizeof(buf) : total_len;
		len = fread(buf, 1, len, f);

		/*  printf("fread len=%i vaddr=%x buf[0..]=%02x %02x %02x\n", len, (int)vaddr, buf[0], buf[1], buf[2]);  */

		if (len > 0)
			memory_rw(cpu, mem, vaddr, &buf[0], len,
			    MEM_WRITE, NO_EXCEPTIONS);
		else {
			if (osf1_hack)
				break;
			else {
				fprintf(stderr, "could not read from %s\n", filename);
				exit(1);
			}
		}

		vaddr += len;
		total_len -= len;
	}

	if (symbsize != 0) {
		struct aout_symbol *aout_symbol_ptr;
		int i, n_symbols;
		uint32_t type, addr, str_index;
		uint32_t strings_len;
		char *string_symbols;
		off_t oldpos;

		debug("symbols = %i bytes @ 0x%x\n", symbsize, (int)ftell(f));
		syms = malloc(symbsize);
		if (syms == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		len = fread(syms, 1, symbsize, f);
		if (len != symbsize) {
			fprintf(stderr, "error reading symbols from %s\n", filename);
			exit(1);
		}

		oldpos = ftell(f);
		fseek(f, 0, SEEK_END);
		strings_len = ftell(f) - oldpos;
		fseek(f, oldpos, SEEK_SET);
		debug("strings = %i bytes @ 0x%x\n", strings_len, (int)ftell(f));
		string_symbols = malloc(strings_len);
		if (string_symbols == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		fread(string_symbols, 1, strings_len, f);

		aout_symbol_ptr = (struct aout_symbol *) syms;
		n_symbols = symbsize / sizeof(struct aout_symbol);
		i = 0;
		while (i < n_symbols) {
			unencode(str_index, &aout_symbol_ptr[i].strindex, uint32_t);
			unencode(type,      &aout_symbol_ptr[i].type,     uint32_t);
			unencode(addr,      &aout_symbol_ptr[i].addr,     uint32_t);

			/* debug("symbol type 0x%04x @ 0x%08x: %s\n", type, addr, string_symbols + str_index); */
			if (type != 0 && addr != 0)
				add_symbol_name(&m->symbol_context,
				    addr, 0, string_symbols + str_index, 0);
			i++;
		}

		free(string_symbols);
		free(syms);
	}

	fclose(f);

	cpu->pc = entry;

	if (encoding == ELFDATA2LSB)
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
	else
		cpu->byte_order = EMUL_BIG_ENDIAN;

	n_executables_loaded ++;
}


/*
 *  file_load_ecoff():
 *
 *  Loads an ecoff binary image into the emulated memory.  The entry point
 *  (read from the ecoff header) is stored in the specified CPU's registers.
 */
static void file_load_ecoff(struct machine *m, struct memory *mem,
	char *filename, struct cpu *cpu)
{
	struct ecoff_exechdr exechdr;
	int f_magic, f_nscns, f_nsyms;
	int a_magic;
	off_t f_symptr, a_tsize, a_dsize, a_bsize;
	uint64_t a_entry, a_tstart, a_dstart, a_bstart, a_gp, end_addr=0;
	char *format_name;
	struct ecoff_scnhdr scnhdr;
	FILE *f;
	int len, secn, total_len, chunk_size;
	int encoding = ELFDATA2LSB;	/*  Assume little-endian. See below  */
	unsigned char buf[4096];

	f = fopen(filename, "r");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	len = fread(&exechdr, 1, sizeof(exechdr), f);
	if (len != sizeof(exechdr)) {
		fprintf(stderr, " not a complete ecoff image\n");
		exit(1);
	}

	/*
	 *  The following code looks a bit ugly, but it should work. The ECOFF
	 *  16-bit magic value seems to be stored in MSB byte order for
	 *  big-endian binaries, and LSB byte order for little-endian binaries.
	 *
	 *  The unencode() assumes little-endianness by default.
	 */
	unencode(f_magic, &exechdr.f.f_magic, uint16_t);
	switch (f_magic) {
	case ((ECOFF_MAGIC_MIPSEB & 0xff) << 8) + ((ECOFF_MAGIC_MIPSEB >> 8) & 0xff):
		format_name = "MIPS1 BE";
		encoding = ELFDATA2MSB;
		break;
	case ECOFF_MAGIC_MIPSEL:
		format_name = "MIPS1 LE";
		encoding = ELFDATA2LSB;
		break;
	case ((ECOFF_MAGIC_MIPSEB2 & 0xff) << 8) + ((ECOFF_MAGIC_MIPSEB2 >> 8) & 0xff):
		format_name = "MIPS2 BE";
		encoding = ELFDATA2MSB;
		break;
	case ECOFF_MAGIC_MIPSEL2:
		format_name = "MIPS2 LE";
		encoding = ELFDATA2LSB;
		break;
	case ((ECOFF_MAGIC_MIPSEB3 & 0xff) << 8) + ((ECOFF_MAGIC_MIPSEB3 >> 8) & 0xff):
		format_name = "MIPS3 BE";
		encoding = ELFDATA2MSB;
		break;
	case ECOFF_MAGIC_MIPSEL3:
		format_name = "MIPS3 LE";
		encoding = ELFDATA2LSB;
		break;
	default:
		fprintf(stderr, "%s: unknown ecoff format, magic = 0x%04x\n", filename, f_magic);
		exit(1);
	}

	/*  Read various header information:  */
	unencode(f_nscns,  &exechdr.f.f_nscns,  uint16_t);
	unencode(f_symptr, &exechdr.f.f_symptr, uint32_t);
	unencode(f_nsyms,  &exechdr.f.f_nsyms,  uint32_t);
	debug("ECOFF, %s, %i sections, %i symbols @ 0x%lx\n",
	    format_name, f_nscns, f_nsyms, (long)f_symptr);

	unencode(a_magic, &exechdr.a.magic, uint16_t);
	unencode(a_tsize, &exechdr.a.tsize, uint32_t);
	unencode(a_dsize, &exechdr.a.dsize, uint32_t);
	unencode(a_bsize, &exechdr.a.bsize, uint32_t);
	debug("magic 0x%04x, tsize 0x%x, dsize 0x%x, bsize 0x%x\n",
	    a_magic, (int)a_tsize, (int)a_dsize, (int)a_bsize);

	unencode(a_tstart, &exechdr.a.text_start, uint32_t);
	unencode(a_dstart, &exechdr.a.data_start, uint32_t);
	unencode(a_bstart, &exechdr.a.bss_start,  uint32_t);
	debug("text @ 0x%08x, data @ 0x%08x, bss @ 0x%08x\n",
	    (int)a_tstart, (int)a_dstart, (int)a_bstart);

	unencode(a_entry, &exechdr.a.entry,    uint32_t);
	unencode(a_gp,    &exechdr.a.gp_value, uint32_t);
	debug("entrypoint 0x%08x, gp = 0x%08x\n",
	    (int)a_entry, (int)a_gp);

	/*
	 *  Special hack for a MACH/pmax kernel, I don't know how applicable
	 *  this is for other files:
	 *  there are no sections (!), and a_magic = 0x0108 instead of
	 *  0x0107 as it is on most other (E)COFF files I've seen.
	 *
	 *  Then load everything after the header to the text start address.
	 */
	if (f_nscns == 0 && a_magic == 0x108) {
		uint64_t where = a_tstart;
		total_len = 0;
		fseek(f, 0x50, SEEK_SET);
		while (!feof(f)) {
			chunk_size = 256;
			len = fread(buf, 1, chunk_size, f);

			if (len > 0)
				memory_rw(cpu, mem, where, &buf[0], len,
				    MEM_WRITE, NO_EXCEPTIONS);
			where += len;
			total_len += len;
		}
		debug("MACH/pmax hack (!), read 0x%x bytes\n", total_len);
	}

	/*  Go through all the section headers:  */
	for (secn=0; secn<f_nscns; secn++) {
		off_t s_scnptr, s_relptr, s_lnnoptr, oldpos;
		int s_nreloc, s_nlnno, s_flags;
		int s_size;
		unsigned int i;
		uint64_t s_paddr, s_vaddr;

		/*  Read a section header:  */
		len = fread(&scnhdr, 1, sizeof(scnhdr), f);
		if (len != sizeof(scnhdr)) {
			fprintf(stderr, "%s: incomplete section header %i\n", filename, secn);
			exit(1);
		}

		/*  Show the section name:  */
		debug("section ");
		for (i=0; i<sizeof(scnhdr.s_name); i++)
			if (scnhdr.s_name[i] >= 32 && scnhdr.s_name[i] < 127)
				debug("%c", scnhdr.s_name[i]);
			else
				break;
		debug(" (");

		unencode(s_paddr,   &scnhdr.s_paddr,   uint32_t);
		unencode(s_vaddr,   &scnhdr.s_vaddr,   uint32_t);
		unencode(s_size,    &scnhdr.s_size,    uint32_t);
		unencode(s_scnptr,  &scnhdr.s_scnptr,  uint32_t);
		unencode(s_relptr,  &scnhdr.s_relptr,  uint32_t);
		unencode(s_lnnoptr, &scnhdr.s_lnnoptr, uint32_t);
		unencode(s_nreloc,  &scnhdr.s_nreloc,  uint16_t);
		unencode(s_nlnno,   &scnhdr.s_nlnno,   uint16_t);
		unencode(s_flags,   &scnhdr.s_flags,   uint32_t);

		debug("0x%x bytes @ 0x%08x, file offset 0x%lx, flags 0x%x)\n",
		    (int)s_size, (int)s_vaddr, (long)s_scnptr, (int)s_flags);

		end_addr = s_vaddr + s_size;

		if (s_relptr != 0) {
			fprintf(stderr, "%s: relocatable code/data in section nr %i: not yet implemented\n",
			    filename, secn);
			exit(1);
		}

		/*  Loadable? Then load the section:  */
		if (s_scnptr != 0 && s_size != 0 &&
		    s_vaddr != 0 && !(s_flags & 0x02)) {
			/*  Remember the current file offset:  */
			oldpos = ftell(f);

			/*  Load the section into emulated memory:  */
			fseek(f, s_scnptr, SEEK_SET);
			total_len = 0;
			chunk_size = 1;
			if ((s_vaddr & 0xf) == 0)	chunk_size = 0x10;
			if ((s_vaddr & 0xff) == 0)	chunk_size = 0x100;
			if ((s_vaddr & 0xfff) == 0)	chunk_size = 0x1000;
			while (total_len < s_size) {
				len = chunk_size;
				if (total_len + len > s_size)
					len = s_size - total_len;
				len = fread(buf, 1, chunk_size, f);
				if (len == 0) {
					debug("!!! total_len = %i, chunk_size = %i, len = %i\n",
					    total_len, chunk_size, len);
					break;
				}

				memory_rw(cpu, mem, s_vaddr, &buf[0], len,
				    MEM_WRITE, NO_EXCEPTIONS);
				s_vaddr += len;
				total_len += len;
			}

			/*  Return to position inside the section headers:  */
			fseek(f, oldpos, SEEK_SET);
		}
	}

	if (f_symptr != 0 && f_nsyms != 0) {
		struct ecoff_symhdr symhdr;
		int sym_magic, iextMax, issExtMax, issMax, crfd;
		off_t cbRfdOffset, cbExtOffset, cbSsExtOffset, cbSsOffset;
		char *symbol_data;
		struct ecoff_extsym *extsyms;
		int nsymbols, sym_nr;

		fseek(f, f_symptr, SEEK_SET);

		len = fread(&symhdr, 1, sizeof(symhdr), f);
		if (len != sizeof(symhdr)) {
			fprintf(stderr, "%s: not a complete ecoff image: symhdr broken\n", filename);
			exit(1);
		}

		unencode(sym_magic,     &symhdr.magic,         uint16_t);
		unencode(crfd,          &symhdr.crfd,          uint32_t);
		unencode(cbRfdOffset,   &symhdr.cbRfdOffset,   uint32_t);
		unencode(issMax,        &symhdr.issMax,        uint32_t);
		unencode(cbSsOffset,    &symhdr.cbSsOffset,    uint32_t);
		unencode(issExtMax,     &symhdr.issExtMax,     uint32_t);
		unencode(cbSsExtOffset, &symhdr.cbSsExtOffset, uint32_t);
		unencode(iextMax,       &symhdr.iextMax,       uint32_t);
		unencode(cbExtOffset,   &symhdr.cbExtOffset,   uint32_t);

		if (sym_magic != MIPS_MAGIC_SYM) {
			debug("symbol magic = 0x%x: ", sym_magic);
			switch (sym_magic) {
			case 0x722e:
				debug("possibly Windows NT (?)");
				break;
			default:
				debug("UNKNOWN");
			}
			debug("\n");
			goto unknown_coff_symbols;
		}

		debug("symbol header: magic = 0x%x\n", sym_magic);

		debug("%i symbols @ 0x%08x (strings @ 0x%08x)\n",
		    iextMax, cbExtOffset, cbSsExtOffset);

		symbol_data = malloc(issExtMax + 2);
		if (symbol_data == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(symbol_data, 0, issExtMax + 2);
		fseek(f, cbSsExtOffset, SEEK_SET);
		fread(symbol_data, 1, issExtMax + 1, f);

		nsymbols = iextMax;

		extsyms = malloc(iextMax * sizeof(struct ecoff_extsym));
		if (extsyms == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(extsyms, 0, iextMax * sizeof(struct ecoff_extsym));
		fseek(f, cbExtOffset, SEEK_SET);
		fread(extsyms, 1, iextMax * sizeof(struct ecoff_extsym), f);

		/*  Unencode the strindex and value first:  */
		for (sym_nr=0; sym_nr<nsymbols; sym_nr++) {
			uint64_t value, strindex;

			unencode(strindex, &extsyms[sym_nr].es_strindex, uint32_t);
			unencode(value,    &extsyms[sym_nr].es_value,    uint32_t);

			extsyms[sym_nr].es_strindex = strindex;
			extsyms[sym_nr].es_value    = value;
		}

		for (sym_nr=0; sym_nr<nsymbols; sym_nr++) {
			/*  debug("symbol%6i: 0x%08x = %s\n",
			    sym_nr, (int)extsyms[sym_nr].es_value,
			    symbol_data + extsyms[sym_nr].es_strindex);  */

			add_symbol_name(&m->symbol_context,
			    extsyms[sym_nr].es_value, 0,
			    symbol_data + extsyms[sym_nr].es_strindex, 0);
		}

		free(extsyms);
		free(symbol_data);

unknown_coff_symbols:
		;
	}

	fclose(f);

	cpu->pc              = a_entry;
	cpu->gpr[GPR_GP]     = a_gp;
	file_loaded_end_addr = end_addr;

	if (encoding == ELFDATA2LSB)
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
	else
		cpu->byte_order = EMUL_BIG_ENDIAN;

	n_executables_loaded ++;
}


/*
 *  file_load_srec():
 *
 *  Loads a Motorola SREC file into emulated memory. Description of the SREC
 *  file format can be found at here:
 *
 *      http://www.ndsu.nodak.edu/instruct/tareski/373f98/notes/srecord.htm
 *  or  http://www.amelek.gda.pl/avr/uisp/srecord.htm
 */
static void file_load_srec(struct memory *mem, char *filename, struct cpu *cpu)
{
	FILE *f;
	unsigned char buf[516];
	unsigned char bytes[270];
	uint64_t entry = 0, vaddr = 0;
	int i, j, count;
	char ch;
	int buf_len, data_start = 0;
	int entry_set = 0;
	int warning = 0;
	int warning_len = 0;
	int total_bytes_loaded = 0;

	f = fopen(filename, "r");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	/*  Load file contents:  */
	while (!feof(f)) {
		memset(buf, 0, sizeof(buf));
		fgets((char *)buf, sizeof(buf)-1, f);

		if (buf[0] == 0 || buf[0]=='\r' || buf[0]=='\n')
			continue;

		if (buf[0] != 'S') {
			if (!warning)
				debug("WARNING! non-S-record found\n");
			warning = 1;
			continue;
		}

		buf_len = strlen((char *)buf);

		if (buf_len < 10) {
			if (!warning_len)
				debug("WARNING! invalid S-record found\n");
			warning_len = 1;
			continue;
		}

		/*
		 *  Stype count address data checksum
		 *  01    23    4..     ..   (last 2 bytes)
		 *
		 *  TODO: actually check the checksum
		 */

		j = 0;
		for (i=1; i<buf_len; i++) {
			if (buf[i]>='a' && buf[i]<='f')
				buf[i] += 10 - 'a';
			else if (buf[i] >= 'A' && buf[i] <= 'F')
				buf[i] += 10 - 'A';
			else if (buf[i] >= '0' && buf[i] <= '9')
				buf[i] -= '0';
			else if (buf[i] == '\r' || buf[i] == '\n') {
			} else
				fatal("invalid characters '%c' in S-record\n", buf[i]);

			if (i >= 4) {
				if (i & 1)
					bytes[j++] += buf[i];
				else
					bytes[j] = buf[i] * 16;
			}
		}

		count = buf[2] * 16 + buf[3];
		/*  debug("count=%i j=%i\n", count, j);  */
		/*  count is j - 1.  */

		switch (buf[1]) {
		case 0:
			debug("SREC \"");
			for (i=2; i<count-1; i++) {
				ch = bytes[i];
				if (ch >= ' ' && ch < 127)
					debug("%c", ch);
				else
					debug("?");
			}
			debug("\"\n");
			break;
		case 1:
		case 2:
		case 3:
			/*  switch again, to get the load address:  */
			switch (buf[1]) {
			case 1:	data_start = 2;
				vaddr = (bytes[0] << 8) + bytes[1];
				break;
			case 2:	data_start = 3;
				vaddr = (bytes[0] << 16) + (bytes[1] << 8) + bytes[2];
				break;
			case 3:	data_start = 4;
				vaddr = (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
			}
			memory_rw(cpu, mem, vaddr, &bytes[data_start],
			    count - 1 - data_start, MEM_WRITE, NO_EXCEPTIONS);
			total_bytes_loaded += count - 1 - data_start;
			break;
		case 7:
		case 8:
		case 9:
			/*  switch again, to get the entry point:  */
			switch (buf[1]) {
			case 7:	entry = (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
				break;
			case 8:	entry = (bytes[0] << 16) + (bytes[1] << 8) + bytes[2];
				break;
			case 9:	entry = (bytes[0] << 8) + bytes[1];
				break;
			}
			entry_set = 1;
			debug("entry point 0x%08x\n", (unsigned int)entry);
			break;
		default:
			debug("unimplemented S-record type %i\n", buf[1]);
		}
	}

	debug("0x%x bytes loaded\n", total_bytes_loaded);

	fclose(f);

	if (!entry_set)
		debug("WARNING! no entrypoint found!\n");
	else
		cpu->pc = entry;

	n_executables_loaded ++;
}


/*
 *  file_load_raw():
 *
 *  Loads a raw binary into emulated memory. The filename should be
 *  of the following form:     loadaddress:filename
 *  or    loadaddress:skiplen:filename
 */
static void file_load_raw(struct memory *mem, char *filename, struct cpu *cpu)
{
	FILE *f;
	int len;
	unsigned char buf[4096];
	uint64_t entry, vaddr, skip = 0;
	char *p, *p2;

	p = strchr(filename, ':');
	if (p == NULL) {
		fprintf(stderr, "error opening %s\n", filename);
		exit(1);
	}

	entry = strtoll(filename, NULL, 0);
	p2 = p+1;

	/*  A second value? That's the optional skip value  */
	p = strchr(p2, ':');
	if (p != NULL) {
		skip = strtoll(p2, NULL, 0);
		p = p+1;
	} else
		p = p2;

	f = fopen(p, "r");
	if (f == NULL) {
		perror(p);
		exit(1);
	}

	vaddr = entry;
	fseek(f, skip, SEEK_SET);

	/*  Load file contents:  */
	while (!feof(f)) {
		len = fread(buf, 1, sizeof(buf), f);

		if (len > 0)
			memory_rw(cpu, mem, vaddr, &buf[0],
			    len, MEM_WRITE, NO_EXCEPTIONS);

		vaddr += len;
	}

	debug("RAW: 0x%llx bytes loaded at 0x%08llx",
	    (long long) (ftell(f) - skip), (long long)entry);
	if (skip != 0)
		debug(" (0x%llx bytes of header skipped)", (long long)skip);
	debug("\n");

	fclose(f);

	cpu->pc = entry;

	n_executables_loaded ++;
}


/*
 *  file_load_elf():
 *
 *  Loads an ELF image into the emulated memory.  The entry point (read from
 *  the ELF header) and the initial value of the gp register (read from the
 *  ELF symbol table) are stored in the specified CPU's registers.
 *
 *  This is pretty heavy stuff, but is needed because of the heaviness of
 *  ELF files. :-/   Hopefully it will be able to recognize most valid MIPS
 *  executables.
 */
static void file_load_elf(struct machine *m, struct memory *mem,
	char *filename, struct cpu *cpu)
{
	Elf32_Ehdr hdr32;
	Elf64_Ehdr hdr64;
	FILE *f;
	uint64_t eentry;
	int len, i;
	int elf64, encoding, eflags;
	int etype, emachine;
	int ephnum, ephentsize, eshnum, eshentsize;
	off_t ephoff, eshoff;
	Elf32_Phdr phdr32;
	Elf64_Phdr phdr64;
	Elf32_Shdr shdr32;
	Elf64_Shdr shdr64;
	Elf32_Sym sym32;
	Elf64_Sym sym64;
	int ofs;
	int chunk_len = 1024;
	int align_len;
	char *symbol_strings = NULL; size_t symbol_length = 0;
	Elf32_Sym *symbols_sym32 = NULL;  int n_symbols = 0;
	Elf64_Sym *symbols_sym64 = NULL;

	f = fopen(filename, "r");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	len = fread(&hdr32, 1, sizeof(Elf32_Ehdr), f);
	if (len < (signed int)sizeof(Elf32_Ehdr)) {
		fprintf(stderr, "%s: not an ELF file image\n", filename);
		exit(1);
	}

	if (memcmp(&hdr32.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0) {
		fprintf(stderr, "%s: not an ELF file image\n", filename);
		exit(1);
	}

	switch (hdr32.e_ident[EI_CLASS]) {
	case ELFCLASS32:
		elf64 = 0;
		break;
	case ELFCLASS64:
		elf64 = 1;
		fseek(f, 0, SEEK_SET);
		len = fread(&hdr64, 1, sizeof(Elf64_Ehdr), f);
		if (len < (signed int)sizeof(Elf64_Ehdr)) {
			fprintf(stderr, "%s: not an ELF64 file image\n", filename);
			exit(1);
		}
		break;
	default:
		fprintf(stderr, "%s: unknown ELF class '%i'\n", filename, hdr32.e_ident[EI_CLASS]);
		exit(1);
	}

	encoding = hdr32.e_ident[EI_DATA];
	if (encoding != ELFDATA2LSB && encoding != ELFDATA2MSB) {
		fprintf(stderr, "%s: unknown data encoding '%i'\n", filename, hdr32.e_ident[EI_DATA]);
		exit(1);
	}

	if (elf64) {
		unencode(etype,      &hdr64.e_type,      Elf64_Quarter);
		unencode(eflags,     &hdr64.e_flags,     Elf64_Half);
		unencode(emachine,   &hdr64.e_machine,   Elf64_Quarter);
		unencode(eentry,     &hdr64.e_entry,     Elf64_Addr);
		unencode(ephnum,     &hdr64.e_phnum,     Elf64_Quarter);
		unencode(ephentsize, &hdr64.e_phentsize, Elf64_Quarter);
		unencode(ephoff,     &hdr64.e_phoff,     Elf64_Off);
		unencode(eshnum,     &hdr64.e_shnum,     Elf64_Quarter);
		unencode(eshentsize, &hdr64.e_shentsize, Elf64_Quarter);
		unencode(eshoff,     &hdr64.e_shoff,     Elf64_Off);
		if (ephentsize != sizeof(Elf64_Phdr)) {
			fprintf(stderr, "%s: incorrect phentsize? %i, should be %i\n",
			    filename, ephentsize, (int)sizeof(Elf64_Phdr));
			exit(1);
		}
		if (eshentsize != sizeof(Elf64_Shdr)) {
			fprintf(stderr, "%s: incorrect phentsize? %i, should be %i\n",
			    filename, ephentsize, (int)sizeof(Elf64_Shdr));
			exit(1);
		}
	} else {
		unencode(etype,      &hdr32.e_type,      Elf32_Half);
		unencode(eflags,     &hdr32.e_flags,     Elf32_Word);
		unencode(emachine,   &hdr32.e_machine,   Elf32_Half);
		unencode(eentry,     &hdr32.e_entry,     Elf32_Addr);
		unencode(ephnum,     &hdr32.e_phnum,     Elf32_Half);
		unencode(ephentsize, &hdr32.e_phentsize, Elf32_Half);
		unencode(ephoff,     &hdr32.e_phoff,     Elf32_Off);
		unencode(eshnum,     &hdr32.e_shnum,     Elf32_Half);
		unencode(eshentsize, &hdr32.e_shentsize, Elf32_Half);
		unencode(eshoff,     &hdr32.e_shoff,     Elf32_Off);
		if (ephentsize != sizeof(Elf32_Phdr)) {
			fprintf(stderr, "%s: incorrect phentsize? %i, should be %i\n",
			    filename, ephentsize, (int)sizeof(Elf32_Phdr));
			exit(1);
		}
		if (eshentsize != sizeof(Elf32_Shdr)) {
			fprintf(stderr, "%s: incorrect phentsize? %i, should be %i\n",
			    filename, ephentsize, (int)sizeof(Elf32_Shdr));
			exit(1);
		}
	}

	if ( etype != ET_EXEC ) {
		fprintf(stderr, "%s is not an ELF Executable file, type = %i\n", filename, etype);
		exit(1);
	}

	if (emachine != EM_MIPS && emachine != EM_MIPS_RS3_LE) {
		fprintf(stderr, "%s: unknown machine type '%i' (not MIPS?)\n", filename, emachine);
		exit(1);
	}

	debug("ELF%i %s, entry point 0x", elf64? 64 : 32,
	    encoding == ELFDATA2LSB? "LSB (LE)" : "MSB (BE)");
	if (elf64)
		debug("%016llx\n", (long long)eentry);
	else
		debug("%08x\n", (int)eentry);

	/*
	 *  MIPS16 encoding?
	 *
	 *  TODO:  Find out what e_flag actually contains.
	 */

	if (((eflags >> 24) & 0xff) == 0x24) {
		debug("MIPS16 encoding (e_flags = 0x%08x)\n", eflags);
#ifdef ENABLE_MIPS16
		cpu->mips16 = 1;
#else
		fatal("ENABLE_MIPS16 must be defined in misc.h.\n");
		exit(1);
#endif
	} else if (eentry & 0x3) {
		debug("MIPS16 encoding (eentry not 32-bit aligned)\n");
#ifdef ENABLE_MIPS16
		cpu->mips16 = 1;
#else
		fatal("ENABLE_MIPS16 must be defined in misc.h.\n");
		exit(1);
#endif
	}

	/*  Read the program headers:  */

	for (i=0; i<ephnum; i++) {
		int p_type;
		uint64_t p_offset;
		uint64_t p_vaddr;
		uint64_t p_paddr;
		uint64_t p_filesz;
		uint64_t p_memsz;
		int p_flags;
		int p_align;

		fseek(f, ephoff + i * ephentsize, SEEK_SET);

		if (elf64) {
			fread(&phdr64, 1, sizeof(Elf64_Phdr), f);
			unencode(p_type,    &phdr64.p_type,    Elf64_Half);
			unencode(p_flags,   &phdr64.p_flags,   Elf64_Half);
			unencode(p_offset,  &phdr64.p_offset,  Elf64_Off);
			unencode(p_vaddr,   &phdr64.p_vaddr,   Elf64_Addr);
			unencode(p_paddr,   &phdr64.p_paddr,   Elf64_Addr);
			unencode(p_filesz,  &phdr64.p_filesz,  Elf64_Xword);
			unencode(p_memsz,   &phdr64.p_memsz,   Elf64_Xword);
			unencode(p_align,   &phdr64.p_align,   Elf64_Xword);
		} else {
			fread(&phdr32, 1, sizeof(Elf32_Phdr), f);
			unencode(p_type,    &phdr32.p_type,    Elf32_Word);
			unencode(p_offset,  &phdr32.p_offset,  Elf32_Off);
			unencode(p_vaddr,   &phdr32.p_vaddr,   Elf32_Addr);
			unencode(p_paddr,   &phdr32.p_paddr,   Elf32_Addr);
			unencode(p_filesz,  &phdr32.p_filesz,  Elf32_Word);
			unencode(p_memsz,   &phdr32.p_memsz,   Elf32_Word);
			unencode(p_flags,   &phdr32.p_flags,   Elf32_Word);
			unencode(p_align,   &phdr32.p_align,   Elf32_Word);
		}

		if (p_type == PT_LOAD || (p_type & PF_MASKPROC) == PT_MIPS_REGINFO) {
			debug("chunk %i (", i);
			if (p_type == PT_LOAD)
				debug("loadable");
			else
				debug("type 0x%08x", (int)p_type);

			debug(") @ 0x%llx, vaddr 0x", (long long)p_offset);

			if (elf64)
				debug("%016llx", (long long)p_vaddr);
			else
				debug("%08x", (int)p_vaddr);

			debug(" len=0x%llx\n", (long long)p_memsz);

			if (p_vaddr != p_paddr) {
				fprintf(stderr, "%s: vaddr != paddr. TODO: how to handle this? "
				    "vaddr=%016llx paddr=%016llx\n", filename, (long long)p_vaddr,
				    (long long)p_paddr);
				exit(1);
			}

			if (p_memsz < p_filesz) {
				fprintf(stderr, "%s: memsz < filesz. TODO: how to handle this? "
				    "memsz=%016llx filesz=%016llx\n", filename, (long long)p_memsz,
				    (long long)p_filesz);
				exit(1);
			}

			fseek(f, p_offset, SEEK_SET);
			align_len = 1;
			if ((p_vaddr & 0xf)==0)		align_len = 0x10;
			if ((p_vaddr & 0x3f)==0)	align_len = 0x40;
			if ((p_vaddr & 0xff)==0)	align_len = 0x100;
			if ((p_vaddr & 0xfff)==0)	align_len = 0x1000;
			ofs = 0;  len = chunk_len = align_len;
			while (ofs < (int64_t)p_filesz && len==chunk_len) {
				unsigned char *ch = malloc(chunk_len);
				int i = 0;

				if (ch == NULL) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}

				len = fread(&ch[0], 1, chunk_len, f);
				if (ofs + len > (int64_t)p_filesz)
					len = p_filesz - ofs;

				while (i < len) {
					size_t len_to_copy;
					len_to_copy = (i + align_len) <= len? align_len : len - i;
					memory_rw(cpu, mem, p_vaddr + ofs, &ch[i],
					    len_to_copy, MEM_WRITE, NO_EXCEPTIONS);
					ofs += align_len;
					i += align_len;
				}

				free(ch);
			}
		}
	}

	/*  Read the section headers to find the address of the _gp symbol:  */

	for (i=0; i<eshnum; i++) {
		int sh_name, sh_type, sh_flags, sh_link, sh_info, sh_entsize;
		uint64_t sh_addr, sh_size, sh_addralign;
		off_t sh_offset;
		int n_entries;		/*  for reading the symbol / string tables  */

		/*  debug("section header %i at %016llx\n", i,
		    (long long) eshoff+i*eshentsize);  */

		fseek(f, eshoff + i * eshentsize, SEEK_SET);

		if (elf64) {
			len = fread(&shdr64, 1, sizeof(Elf64_Shdr), f);
			if (len != sizeof(Elf64_Shdr)) {
				fprintf(stderr, "couldn't read header\n");
				exit(1);
			}
			unencode(sh_name,      &shdr64.sh_name,      Elf64_Half);
			unencode(sh_type,      &shdr64.sh_type,      Elf64_Half);
			unencode(sh_flags,     &shdr64.sh_flags,     Elf64_Xword);
			unencode(sh_addr,      &shdr64.sh_addr,      Elf64_Addr);
			unencode(sh_offset,    &shdr64.sh_offset,    Elf64_Off);
			unencode(sh_size,      &shdr64.sh_size,      Elf64_Xword);
			unencode(sh_link,      &shdr64.sh_link,      Elf64_Half);
			unencode(sh_info,      &shdr64.sh_info,      Elf64_Half);
			unencode(sh_addralign, &shdr64.sh_addralign, Elf64_Xword);
			unencode(sh_entsize,   &shdr64.sh_entsize,   Elf64_Xword);
		} else {
			len = fread(&shdr32, 1, sizeof(Elf32_Shdr), f);
			if (len != sizeof(Elf32_Shdr)) {
				fprintf(stderr, "couldn't read header\n");
				exit(1);
			}
			unencode(sh_name,      &shdr32.sh_name,      Elf32_Word);
			unencode(sh_type,      &shdr32.sh_type,      Elf32_Word);
			unencode(sh_flags,     &shdr32.sh_flags,     Elf32_Word);
			unencode(sh_addr,      &shdr32.sh_addr,      Elf32_Addr);
			unencode(sh_offset,    &shdr32.sh_offset,    Elf32_Off);
			unencode(sh_size,      &shdr32.sh_size,      Elf32_Word);
			unencode(sh_link,      &shdr32.sh_link,      Elf32_Word);
			unencode(sh_info,      &shdr32.sh_info,      Elf32_Word);
			unencode(sh_addralign, &shdr32.sh_addralign, Elf32_Word);
			unencode(sh_entsize,   &shdr32.sh_entsize,   Elf32_Word);
		}

		/*  debug("sh_name=%04lx, sh_type=%08lx, sh_flags=%08lx"
		    " sh_size=%06lx sh_entsize=%03lx\n",
		    (long)sh_name, (long)sh_type, (long)sh_flags,
		    (long)sh_size, (long)sh_entsize);  */

		/*  Perhaps it is bad to reuse sh_entsize like this?  TODO  */
		if (elf64)
			sh_entsize = sizeof(Elf64_Sym);
		else
			sh_entsize = sizeof(Elf32_Sym);

		if (sh_type == SHT_SYMTAB) {
			size_t len;
			n_entries = sh_size / sh_entsize;

			fseek(f, sh_offset, SEEK_SET);

			if (elf64) {
				if (symbols_sym64 != NULL)
					free(symbols_sym64);
				symbols_sym64 = malloc(sh_size);
				if (symbols_sym64 == NULL) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}

				len = fread(symbols_sym64, 1, sh_entsize * n_entries, f);
			} else {
				if (symbols_sym32 != NULL)
					free(symbols_sym32);
				symbols_sym32 = malloc(sh_size);
				if (symbols_sym32 == NULL) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}

				len = fread(symbols_sym32, 1, sh_entsize * n_entries, f);
			}

			if (len != sh_size) {
				fprintf(stderr, "could not read symbols from %s\n", filename);
				exit(1);
			}

			debug("%i symbol entries at 0x%llx\n",
			    (int)n_entries, (long long)sh_offset);

			n_symbols = n_entries;
		}

		/*
		 *  TODO:  This is incorrect, there may be several strtab sections.
		 *
		 *  For now, the simple/stupid guess that the largest string table
		 *  is the one to use seems to be good enough.
		 */

		if (sh_type == SHT_STRTAB && sh_size > symbol_length) {
			size_t len;

			if (symbol_strings != NULL)
				free(symbol_strings);

			symbol_strings = malloc(sh_size + 1);
			if (symbol_strings == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}

			fseek(f, sh_offset, SEEK_SET);
			len = fread(symbol_strings, 1, sh_size, f);
			if (len != sh_size) {
				fprintf(stderr, "could not read symbols from %s\n", filename);
				exit(1);
			}

			debug("%i bytes of symbol strings at 0x%llx\n",
			    (int)sh_size, (long long)sh_offset);

			symbol_strings[sh_size] = '\0';
			symbol_length = sh_size;
		}
	}

	fclose(f);

	/*  Decode symbols:  */
	if (symbol_strings != NULL) {
		for (i=0; i<n_symbols; i++) {
			uint64_t st_name, addr, size;
			int st_info;

			if (elf64) {
				sym64 = symbols_sym64[i];
				unencode(st_name, &sym64.st_name,  Elf64_Half);
				unencode(st_info, &sym64.st_info,  Elf_Byte);
				unencode(addr,    &sym64.st_value, Elf64_Addr);
				unencode(size,    &sym64.st_size,  Elf64_Xword);
			} else {
				sym32 = symbols_sym32[i];
				unencode(st_name, &sym32.st_name,  Elf32_Word);
				unencode(st_info, &sym64.st_info,  Elf_Byte);
				unencode(addr,    &sym32.st_value, Elf32_Word);
				unencode(size,    &sym32.st_size, Elf32_Word);
			}

			if (size == 0)
				size ++;

			if (addr != 0)
				add_symbol_name(&m->symbol_context,
				    addr, size, symbol_strings + st_name, 0);

			if (strcmp(symbol_strings + st_name, "_gp") == 0) {
				debug("found _gp address: 0x");
				if (elf64)
					debug("%016llx\n", (long long)addr);
				else
					debug("%08x\n", (int)addr);
				cpu->gpr[GPR_GP] = addr;
			}
		}
	}

	cpu->pc = eentry;

	if (encoding == ELFDATA2LSB)
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
	else
		cpu->byte_order = EMUL_BIG_ENDIAN;

	n_executables_loaded ++;
}


/*
 *  file_n_executables_loaded():
 *
 *  Returns the number of executable files loaded into emulated memory.
 */
int file_n_executables_loaded(void)
{
	return n_executables_loaded;
}


/*
 *  file_load():
 *
 *  Sense the file format of a file (ELF, a.out, ecoff), and call the
 *  right file_load_XXX() function.  If the file isn't of a recognized
 *  binary format, assume that it contains symbol definitions.
 *
 *  If the filename doesn't exist, try to treat the name as
 *   "address:filename" and load the file as a raw binary.
 */
void file_load(struct memory *mem, char *filename, struct cpu *cpu)
{
	int iadd = 4;
	FILE *f;
	unsigned char minibuf[12];
	int len, i;
	off_t size;

	if (mem == NULL || filename == NULL) {
		fprintf(stderr, "file_load(): mem or filename is NULL\n");
		exit(1);
	}

	debug("loading %s:\n", filename);
	debug_indentation(iadd);

	f = fopen(filename, "r");
	if (f == NULL) {
		file_load_raw(mem, filename, cpu);
		goto ret;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size > 24000000) {
		fprintf(stderr, "\nThis file is very large (%lli bytes)\n",
		    (long long)size);
		fprintf(stderr, "Are you sure it is a kernel and not a disk image?\n");
		exit(1);
	}

	memset(minibuf, 0, sizeof(minibuf));
	len = fread(minibuf, 1, sizeof(minibuf), f);
	fclose(f);

	if (len < (signed int)sizeof(minibuf)) {
		fprintf(stderr, "\nThis file is too small to contain anything useful\n");
		exit(1);
	}

	/*  Is it an ELF?  */
	if (minibuf[1]=='E' && minibuf[2]=='L' && minibuf[3]=='F') {
		file_load_elf(cpu->machine, mem, filename, cpu);
		goto ret;
	}

	/*  Is it an a.out?  (Special case for DEC OSF1 kernels.)  */
	if (minibuf[0]==0x00 && minibuf[1]==0x8b && minibuf[2]==0x01 && minibuf[3]==0x07) {
		file_load_aout(cpu->machine, mem, filename, cpu, 0);
		goto ret;
	}
	if (minibuf[0]==0x00 && minibuf[2]==0x00 && minibuf[8]==0x7a && minibuf[9]==0x75) {
		file_load_aout(cpu->machine, mem, filename, cpu, 1);
		goto ret;
	}

	/*
	 *  Is it an ecoff?
	 *
	 *  TODO: What's the deal with the magic value's byte order? Sometimes
	 *  it seems to be reversed for BE when compared to LE, but not always?
	 */
	if (minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEB ||
	    minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEL ||
	    minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEB2 ||
	    minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEL2 ||
	    minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEB3 ||
	    minibuf[0]+256*minibuf[1] == ECOFF_MAGIC_MIPSEL3 ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEB ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEL ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEB2 ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEL2 ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEB3 ||
	    minibuf[1]+256*minibuf[0] == ECOFF_MAGIC_MIPSEL3) {
		file_load_ecoff(cpu->machine, mem, filename, cpu);
		goto ret;
	}

	/*  Is it a Motorola SREC file?  */
	if ((minibuf[0]=='S' && minibuf[1]>='0' && minibuf[1]<='9')) {
		file_load_srec(mem, filename, cpu);
		goto ret;
	}

	/*
	 *  Last resort:  symbol definitions from nm (or nm -S):
	 *
	 *  If the minibuf contains typical 'binary' characters, then print
	 *  an error message and quit instead of assuming that it is a
	 *  symbol file.
	 */
	for (i=0; i<(signed)sizeof(minibuf); i++)
		if (minibuf[i] < 32)
			if (minibuf[i] != '\t' && minibuf[i] != '\n' &&
			    minibuf[i] != '\r' && minibuf[i] != '\f') {
				fprintf(stderr, "\nThe file format of '%s' is unknown.\n", filename);
				for (i=0; i<(signed)sizeof(minibuf); i++)
					fprintf(stderr, " %02x", minibuf[i]);
				fprintf(stderr, "\n");
				fprintf(stderr, "Possible explanations:\n\n");
				fprintf(stderr, "  o)  If this is a disk image, you forgot '-d' on the command line.\n");
				fprintf(stderr, "  o)  This is an unsupported binary format.\n");
				exit(1);
			}

	symbol_readfile(&cpu->machine->symbol_context, filename);

ret:
	debug_indentation(-iadd);
}

