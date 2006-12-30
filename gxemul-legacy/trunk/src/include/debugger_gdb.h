#ifndef	DEBUGGER_GDB_H
#define	DEBUGGER_GDB_H

/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: debugger_gdb.h,v 1.5 2006-12-30 13:31:00 debug Exp $
 *
 *  See src/debugger/debugger_gdb.c.
 */

struct machine;

struct debugger_gdb {
	int		port;
	int		socket;

	int		rx_state;	/*  current receive state  */

	unsigned char	*rx_buf;
	unsigned char	rx_buf_checksum;
	unsigned char	rx_checksum1;
	size_t		rx_buf_size;
	size_t		rx_buf_curlen;
};


/*  Receive states:  */
#define	RXSTATE_WAITING_FOR_DOLLAR	0
#define	RXSTATE_WAITING_FOR_HASH	1
#define	RXSTATE_WAITING_FOR_CHECKSUM1	2
#define	RXSTATE_WAITING_FOR_CHECKSUM2	3


/*  debugger_gdb.c:  */
void debugger_gdb_init(struct machine *machine);
void debugger_gdb_check_incoming(struct machine *machine);
void debugger_gdb_after_singlestep(struct machine *machine);


#endif	/*  DEBUGGER_GDB_H  */
