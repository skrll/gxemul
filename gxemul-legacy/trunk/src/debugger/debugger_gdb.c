/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: debugger_gdb.c,v 1.4 2006-03-30 19:36:04 debug Exp $
 *
 *  Routines used for communicating with the GNU debugger, using the GDB
 *  remote serial protocol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

#include "debugger_gdb.h"
#include "machine.h"
#include "memory.h"


/*
 *  debugger_gdb_listen():
 *
 *  Set up a GDB remote listening port for a specific emulated machine.
 */
static void debugger_gdb_listen(struct machine *machine)
{
	int listen_socket, res;
	struct sockaddr_in si;
	struct sockaddr_in incoming;
	socklen_t incoming_len;

	printf("----------------------------------------------------------"
	    "---------------------\nWaiting for incoming remote GDB connection"
	    " on port %i...\n", machine->gdb.port);

	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket < 0) {
		perror("socket");
		exit(1);
	}

	memset((char *)&si, sizeof(si), 0);
	si.sin_family = AF_INET;
	si.sin_port = htons(machine->gdb.port);
	si.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listen_socket, (struct sockaddr *)&si, sizeof(si)) < 0) {
		perror("bind");
		exit(1);
	}

	if (listen(listen_socket, 1) != 0) {
		perror("listen");
	}

	machine->gdb.socket = accept(listen_socket,
	    (struct sockaddr *)&incoming, &incoming_len);
	printf("Connected; GDB socket = %i\n", machine->gdb.socket);

	/*  Set the socket to non-blocking:  */
	res = fcntl(machine->gdb.socket, F_GETFL);
	fcntl(machine->gdb.socket, F_SETFL, res | O_NONBLOCK);
}


static void send_response(struct machine *machine, unsigned char *msg)
{
	unsigned char hex[16] = "0123456789abcdef";
	unsigned char checksum = 0x00;
	int i = 0;
	unsigned char ch;

	while (msg[i]) {
		checksum += msg[i];
		i ++;
	}

	ch = '$'; write(machine->gdb.socket, &ch, 1);
	write(machine->gdb.socket, msg, i);
	ch = '#'; write(machine->gdb.socket, &ch, 1);
	ch = hex[checksum >> 4]; write(machine->gdb.socket, &ch, 1);
	ch = hex[checksum & 15]; write(machine->gdb.socket, &ch, 1);
}


/*
 *  debugger_gdb__execute_command():
 *
 *  Execute the command in the machine's receive buffer.
 */
void debugger_gdb__execute_command(struct machine *machine)
{
	char *cmd = machine->gdb.rx_buf;

	fatal("[ EXECUTE: '%s' ]\n", machine->gdb.rx_buf);

	if (strcmp(cmd, "?") == 0) {
		send_response(machine, "x00");
	} else if (strncmp(cmd, "Hc", 2) == 0) {
		fatal("[ TODO: GDB SET THREAD ]\n");
		send_response(machine, "OK");
	} else {
		fatal("[ (UNKNOWN COMMAND) ]\n");
		send_response(machine, "");
	}
}


/*
 *  debugger_gdb__check_incoming_char():
 *
 *  Handle each incoming character.
 */
int debugger_gdb__check_incoming_char(struct machine *machine)
{
	int old_state = machine->gdb.rx_state;
	unsigned char ch, ch1;
	ssize_t len = read(machine->gdb.socket, &ch, 1);

	if (len == 0) {
		perror("GDB socket read");
		fprintf(stderr, "Connection closed. TODO\n");
		exit(1);
	}

	/*  EAGAIN, and similar:  */
	if (len < 0)
		return 0;

	fatal("[ debugger_gdb: received char ");
	if (ch >= ' ')
		fatal("'%c' ]\n", ch);
	else
		fatal("0x%02x ]\n", ch);

	switch (machine->gdb.rx_state) {

	case RXSTATE_WAITING_FOR_DOLLAR:
		if (ch == '$') {
			machine->gdb.rx_state =
			    RXSTATE_WAITING_FOR_HASH;
			if (machine->gdb.rx_buf != NULL)
				free(machine->gdb.rx_buf);
			machine->gdb.rx_buf_size = 200;
			machine->gdb.rx_buf = malloc(
			    machine->gdb.rx_buf_size + 1);
			machine->gdb.rx_buf_curlen = 0;
			machine->gdb.rx_buf_checksum = 0x00;
		} else {
			fatal("[ debugger_gdb: ignoring char '%c' ]\n", ch);
		}
		break;

	case RXSTATE_WAITING_FOR_HASH:
		if (ch == '#') {
			machine->gdb.rx_state =
			    RXSTATE_WAITING_FOR_CHECKSUM1;

			machine->gdb.rx_buf[machine->gdb.rx_buf_curlen] = '\0';
		} else {
			if (machine->gdb.rx_buf_curlen >=
			    machine->gdb.rx_buf_size) {
				machine->gdb.rx_buf_size *= 2;
				machine->gdb.rx_buf = realloc(
				    machine->gdb.rx_buf,
				    machine->gdb.rx_buf_size + 1);
			}

			machine->gdb.rx_buf[
			    machine->gdb.rx_buf_curlen ++] = ch;

			machine->gdb.rx_buf_checksum += ch;

			fatal("[ debugger_gdb: current checksum = "
			    "0x%02x ]\n", machine->gdb.rx_buf_checksum);
		}
		break;

	case RXSTATE_WAITING_FOR_CHECKSUM1:
		machine->gdb.rx_checksum1 = ch;
		machine->gdb.rx_state = RXSTATE_WAITING_FOR_CHECKSUM2;
		break;

	case RXSTATE_WAITING_FOR_CHECKSUM2:
		ch1 = machine->gdb.rx_checksum1;

		if (ch1 >= '0' && ch1 <= '9')
			ch1 = ch1 - '0';
		else if (ch1 >= 'a' && ch1 <= 'f')
			ch1 = 10 + ch1 - 'a';
		else if (ch1 >= 'A' && ch1 <= 'F')
			ch1 = 10 + ch1 - 'A';

		if (ch >= '0' && ch <= '9')
			ch = ch - '0';
		else if (ch >= 'a' && ch <= 'f')
			ch = 10 + ch - 'a';
		else if (ch >= 'A' && ch <= 'F')
			ch = 10 + ch - 'A';

		if (machine->gdb.rx_buf_checksum != ch1 * 16 + ch) {
			/*  Checksum mismatch!  */

			fatal("[ debugger_gdb: CHECKSUM MISMATCH! (0x%02x, "
			    " calculated is 0x%02x ]\n", ch1 * 16 + ch,
			    machine->gdb.rx_buf_checksum);

			/*  Send a NACK message:  */
			ch = '-';
			write(machine->gdb.socket, &ch, 1);
		} else {
			/*  Checksum is ok. Send an ACK message...  */
			ch = '+';
			write(machine->gdb.socket, &ch, 1);

			/*  ... and execute the command:  */
			debugger_gdb__execute_command(machine);
		}

		machine->gdb.rx_state = RXSTATE_WAITING_FOR_DOLLAR;
		break;

	default:fatal("debugger_gdb_check_incoming(): internal error "
		    "(state %i unknown)\n", machine->gdb.rx_state);
		exit(1);
	}

	if (machine->gdb.rx_state != old_state)
		fatal("[ debugger_gdb: state %i -> %i ]\n",
		    old_state, machine->gdb.rx_state);

	return 1;
}


/*
 *  debugger_gdb_check_incoming():
 *
 *  This function should be called regularly, to check for incoming data on
 *  the remote GDB socket.
 */
void debugger_gdb_check_incoming(struct machine *machine)
{
	while (debugger_gdb__check_incoming_char(machine))
		;
}


/*
 *  debugger_gdb_init():
 *
 *  Initialize stuff needed for a GDB remote connection.
 */
void debugger_gdb_init(struct machine *machine)
{
	if (machine->gdb.port < 1)
		return;

	debugger_gdb_listen(machine);
}
