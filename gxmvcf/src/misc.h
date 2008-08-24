#ifndef	MISC_H
#define	MISC_H

/*
 *  Copyright (C) 2003-2008  Anders Gavare.  All rights reserved.
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
 *  Misc. definitions for GXmvcf.
 */


// config.h contains #defines set by the configure script.
#include "../config.h"

#include <iostream>

#ifndef NDEBUG
#include "thirdparty/debug_new.h"
#endif


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


#endif	/*  MISC_H  */
