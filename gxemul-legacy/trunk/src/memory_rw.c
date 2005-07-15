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
 *  $Id: memory_rw.c,v 1.46 2005-07-15 09:36:35 debug Exp $
 *
 *  Generic memory_rw(), with special hacks for specific CPU families.
 *
 *  Example for inclusion from memory_mips.c:
 *
 *	MEMORY_RW should be mips_memory_rw
 *	MEM_MIPS should be defined
 */


/*
 *  memory_rw():
 *
 *  Read or write data from/to memory.
 *
 *	cpu		the cpu doing the read/write
 *	mem		the memory object to use
 *	vaddr		the virtual address
 *	data		a pointer to the data to be written to memory, or
 *			a placeholder for data when reading from memory
 *	len		the length of the 'data' buffer
 *	writeflag	set to MEM_READ or MEM_WRITE
 *	cache_flags	CACHE_{NONE,DATA,INSTRUCTION} | other flags
 *
 *  If the address indicates access to a memory mapped device, that device'
 *  read/write access function is called.
 *
 *  If instruction latency/delay support is enabled, then
 *  cpu->instruction_delay is increased by the number of instruction to
 *  delay execution.
 *
 *  This function should not be called with cpu == NULL.
 *
 *  Returns one of the following:
 *	MEMORY_ACCESS_FAILED
 *	MEMORY_ACCESS_OK
 *
 *  (MEMORY_ACCESS_FAILED is 0.)
 */
int MEMORY_RW(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags)
{
#ifdef MEM_ALPHA
	const int offset_mask = 0x1fff;
#else
	const int offset_mask = 0xfff;
#endif

#ifndef MEM_USERLAND
	int ok = 1;
#endif
	uint64_t paddr;
	int cache, no_exceptions, offset;
	unsigned char *memblock;
#ifdef MEM_MIPS
	int bintrans_cached = cpu->machine->bintrans_enable;
#endif
	int bintrans_device_danger = 0;
	no_exceptions = cache_flags & NO_EXCEPTIONS;
	cache = cache_flags & CACHE_FLAGS_MASK;

#ifdef MEM_PPC
	if (cpu->cd.ppc.bits == 32)
		vaddr &= 0xffffffff;
#endif

#ifdef MEM_ALPHA
	vaddr &= 0x1fffffff;
#endif

#ifdef MEM_ARM
	vaddr &= 0x3fffffff;
#endif

#ifdef MEM_X86
	/*  Real-mode wrap-around:  */
	if (REAL_MODE && !(cache_flags & PHYSICAL)) {
		if ((vaddr & 0xffff) + len > 0x10000) {
			/*  Do one byte at a time:  */
			int res = 0, i;
			for (i=0; i<len; i++)
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    writeflag, cache_flags);
			return res;
		}
	}

	/*  Crossing a page boundary? Then do one byte at a time:  */
	if ((vaddr & 0xfff) + len > 0x1000 && !(cache_flags & PHYSICAL)
	    && cpu->cd.x86.cr[0] & X86_CR0_PG) {
		/*  For WRITES: Read ALL BYTES FIRST and write them back!!!
		    Then do a write of all the new bytes. This is to make sure
		    than both pages around the boundary are writable so we don't
		    do a partial write.  */
		int res = 0, i;
		if (writeflag == MEM_WRITE) {
			unsigned char tmp;
			for (i=0; i<len; i++) {
				res = MEMORY_RW(cpu, mem, vaddr+i, &tmp, 1,
				    MEM_READ, cache_flags);
				if (!res)
					return 0;
				res = MEMORY_RW(cpu, mem, vaddr+i, &tmp, 1,
				    MEM_WRITE, cache_flags);
				if (!res)
					return 0;
			}
			for (i=0; i<len; i++) {
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    MEM_WRITE, cache_flags);
				if (!res)
					return 0;
			}
		} else {
			for (i=0; i<len; i++) {
				/*  Do one byte at a time:  */
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    writeflag, cache_flags);
				if (!res) {
					if (cache == CACHE_INSTRUCTION) {
						fatal("FAILED instruction "
						    "fetch across page boundar"
						    "y: todo. vaddr=0x%08x\n",
						    (int)vaddr);
						cpu->running = 0;
					}
					return 0;
				}
			}
		}
		return res;
	}
#endif	/*  X86  */

#ifdef MEM_URISC
	{
		uint64_t mask = (uint64_t) -1;
		if (cpu->cd.urisc.wordlen < 64)
			mask = ((int64_t)1 << cpu->cd.urisc.wordlen) - 1;
		vaddr &= mask;
	}
#endif

#ifdef MEM_MIPS
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->cd.mips.pc_bintrans_host_4kpage = NULL;
			cpu->cd.mips.pc_bintrans_paddr_valid = 0;
		}
	}
#endif	/*  MEM_MIPS  */

#ifdef MEM_USERLAND
	paddr = vaddr & 0x7fffffff;
	goto have_paddr;
#endif

#ifndef MEM_USERLAND
#ifdef MEM_MIPS
	/*
	 *  For instruction fetch, are we on the same page as the last
	 *  instruction we fetched?
	 *
	 *  NOTE: There's no need to check this stuff here if this address
	 *  is known to be in host ram, as it's done at instruction fetch
	 *  time in cpu.c!  Only check if _host_4k_page == NULL.
	 */
	if (cache == CACHE_INSTRUCTION &&
	    cpu->cd.mips.pc_last_host_4k_page == NULL &&
	    (vaddr & ~0xfff) == cpu->cd.mips.pc_last_virtual_page) {
		paddr = cpu->cd.mips.pc_last_physical_page | (vaddr & 0xfff);
		goto have_paddr;
	}
#endif	/*  MEM_MIPS  */

	if (cache_flags & PHYSICAL || cpu->translate_address == NULL) {
		paddr = vaddr;
	} else {
		ok = cpu->translate_address(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) +
		    (no_exceptions? FLAG_NOEXCEPTIONS : 0)
#ifdef MEM_X86
		    + (cache_flags & NO_SEGMENTATION)
#endif
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));
		/*  If the translation caused an exception, or was invalid in
		    some way, we simply return without doing the memory
		    access:  */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}


#ifdef MEM_X86
	/*  DOS debugging :-)  */
	if (!quiet_mode && !(cache_flags & PHYSICAL)) {
		if (paddr >= 0x400 && paddr <= 0x4ff)
			debug("{ PC BIOS DATA AREA: %s 0x%x }\n", writeflag ==
			    MEM_WRITE? "writing to" : "reading from",
			    (int)paddr);
#if 0
		if (paddr >= 0xf0000 && paddr <= 0xfffff)
			debug("{ BIOS ACCESS: %s 0x%x }\n",
			    writeflag == MEM_WRITE? "writing to" :
			    "reading from", (int)paddr);
#endif
	}
#endif

#ifdef MEM_MIPS
	/*
	 *  If correct cache emulation is enabled, and we need to simluate
	 *  cache misses even from the instruction cache, we can't run directly
	 *  from a host page. :-/
	 */
#if defined(ENABLE_CACHE_EMULATION) && defined(ENABLE_INSTRUCTION_DELAYS)
#else
	if (cache == CACHE_INSTRUCTION) {
		cpu->cd.mips.pc_last_virtual_page = vaddr & ~0xfff;
		cpu->cd.mips.pc_last_physical_page = paddr & ~0xfff;
		cpu->cd.mips.pc_last_host_4k_page = NULL;

		/*  _last_host_4k_page will be set to 1 further down,
		    if the page is actually in host ram  */
	}
#endif
#endif	/*  MEM_MIPS  */
#endif	/*  ifndef MEM_USERLAND  */


#if defined(MEM_MIPS) || defined(MEM_USERLAND)
have_paddr:
#endif


#ifdef MEM_MIPS
	/*  TODO: How about bintrans vs cache emulation?  */
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->cd.mips.pc_bintrans_paddr_valid = 1;
			cpu->cd.mips.pc_bintrans_paddr = paddr;
		}
	}
#endif	/*  MEM_MIPS  */



#ifndef MEM_USERLAND
	/*
	 *  Memory mapped device?
	 *
	 *  TODO: this is utterly slow.
	 *  TODO2: if paddr<base, but len enough, then we should write
	 *  to a device to
	 */
	if (paddr >= mem->mmap_dev_minaddr && paddr < mem->mmap_dev_maxaddr) {
		uint64_t orig_paddr = paddr;
		int i, start, res;

		/*
		 *  Really really slow, but unfortunately necessary. This is
		 *  to avoid the folowing scenario:
		 *
		 *	a) offsets 0x000..0x123 are normal memory
		 *	b) offsets 0x124..0x777 are a device
		 *
		 *	1) a read is done from offset 0x100. the page is
		 *	   added to the bintrans system as a "RAM" page
		 *	2) a bintranslated read is done from offset 0x200,
		 *	   which should access the device, but since the
		 *	   entire page is added, it will access non-existant
		 *	   RAM instead, without warning.
		 *
		 *  Setting bintrans_device_danger = 1 on accesses which are
		 *  on _any_ offset on pages that are device mapped avoids
		 *  this problem, but it is probably not very fast.
		 */
		for (i=0; i<mem->n_mmapped_devices; i++)
			if (paddr >= (mem->dev_baseaddr[i] & ~offset_mask) &&
			    paddr <= ((mem->dev_baseaddr[i] +
			    mem->dev_length[i] - 1) | offset_mask)) {
				bintrans_device_danger = 1;
				break;
			}

		i = start = mem->last_accessed_device;

		/*  Scan through all devices:  */
		do {
			if (paddr >= mem->dev_baseaddr[i] &&
			    paddr < mem->dev_baseaddr[i] + mem->dev_length[i]) {
				/*  Found a device, let's access it:  */
				mem->last_accessed_device = i;

				paddr -= mem->dev_baseaddr[i];
				if (paddr + len > mem->dev_length[i])
					len = mem->dev_length[i] - paddr;

				if (cpu->update_translation_table != NULL &&
				    mem->dev_flags[i] & MEM_DYNTRANS_OK) {
					int wf = writeflag == MEM_WRITE? 1 : 0;

					if (writeflag) {
						if (paddr < mem->
						    dev_bintrans_write_low[i])
							mem->
							dev_bintrans_write_low
							    [i] = paddr &
							    ~offset_mask;
						if (paddr > mem->
						    dev_bintrans_write_high[i])
							mem->
						 	dev_bintrans_write_high
							    [i] = paddr |
							    offset_mask;
					}

					if (!(mem->dev_flags[i] &
					    MEM_DYNTRANS_WRITE_OK))
						wf = 0;

					cpu->update_translation_table(cpu,
					    vaddr & ~offset_mask,
					    mem->dev_bintrans_data[i] +
					    (paddr & ~offset_mask),
					    wf, orig_paddr & ~offset_mask);
				}

				res = 0;
				if (!no_exceptions || (mem->dev_flags[i] &
				    MEM_READING_HAS_NO_SIDE_EFFECTS))
					res = mem->dev_f[i](cpu, mem, paddr,
					    data, len, writeflag,
					    mem->dev_extra[i]);

#ifdef ENABLE_INSTRUCTION_DELAYS
				if (res == 0)
					res = -1;

				cpu->cd.mips.instruction_delay +=
				    ( (abs(res) - 1) *
				     cpu->cd.mips.cpu_type.instrs_per_cycle );
#endif

#ifndef MEM_X86
				/*
				 *  If accessing the memory mapped device
				 *  failed, then return with a DBE exception.
				 */
				if (res <= 0 && !no_exceptions) {
					debug("%s device '%s' addr %08lx "
					    "failed\n", writeflag?
					    "writing to" : "reading from",
					    mem->dev_name[i], (long)paddr);
#ifdef MEM_MIPS
					mips_cpu_exception(cpu, EXCEPTION_DBE,
					    0, vaddr, 0, 0, 0, 0);
#endif
					return MEMORY_ACCESS_FAILED;
				}
#endif
				goto do_return_ok;
			}

			i ++;
			if (i == mem->n_mmapped_devices)
				i = 0;
		} while (i != start);
	}


#ifdef MEM_MIPS
	/*
	 *  Data and instruction cache emulation:
	 */

	switch (cpu->cd.mips.cpu_type.mmu_model) {
	case MMU3K:
		/*  if not uncached addess  (TODO: generalize this)  */
		if (!(cache_flags & PHYSICAL) && cache != CACHE_NONE &&
		    !((vaddr & 0xffffffffULL) >= 0xa0000000ULL &&
		      (vaddr & 0xffffffffULL) <= 0xbfffffffULL)) {
			if (memory_cache_R3000(cpu, cache, paddr,
			    writeflag, len, data))
				goto do_return_ok;
		}
		break;
#if 0
/*  Remove this, it doesn't work anyway  */
	case MMU10K:
		/*  other cpus:  */
		/*
		 *  SUPER-UGLY HACK for SGI-IP32 PROM, R10000:
		 *  K0 bits == 0x3 means uncached...
		 *
		 *  It seems that during bootup, the SGI-IP32 prom
		 *  stores a return pointers a 0x80000f10, then tests
		 *  memory by writing bit patterns to 0xa0000xxx, and
		 *  then when it's done, reads back the return pointer
		 *  from 0x80000f10.
		 *
		 *  I need to find the correct way to disconnect the
		 *  cache from the main memory for R10000.  (TODO !!!)
		 */
/*		if ((cpu->cd.mips.coproc[0]->reg[COP0_CONFIG] & 7) == 3) {  */
/*
		if (cache == CACHE_DATA &&
		    cpu->r10k_cache_disable_TODO) {
			paddr &= ((512*1024)-1);
			paddr += 512*1024;
		}
*/
		break;
#endif
	default:
		/*  R4000 etc  */
		/*  TODO  */
		;
	}
#endif	/*  MEM_MIPS  */


	/*  Outside of physical RAM?  */
	if (paddr >= mem->physical_max) {
#ifdef MEM_MIPS
		if ((paddr & 0xffffc00000ULL) == 0x1fc00000) {
			/*  Ok, this is PROM stuff  */
		} else if ((paddr & 0xfffff00000ULL) == 0x1ff00000) {
			/*  Sprite reads from this area of memory...  */
			/*  TODO: is this still correct?  */
			if (writeflag == MEM_READ)
				memset(data, 0, len);
			goto do_return_ok;
		} else
#endif /* MIPS */
		{
			if (paddr >= mem->physical_max) {
				char *symbol;
				uint64_t old_pc;
				uint64_t offset;

#ifdef MEM_MIPS
				old_pc = cpu->cd.mips.pc_last;
#else
				/*  Default instruction size on most
				    RISC archs is 32 bits:  */
				old_pc = cpu->pc - sizeof(uint32_t);
#endif

				/*  This allows for example OS kernels to probe
				    memory a few KBs past the end of memory,
				    without giving too many warnings.  */
				if (!quiet_mode && paddr >=
				    mem->physical_max + 0x40000) {
					fatal("[ memory_rw(): writeflag=%i ",
					    writeflag);
					if (writeflag) {
						unsigned int i;
						debug("data={", writeflag);
						if (len > 16) {
							int start2 = len-16;
							for (i=0; i<16; i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
							debug(" .. ");
							if (start2 < 16)
								start2 = 16;
							for (i=start2; i<len;
							    i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
						} else
							for (i=0; i<len; i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
						debug("}");
					}

					fatal(" paddr=0x%llx >= physical_max"
					    "; pc=", (long long)paddr);
					if (cpu->is_32bit)
						fatal("0x%08x",(int)old_pc);
					else
						fatal("0x%016llx",
						    (long long)old_pc);
					symbol = get_symbol_name(
					    &cpu->machine->symbol_context,
					    old_pc, &offset);
					fatal(" <%s> ]\n",
					    symbol? symbol : " no symbol ");
				}

				if (cpu->machine->single_step_on_bad_addr) {
					fatal("[ unimplemented access to "
					    "0x%llx, pc=0x",(long long)paddr);
					if (cpu->is_32bit)
						fatal("%08x ]\n",
						    (int)old_pc);
					else
						fatal("%016llx ]\n",
						    (long long)old_pc);
					single_step = 1;
				}
			}

			if (writeflag == MEM_READ) {
#ifdef MEM_X86
				/*  Reading non-existant memory on x86:  */
				memset(data, 0xff, len);
#else
				/*  Return all zeroes? (Or 0xff? TODO)  */
				memset(data, 0, len);
#endif

#ifdef MEM_MIPS
				/*
				 *  For real data/instruction accesses, cause
				 *  an exceptions on an illegal read:
				 */
				if (cache != CACHE_NONE && cpu->machine->
				    dbe_on_nonexistant_memaccess &&
				    !no_exceptions) {
					if (paddr >= mem->physical_max &&
					    paddr < mem->physical_max+1048576)
						mips_cpu_exception(cpu,
						    EXCEPTION_DBE, 0, vaddr, 0,
						    0, 0, 0);
				}
#endif  /*  MEM_MIPS  */
			}

			/*  Hm? Shouldn't there be a DBE exception for
			    invalid writes as well?  TODO  */

			goto do_return_ok;
		}
	}

#endif	/*  ifndef MEM_USERLAND  */


	/*
	 *  Uncached access:
	 */
	memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
	if (memblock == NULL) {
		if (writeflag == MEM_READ)
			memset(data, 0, len);
		goto do_return_ok;
	}

	offset = paddr & ((1 << BITS_PER_MEMBLOCK) - 1);

	if (cpu->update_translation_table != NULL && !bintrans_device_danger)
		cpu->update_translation_table(cpu, vaddr & ~offset_mask,
		    memblock + (offset & ~offset_mask),
#if 0
		    cache == CACHE_INSTRUCTION?
			(writeflag == MEM_WRITE? 1 : 0)
			: ok - 1,
#else
		    writeflag == MEM_WRITE? 1 : 0,
#endif
		    paddr & ~offset_mask);

	if (writeflag == MEM_WRITE) {
		if (len == sizeof(uint32_t) && (offset & 3)==0)
			*(uint32_t *)(memblock + offset) = *(uint32_t *)data;
		else if (len == sizeof(uint8_t))
			*(uint8_t *)(memblock + offset) = *(uint8_t *)data;
		else
			memcpy(memblock + offset, data, len);
	} else {
		if (len == sizeof(uint32_t) && (offset & 3)==0)
			*(uint32_t *)data = *(uint32_t *)(memblock + offset);
		else if (len == sizeof(uint8_t))
			*(uint8_t *)data = *(uint8_t *)(memblock + offset);
		else
			memcpy(data, memblock + offset, len);

#ifdef MEM_MIPS
		if (cache == CACHE_INSTRUCTION) {
			cpu->cd.mips.pc_last_host_4k_page = memblock
			    + (offset & ~offset_mask);
			if (bintrans_cached) {
				cpu->cd.mips.pc_bintrans_host_4kpage =
				    cpu->cd.mips.pc_last_host_4k_page;
			}
		}
#endif	/*  MIPS  */
	}


do_return_ok:
	return MEMORY_ACCESS_OK;
}

