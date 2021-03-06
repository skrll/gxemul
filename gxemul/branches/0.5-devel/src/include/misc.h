#ifndef	MISC_H
#define	MISC_H

/*
 *  Copyright (C) 2003-2009  Anders Gavare.  All rights reserved.
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
 *  Misc. definitions for GXemul.
 */


#include <sys/types.h>
#include <inttypes.h>

/*
 *  ../../config.h contains #defines set by the configure script. Some of these
 *  might reduce speed of the emulator, so don't enable them unless you
 *  need them.
 */

#include "../../config.h"

#define	COPYRIGHT_MSG	"Copyright (C) 2003-2009  Anders Gavare"

// The recommended way to add a specific message to the startup banner or
// about box is to use the SECONDARY_MSG. This should end with a newline
// character, unless it is completely empty.
//
// Example:  "Modified by XYZ to include support for machine type UVW.\n"
//
#define	SECONDARY_MSG	""


// Use Glib::ustring if available, otherwise std::string. Define
// stringchar to be the type of a character.
#ifdef WITH_GLIBMM
#include <glibmm/ustring.h>
typedef Glib::ustring string;
typedef gunichar stringchar;
#else
#include <string>
typedef std::string string;
typedef char stringchar;
#endif

// Use Glib's I18N support if available
#if defined(I18N) && defined(WITH_GLIBMM)
#include <glibmm/i18n.h>
#else
#define	_(x)	(x)
#endif

#include <iostream>
using std::ostream;

#include <memory>
using std::auto_ptr;

#include <list>
using std::list;

#include <map>
using std::map;

#include <set>
using std::set;

#include <sstream>
using std::stringstream;

#include <vector>
using std::vector;

using std::min;
using std::max;


#ifndef NDEBUG
#include "thirdparty/debug_new.h"
#endif

// Reference counting is needed in lots of places, so it is best to
// include it from this file.
#include "refcount_ptr.h"  


#ifdef NO_C99_PRINTF_DEFINES
/*
 *  This is a SUPER-UGLY HACK which happens to work on some machines.
 *  The correct solution is to upgrade your compiler to C99.
 */
#ifdef NO_C99_64BIT_LONGLONG
#define	PRIi8		"i"
#define	PRIi16		"i"
#define	PRIi32		"i"
#define	PRIi64		"lli"
#define	PRIx8		"x"
#define	PRIx16		"x"
#define	PRIx32		"x"
#define	PRIx64		"llx"
#else
#define	PRIi8		"i"
#define	PRIi16		"i"
#define	PRIi32		"i"
#define	PRIi64		"li"
#define	PRIx8		"x"
#define	PRIx16		"x"
#define	PRIx32		"x"
#define	PRIx64		"lx"
#endif
#endif


#ifdef NO_MAP_ANON
#ifdef mmap
#undef mmap
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
static void *no_map_anon_mmap(void *addr, size_t len, int prot, int flags,
	int nonsense_fd, off_t offset)
{
	void *p;
	int fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open /dev/zero\n");
		exit(1);
	}

	printf("addr=%p len=%lli prot=0x%x flags=0x%x nonsense_fd=%i "
	    "offset=%16lli\n", addr, (long long) len, prot, flags,
	    nonsense_fd, (long long) offset);

	p = mmap(addr, len, prot, flags, fd, offset);

	printf("p = %p\n", p);

	/*  TODO: Close the descriptor?  */
	return p;
}
#define mmap no_map_anon_mmap
#endif


/*  tmp dir to use if the TMPDIR environment variable isn't set:  */
#define	DEFAULT_TMP_DIR		"/tmp"


struct cpu;
struct emul;
struct machine;
struct memory;


enum Endianness
{
	BigEndian = 0,
	LittleEndian
};


#define	NO_BYTE_ORDER_OVERRIDE		-1
#define	EMUL_UNDEFINED_ENDIAN		0
#define	EMUL_LITTLE_ENDIAN		1
#define	EMUL_BIG_ENDIAN			2

#ifdef HOST_LITTLE_ENDIAN
#define	LE16_TO_HOST(x)	    (x)
#define	BE16_TO_HOST(x)	    ((((x) & 0xff00) >> 8) | (((x)&0xff) << 8))
#else
#define	LE16_TO_HOST(x)	    ((((x) & 0xff00) >> 8) | (((x)&0xff) << 8))
#define	BE16_TO_HOST(x)	    (x)
#endif

#ifdef HOST_LITTLE_ENDIAN
#define	LE32_TO_HOST(x)	    (x)
#define	BE32_TO_HOST(x)	    ((((x) & 0xff000000) >> 24) | (((x)&0xff) << 24) | \
			     (((x) & 0xff0000) >> 8) | (((x) & 0xff00) << 8))
#else
#define	LE32_TO_HOST(x)	    ((((x) & 0xff000000) >> 24) | (((x)&0xff) << 24) | \
			     (((x) & 0xff0000) >> 8) | (((x) & 0xff00) << 8))
#define	BE32_TO_HOST(x)	    (x)
#endif

#ifdef HOST_LITTLE_ENDIAN
#define	LE64_TO_HOST(x)	    (x)
#define BE64_TO_HOST(x)	    (	(((x) >> 56) & 0xff) +			\
				((((x) >> 48) & 0xff) << 8) +		\
				((((x) >> 40) & 0xff) << 16) +		\
				((((x) >> 32) & 0xff) << 24) +		\
				((((x) >> 24) & 0xff) << 32) +		\
				((((x) >> 16) & 0xff) << 40) +		\
				((((x) >> 8) & 0xff) << 48) +		\
				(((x) & 0xff) << 56)  )
#else
#define	BE64_TO_HOST(x)	    (x)
#define LE64_TO_HOST(x)	    (	(((x) >> 56) & 0xff) +			\
				((((x) >> 48) & 0xff) << 8) +		\
				((((x) >> 40) & 0xff) << 16) +		\
				((((x) >> 32) & 0xff) << 24) +		\
				((((x) >> 24) & 0xff) << 32) +		\
				((((x) >> 16) & 0xff) << 40) +		\
				((((x) >> 8) & 0xff) << 48) +		\
				(((x) & 0xff) << 56)  )
#endif


/*  Debug stuff:  */
#define	DEBUG_BUFSIZE		1024
#define	DEBUG_INDENTATION	4


#ifdef HAVE___FUNCTION__

#define	FAILURE(error_msg)					{	\
		char where_msg[400];					\
		snprintf(where_msg, sizeof(where_msg),			\
		    "%s, line %i, function %s().\n",			\
		    __FILE__, __LINE__, __FUNCTION__);			\
        	fprintf(stderr, "\n%s, in %s\n", error_msg, where_msg);	\
		exit(1);						\
	}

#else

#define	FAILURE(error_msg)					{	\
		char where_msg[400];					\
		snprintf(where_msg, sizeof(where_msg),			\
		    "%s, line %i\n", __FILE__, __LINE__);		\
        	fprintf(stderr, "\n%s, in %s.\n", error_msg, where_msg);\
		exit(1);						\
	}

#endif	/*  !HAVE___FUNCTION__  */


#define	CHECK_ALLOCATION(ptr)					{	\
		if ((ptr) == NULL)					\
			FAILURE("Out of memory");			\
	}


#endif	/*  MISC_H  */
