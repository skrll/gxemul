/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: net.c,v 1.15 2004-07-14 09:14:40 debug Exp $
 *
 *  Emulated (ethernet / internet) network support.
 *
 *
 *	NOTE:	This is just a hack, and just barely enough to get some
 *		Internet networking up and running for the guest OS.
 *
 *
 *  The emulated NIC has a MAC address of (for example) 11:22:33:44:55:66.
 *  From the emulated environment, the only other machine existing on the
 *  network is a "gateway" or "firewall", which has an address of
 *  55:44:33:22:11:00. This module (net.c) contains the emulation of that
 *  gateway. It works like a NAT firewall, but emulated in userland software.
 *
 *  The gateway uses IPv4 address 10.0.0.254, the guest OS (inside the
 *  emulator) could use any 10.x.x.x address, except 10.0.0.254. A suitable
 *  choice is, for example 10.0.0.1.
 *
 *
 *  NOTE: The 'extra' argument used in many functions in this file is a pointer
 *  to something unique for each controller, so that if multiple controllers
 *  are emulated concurrently, they will not get packets that aren't meant
 *  for some other controller.
 *
 *
 *	guestOS   <------->   gateway (in the   <------->   outside machine
 *          ("inside")          emulator)                     ("outside")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include "misc.h"
#include "net.h"


/*  #define debug fatal  */


struct ethernet_packet_link {
	struct ethernet_packet_link *prev;
	struct ethernet_packet_link *next;

	void		*extra;
	unsigned char	*data;
	int		len;
};

static struct ethernet_packet_link *first_ethernet_packet = NULL;
static struct ethernet_packet_link *last_ethernet_packet = NULL;

unsigned char gateway_addr[6] = { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
unsigned char gateway_ipv4[4] = { 10, 0, 0, 254 };


#define	MAX_TCP_CONNECTIONS	25
#define	MAX_UDP_CONNECTIONS	15

static int64_t net_timestamp = 0;

struct udp_connection {
	int		in_use;
	int64_t		last_used_timestamp;

	/*  Inside:  */
	unsigned char	ethernet_address[6];
	unsigned char	inside_ip_address[4];
	int		inside_udp_port;

	/*  Outside:  */
	int		udp_id;
	int		socket;
	unsigned char	outside_ip_address[4];
	int		outside_udp_port;
};

struct tcp_connection {
	int		in_use;

	/*  Inside:  */
	unsigned char	ethernet_address[6];
	unsigned char	inside_ip_address[4];
	int		tcp_port;

/*  TODO:  states and buffers  */

	/*  Outside:  */
	int		tcp_id;
	int		socket;
	unsigned char	outside_ip_address[4];
	int		outside_tcp_port;
};


static struct udp_connection udp_connections[MAX_UDP_CONNECTIONS];
static struct tcp_connection tcp_connections[MAX_TCP_CONNECTIONS];


/*
 *  net_ip_checksum():
 *
 *  Fill in an IP header checksum. (This works for ICMP too.)
 *  chksumoffset should be 10 for IP headers, and len = 20.
 *  For ICMP packets, chksumoffset = 2 and len = length of the ICMP packet.
 */
void net_ip_checksum(unsigned char *ip_header, int chksumoffset, int len)
{
	int i;
	uint32_t sum = 0;

	for (i=0; i<len; i+=2)
		if (i != chksumoffset) {
			uint16_t w = (ip_header[i] << 8) + ip_header[i+1];
			sum += w;
			while (sum > 65535) {
				int to_add = sum >> 16;
				sum = (sum & 0xffff) + to_add;
			}
		}

	sum ^= 0xffff;
	ip_header[chksumoffset + 0] = sum >> 8;
	ip_header[chksumoffset + 1] = sum & 0xff;
}


/*
 *  net_allocate_packet_link():
 *
 *  This routine allocates an ethernet_packet_link struct, and adds it at
 *  the end of the packet chain.  A data buffer is allocated (and zeroed),
 *  and the data, extra, and len fields of the link are set.
 *
 *  Return value is a pointer to the link on success. It doesn't return on
 *  failure.
 */
struct ethernet_packet_link *net_allocate_packet_link(void *extra, int len)
{
	struct ethernet_packet_link *lp;

	lp = malloc(sizeof(struct ethernet_packet_link));
	if (lp == NULL) {
		fprintf(stderr, "out of memory in net_allocate_packet_link()\n");
		exit(1);
	}

	memset(lp, 0, sizeof(struct ethernet_packet_link));
	lp->len = len;
	lp->extra = extra;
	lp->data = malloc(len);
	if (lp->data == NULL) {
		fprintf(stderr, "out of memory in net_allocate_packet_link()\n");
		exit(1);
	}
	memset(lp->data, 0, len);

	/*  Add last in the link chain:  */
	lp->prev = last_ethernet_packet;
	if (lp->prev != NULL)
		lp->prev->next = lp;
	else
		first_ethernet_packet = lp;
	last_ethernet_packet = lp;

	return lp;
}


/*
 *  net_ip_icmp():
 *
 *  Handle an ICMP packet.
 *
 *  The IP header (at offset 14) could look something like
 *
 *	ver=45 tos=00 len=0054 id=001a ofs=0000 ttl=ff p=01 sum=a87e
 *	src=0a000005 dst=03050607
 *
 *  and the ICMP specific data (beginning at offset 34):
 *
 *	type=08 code=00 chksum=b8bf
 *	000c0008d5cee94089190c0008090a0b
 *	0c0d0e0f101112131415161718191a1b
 *	1c1d1e1f202122232425262728292a2b
 *	2c2d2e2f3031323334353637
 */
static void net_ip_icmp(void *extra, unsigned char *packet, int len)
{
	int type;
	struct ethernet_packet_link *lp;

	type = packet[34];

	switch (type) {
	case 8:	/*  ECHO request  */
		debug("[ ICMP echo ]\n");
		lp = net_allocate_packet_link(extra, len);

		/*  Copy the old packet first:  */
		memcpy(lp->data, packet, len);

		/*  Switch to and from ethernet addresses:  */
		memcpy(lp->data + 0, packet + 6, 6);
		memcpy(lp->data + 6, packet + 0, 6);

		/*  Switch to and from IP addresses:  */
		memcpy(lp->data + 26, packet + 30, 4);
		memcpy(lp->data + 30, packet + 26, 4);

		/*  Change from echo REQUEST to echo REPLY:  */
		lp->data[34] = 0x00;

		/*  Decrease the TTL to a low value:  */
		lp->data[22] = 2;

		/*  Recalculate ICMP checksum:  */
		net_ip_checksum(lp->data + 34, 2, len - 34);

		/*  Recalculate IP header checksum:  */
		net_ip_checksum(lp->data + 14, 10, 20);

		break;
	default:
		fatal("[ net: ICMP type %i not yet implemented ]\n", type);
	}
}


/*
 *  net_ip_tcp():
 *
 *  Handle a TCP packet comming from the emulated OS.
 *
 *  The IP header (at offset 14) could look something like
 *
 *	ver=45 tos=00 len=003c id=0006 ofs=0000 ttl=40 p=11 sum=b798
 *	src=0a000001 dst=c1abcdef
 *
 *  TCP header, at offset 34:
 *
 *	srcport=fffe dstport=0015 seqnr=af419a1d acknr=00000000
 *	control=a002 window=4000 checksum=fe58 urgent=0000
 *	and then "options and padding" and then data.
 *	(020405b4010303000101080a0000000000000000)
 *
 *  See the following URLs for good descriptions of TCP:
 *
 *	http://www.networksorcery.com/enp/protocol/tcp.htm
 *	http://www.tcpipguide.com/free/t_TCPIPTransmissionControlProtocolTCP.htm
 */
static void net_ip_tcp(void *extra, unsigned char *packet, int len)
{
	int con_id, free_con_id, i, srcport, dstport, data_offset;
	int syn, ack, psh, rst, urg, fin;
	uint32_t seqnr, acknr;
	int window, checksum, urgptr;

	fatal("[ net: TCP: ");
	for (i=0; i<34; i++)
		fatal("%02x", packet[i]);
	fatal(" ");

	srcport = (packet[34] << 8) + packet[35];
	dstport = (packet[36] << 8) + packet[37];
	seqnr   = (packet[38] << 24) + (packet[39] << 16)
		+ (packet[40] << 8) + packet[41];
	acknr   = (packet[42] << 24) + (packet[43] << 16)
		+ (packet[44] << 8) + packet[45];

	fatal("srcport=%i dstport=%i seqnr=%lli acknr=%lli ",
	    srcport, dstport, (long long)seqnr, (long long)acknr);

	data_offset = (packet[46] >> 5) * 4 + 34;
	/*  data_offset is now data offset within packet :-)  */

	urg = packet[47] & 32;
	ack = packet[47] & 16;
	psh = packet[47] &  8;
	rst = packet[47] &  4;
	syn = packet[47] &  2;
	fin = packet[47] &  1;

	fatal(urg? "URG " : "");
	fatal(ack? "ACK " : "");
	fatal(psh? "PSH " : "");
	fatal(rst? "RST " : "");
	fatal(syn? "SYN " : "");
	fatal(fin? "FIN " : "");

	window   = (packet[48] << 8) + packet[49];
	checksum = (packet[50] << 8) + packet[51];
	urgptr   = (packet[52] << 8) + packet[53];

	fatal("window=0x%04x checksum=0x%04x urgptr=0x%04x ",
	    window, checksum, urgptr);

	for (i=data_offset; i < len; i++)
		fatal("%02x", packet[i]);

	fatal(" ]\n");

}


/*
 *  net_ip_udp():
 *
 *  Handle a UDP packet.
 *
 *  (See http://www.networksorcery.com/enp/protocol/udp.htm.)
 *
 *  The IP header (at offset 14) could look something like
 *
 *	ver=45 tos=00 len=003c id=0006 ofs=0000 ttl=40 p=11 sum=b798
 *	src=0a000001 dst=c1abcdef
 *
 *  and the UDP data (beginning at offset 34):
 *
 *	srcport=fffc dstport=0035 length=0028 chksum=76b6
 *	43e20100000100000000000003667470066e6574627364036f726700001c0001
 */
static void net_ip_udp(void *extra, unsigned char *packet, int len)
{
	int con_id, free_con_id, i, srcport, dstport, udp_len;
	ssize_t res;
	struct sockaddr_in remote_ip;

	if ((packet[20] & 0x3f) != 0) {
		fatal("[ net_ip_udp(): WARNING! fragmented UDP packet, TODO ]\n");
		return;
	}

	srcport = (packet[34] << 8) + packet[35];
	dstport = (packet[36] << 8) + packet[37];
	udp_len = (packet[38] << 8) + packet[39];
	/*  chksum at offset 40 and 41  */

	debug("[ net: UDP: ");
	debug("srcport=%i dstport=%i len=%i ", srcport, dstport, udp_len);
	for (i=42; i<len; i++) {
		if (packet[i] >= ' ' && packet[i] < 127)
			debug("%c", packet[i]);
		else
			debug("[%02x]", packet[i]);
	}
	debug(" ]\n");

	/*  Is this "connection" new, or a currently ongoing one?  */
	con_id = free_con_id = -1;
	for (i=0; i<MAX_UDP_CONNECTIONS; i++) {
		if (!udp_connections[i].in_use)
			free_con_id = i;
		if (udp_connections[i].in_use &&
		    udp_connections[i].inside_udp_port == srcport &&
		    udp_connections[i].outside_udp_port == dstport &&
		    memcmp(udp_connections[i].inside_ip_address,
			packet + 26, 4) == 0 &&
		    memcmp(udp_connections[i].outside_ip_address,
			packet + 30, 4) == 0) {
			con_id = i;
			break;
		}
	}

	fatal("&& UDP connection is ");
	if (con_id >= 0)
		fatal("ONGOING");
	else {
		fatal("NEW");
		if (free_con_id < 0) {
			int i;
			int64_t oldest = udp_connections[0].last_used_timestamp;
			free_con_id = 0;

			fatal(", NO FREE SLOTS, REUSING OLDEST ONE");
			for (i=0; i<MAX_UDP_CONNECTIONS; i++)
				if (udp_connections[i].last_used_timestamp < oldest) {
					oldest = udp_connections[i].last_used_timestamp;
					free_con_id = i;
				}
			close(udp_connections[free_con_id].socket);
		}
		con_id = free_con_id;
		memset(&udp_connections[con_id], 0,
		    sizeof(struct udp_connection));

		memcpy(udp_connections[con_id].ethernet_address,
		    packet + 6, 6);
		memcpy(udp_connections[con_id].inside_ip_address,
		    packet + 26, 4);
		udp_connections[con_id].inside_udp_port = srcport;
		memcpy(udp_connections[con_id].outside_ip_address,
		    packet + 30, 4);
		udp_connections[con_id].outside_udp_port = dstport;

		udp_connections[con_id].socket = socket(AF_INET, SOCK_DGRAM, 0);
		if (udp_connections[con_id].socket < 0) {
			fatal("[ net: UDP: socket() returned %i ]\n",
			    udp_connections[con_id].socket);
			return;
		}

		fatal(" {socket=%i}", udp_connections[con_id].socket);

		udp_connections[con_id].in_use = 1;

		/*  Set the socket to non-blocking:  */
		res = fcntl(udp_connections[con_id].socket, F_GETFL);
		fcntl(udp_connections[con_id].socket, F_SETFL,
		    res | O_NONBLOCK);
	}

	fatal(", connection id %i\n", con_id);

	net_timestamp ++;
	udp_connections[con_id].last_used_timestamp = net_timestamp;

	remote_ip.sin_family = AF_INET;
	memcpy((unsigned char *)&remote_ip.sin_addr,
	    udp_connections[con_id].outside_ip_address, 4);
	remote_ip.sin_port = htons(udp_connections[con_id].outside_udp_port);

	res = sendto(udp_connections[con_id].socket, packet + 42, len - 42,
	    0, (const struct sockaddr *)&remote_ip, sizeof(remote_ip));

	if (res != len-42)
		debug("[ net: UDP: unable to send %i bytes ]\n", len-42);
	else
		debug("[ net: UDP: OK!!! ]\n");
}


/*
 *  net_ip():
 *
 *  Handle an IP packet, coming from the emulated NIC.
 */
static void net_ip(void *extra, unsigned char *packet, int len)
{
	int i;

	debug("[ net: IP: ");
	debug("ver=%02x ", packet[14]);
	debug("tos=%02x ", packet[15]);
	debug("len=%02x%02x ", packet[16], packet[17]);
	debug("id=%02x%02x ",  packet[18], packet[19]);
	debug("ofs=%02x%02x ", packet[20], packet[21]);
	debug("ttl=%02x ", packet[22]);
	debug("p=%02x ", packet[23]);
	debug("sum=%02x%02x ", packet[24], packet[25]);
	debug("src=%02x%02x%02x%02x ",
	    packet[26], packet[27], packet[28], packet[29]);
	debug("dst=%02x%02x%02x%02x ",
	    packet[30], packet[31], packet[32], packet[33]);
	for (i=34; i<len; i++)
		debug("%02x", packet[i]);
	debug(" ]\n");

	if (packet[14] == 0x45) {
		/*  IPv4:  */
		switch (packet[23]) {
		case 1:	/*  ICMP  */
			net_ip_icmp(extra, packet, len);
			break;
		case 6:	/*  TCP  */
			net_ip_tcp(extra, packet, len);
			break;
		case 17:/*  UDP  */
			net_ip_udp(extra, packet, len);
			break;
		default:
			fatal("[ net: IP: UNIMPLEMENTED protocol %i ]\n",
			    packet[23]);
		}
	} else
		fatal("[ net: IP: UNIMPLEMENTED ip, first byte = 0x%02x ]\n",
		    packet[14]);
}


/*
 *  net_arp():
 *
 *  Handle an ARP packet, coming from the emulated NIC.
 *
 *  An ARP packet might look like this:
 *
 *	ARP header:
 *	    ARP hardware addr family:	0001
 *	    ARP protocol addr family:	0800
 *	    ARP addr lengths:		06 04
 *	    ARP request:		0001
 *	    ARP from:			112233445566 01020304
 *	    ARP to:			000000000000 01020301
 *
 *  An ARP request with a 'to' IP value of the gateway should cause an
 *  ARP response packet to be created.
 *
 *  An ARP request with the same from and to IP addresses should be ignored.
 *  (This would be a host testing to see if there is an IP collision.)
 */
static void net_arp(void *extra, unsigned char *packet, int len)
{
	int i;

	/*  TODO: This debug dump assumes ethernet->IPv4 translation:  */
	debug("[ net: ARP: ");
	for (i=0; i<2; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=2; i<4; i++)
		debug("%02x", packet[i]);
	debug(" ");
	debug("%02x", packet[4]);
	debug(" ");
	debug("%02x", packet[5]);
	debug(" req=");
	debug("%02x", packet[6]);	/*  Request type  */
	debug("%02x", packet[7]);
	debug(" from=");
	for (i=8; i<18; i++)
		debug("%02x", packet[i]);
	debug(" to=");
	for (i=18; i<28; i++)
		debug("%02x", packet[i]);
	debug(" ]\n");

	if (packet[0] == 0x00 && packet[1] == 0x01 &&
	    packet[2] == 0x08 && packet[3] == 0x00 &&
	    packet[4] == 0x06 && packet[5] == 0x04) {
		int r = (packet[6] << 8) + packet[7];
		struct ethernet_packet_link *lp;

		switch (r) {
		case 1:		/*  Request  */
			lp = net_allocate_packet_link(extra, len + 14);

			/*  Copy the old packet first:  */
			memcpy(lp->data + 14, packet, len);

			/*  Add ethernet ARP header:  */
			memcpy(lp->data + 0, lp->data + 8 + 14, 6);
			memcpy(lp->data + 6, gateway_addr, 6);
			lp->data[12] = 0x08; lp->data[13] = 0x06;

			/*  Address of the emulated machine:  */
			memcpy(lp->data + 18 + 14, lp->data + 8 + 14, 10);

			/*  Address of the gateway:  */
			memcpy(lp->data +  8 + 14, gateway_addr, 6);
			memcpy(lp->data + 14 + 14, gateway_ipv4, 4);

			/*  This is a Reply:  */
			lp->data[6 + 14] = 0x00; lp->data[7 + 14] = 0x02;

			break;
		case 2:		/*  Reply  */
		case 3:		/*  Reverse Request  */
		case 4:		/*  Reverse Reply  */
		default:
			fatal("[ net: ARP: UNIMPLEMENTED request type 0x%04x ]\n", r);
		}
	} else {
		fatal("[ net: ARP: UNIMPLEMENTED arp packet type: ");
		for (i=0; i<len; i++)
			fatal("%02x", packet[i]);
		fatal(" ]\n");
	}
}


/*
 *  net_ethernet_rx_avail():
 *
 *  Return 1 if there is a packet available for this 'extra' pointer, otherwise
 *  return 0.
 *
 *  Appart from actually checking for incoming packets from the outside world,
 *  this function basically works like net_ethernet_rx() but it only receives
 *  a return value telling us whether there is a packet or not, we don't
 *  actually get the packet.
 */
int net_ethernet_rx_avail(void *extra)
{
	int con_id;

	for (con_id=0; con_id<MAX_UDP_CONNECTIONS; con_id++) {
		ssize_t res;
		unsigned char buf[70000];
		unsigned char udp_data[70008];
		struct sockaddr_in from;
		socklen_t from_len = sizeof(from);
		int ip_len, udp_len;
		struct ethernet_packet_link *lp;
		int max_per_packet;
		int bytes_converted = 0;
		int this_packets_data_length;
		int fragment_ofs = 0;

		if (!udp_connections[con_id].in_use)
			continue;

		if (udp_connections[con_id].socket < 0) {
			fatal("INTERNAL ERROR in net.c, udp socket < 0 but in use?\n");
			continue;
		}

		res = recvfrom(udp_connections[con_id].socket, buf, sizeof(buf),
		    0, (struct sockaddr *)&from, &from_len);

		/*  No more incoming UDP on this connection?  */
		if (res < 0)
			continue;

		net_timestamp ++;
		udp_connections[con_id].last_used_timestamp = net_timestamp;

		udp_connections[con_id].udp_id ++;

		/*
		 *  We now have a UDP packet of size 'res' which we need
		 *  turn into one or more ethernet packets for the emulated
		 *  operating system.  Ethernet packets are at most 1518
		 *  bytes long. With some margin, that means we can have
		 *  about 1500 bytes per packet.
		 *
		 *	Ethernet = 14 bytes
		 *	IP = 20 bytes
		 *	(UDP = 8 bytes + data)
		 *
		 *  So data can be at most max_per_packet - 34. For UDP
		 *  fragments, each multiple should (?) be a multiple of
		 *  8 bytes, except the last which doesn't have any such
		 *  restriction.
		 */
		max_per_packet = 1500;

		/*  UDP:  */
		udp_len = res + 8;
		udp_data[0] = ((unsigned char *)&from)[2];	/*  outside_udp_port  */
		udp_data[1] = ((unsigned char *)&from)[3];
		udp_data[2] = (udp_connections[con_id].inside_udp_port >> 8) & 0xff;
		udp_data[3] = udp_connections[con_id].inside_udp_port & 0xff;
		udp_data[4] = udp_len >> 8;
		udp_data[5] = udp_len & 0xff;
		udp_data[6] = 0;
		udp_data[7] = 0;
		memcpy(udp_data + 8, buf, res);
		/*
		 *  TODO:  UDP checksum, if neccessary. At least NetBSD
		 *  and OpenBSD accept UDP packets with 0x0000 in the
		 *  checksum field anyway.
		 */

		while (bytes_converted < udp_len) {
			this_packets_data_length = udp_len - bytes_converted;

			/*  Do we need to fragment?  */
			if (this_packets_data_length > max_per_packet-34) {
				this_packets_data_length =
				    max_per_packet - 34;
				while (this_packets_data_length & 7)
					this_packets_data_length --;
			}

			ip_len = 20 + this_packets_data_length;

			lp = net_allocate_packet_link(extra,
			    14 + 20 + this_packets_data_length);

			/*  Ethernet header:  */
			memcpy(lp->data + 0, udp_connections[con_id].ethernet_address, 6);
			memcpy(lp->data + 6, gateway_addr, 6);
			lp->data[12] = 0x08;	/*  IP = 0x0800  */
			lp->data[13] = 0x00;

			/*  IP header:  */
			lp->data[14] = 0x45;	/*  ver  */
			lp->data[15] = 0x00;	/*  tos  */
			lp->data[16] = ip_len >> 8;
			lp->data[17] = ip_len & 0xff;
			lp->data[18] = udp_connections[con_id].udp_id >> 8;
			lp->data[19] = udp_connections[con_id].udp_id & 0xff;
			lp->data[20] = (fragment_ofs >> 8);
			if (bytes_converted + this_packets_data_length
			    < udp_len)
				lp->data[20] |= 0x20;	/*  More fragments  */
			lp->data[21] = fragment_ofs & 0xff;
			lp->data[22] = 0x40;	/*  ttl  */
			lp->data[23] = 17;	/*  p = UDP  */
			lp->data[26] = ((unsigned char *)&from)[4];
			lp->data[27] = ((unsigned char *)&from)[5];
			lp->data[28] = ((unsigned char *)&from)[6];
			lp->data[29] = ((unsigned char *)&from)[7];
			lp->data[30] = udp_connections[con_id].inside_ip_address[0];
			lp->data[31] = udp_connections[con_id].inside_ip_address[1];
			lp->data[32] = udp_connections[con_id].inside_ip_address[2];
			lp->data[33] = udp_connections[con_id].inside_ip_address[3];
			net_ip_checksum(lp->data + 14, 10, 20);

			memcpy(lp->data+34, udp_data + bytes_converted,
			    this_packets_data_length);

			bytes_converted += this_packets_data_length;
			fragment_ofs = bytes_converted / 8;
		}

		/*  This makes sure we check this connection AGAIN
		    for more incoming UDP packets, before moving to the
		    next connection:  */
		con_id --;
	}

	return net_ethernet_rx(extra, NULL, NULL);
}


/*
 *  net_ethernet_rx():
 *
 *  Receive an ethernet packet. (This means handing over an already prepared
 *  packet from this module (net.c) to a specific ethernet controller device.)
 *
 *  Return value is 1 if there was a packet available. *packetp and *lenp
 *  will be set to the packet's data pointer and length, respectively, and
 *  the packet will be removed from the linked list). If there was no packet
 *  available, 0 is returned.
 *
 *  If packetp is NULL, then the search is aborted as soon as a packet with
 *  the correct 'extra' field is found, and a 1 is returned, but as packetp
 *  is NULL we can't return the actual packet. (This is the internal form
 *  if net_ethernet_rx_avail().)
 */
int net_ethernet_rx(void *extra, unsigned char **packetp, int *lenp)
{
	struct ethernet_packet_link *lp, *prev;

	/*  Find the first packet which has the right 'extra' field.  */

	lp = first_ethernet_packet;
	prev = NULL;
	while (lp != NULL) {
		if (lp->extra == extra) {
			/*  We found a packet for this controller!  */
			if (packetp == NULL || lenp == NULL)
				return 1;

			/*  Let's return it:  */
			(*packetp) = lp->data;
			(*lenp) = lp->len;

			/*  Remove this link from the linked list:  */
			if (prev == NULL)
				first_ethernet_packet = lp->next;
			else
				prev->next = lp->next;

			if (lp->next == NULL)
				last_ethernet_packet = prev;
			else
				lp->next->prev = prev;

			free(lp);

			/*  ... and return successfully:  */
			return 1;
		}

		prev = lp;
		lp = lp->next;
	}

	/*  No packet found. :-(  */
	return 0;
}


/*
 *  net_ethernet_tx():
 *
 *  Transmit an ethernet packet, as seen from the emulated ethernet controller.
 *  If the packet can be handled here, it will not neccessarily be transmitted
 *  to the outside world.
 */
void net_ethernet_tx(void *extra, unsigned char *packet, int len)
{
	int i, n;

#if 0
	fatal("[ net: ethernet: ");
	for (i=0; i<6; i++)
		fatal("%02x", packet[i]);
	fatal(" ");
	for (i=6; i<12; i++)
		fatal("%02x", packet[i]);
	fatal(" ");
	for (i=12; i<14; i++)
		fatal("%02x", packet[i]);
	fatal(" ");
	for (i=14; i<len; i++)
		fatal("%02x", packet[i]);
	fatal(" ]\n");
#endif

	/*  Sprite:  */
	if (packet[12] == 0x05 && packet[13] == 0x00) {
		/*  TODO.  */
		fatal("[ net: TX: UNIMPLEMENTED Sprite packet ]\n");
		return;
	}

	/*  IP:  */
	if (packet[12] == 0x08 && packet[13] == 0x00) {
		/*  Routed via the gateway?  */
		if (memcmp(packet+0, gateway_addr, 6) == 0) {
			net_ip(extra, packet, len);
			return;
		}

		/*  Broadcast? (DHCP does this.)  */
		n = 0;
		for (i=0; i<6; i++)
			if (packet[i] == 0xff)
				n++;
		if (n == 6) {
			fatal("[ net: TX: IP broadcast: TODO ]\n");
			return;
		}

		fatal("[ net: TX: IP packet not for gateway, and not broadcast: ");
		for (i=0; i<14; i++)
			fatal("%02x", packet[i]);
		fatal(" ]\n");
		return;
	}

	/*  ARP:  */
	if (len == 60 && packet[12] == 0x08 && packet[13] == 0x06) {
		net_arp(extra, packet + 14, len - 14);
		return;
	}

	/*  RARP:  */
	if (packet[12] == 0x80 && packet[13] == 0x35) {
		/*  TODO.  */
		fatal("[ net: TX: UNIMPLEMENTED RARP packet ]\n");
		return;
	}

	/*  IPv6:  */
	if (packet[12] == 0x86 && packet[13] == 0xdd) {
		/*  TODO.  */
		fatal("[ net: TX: UNIMPLEMENTED IPv6 packet ]\n");
		return;
	}

	fatal("[ net: TX: UNIMPLEMENTED ethernet packet type 0x%02x%02x! ]\n",
	    packet[12], packet[13]);
}


/*
 *  net_init():
 *
 *  This function should be called before any other net_*() functions are
 *  used.
 */
void net_init(void)
{
	first_ethernet_packet = last_ethernet_packet = NULL;

	memset(udp_connections, 0, MAX_UDP_CONNECTIONS *
	    sizeof(struct udp_connection));
	memset(tcp_connections, 0, MAX_TCP_CONNECTIONS *
	    sizeof(struct tcp_connection));
}

