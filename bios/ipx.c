/*
THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.  
COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/


#pragma off (unreferenced)
static char rcsid[] = "$Id: ipx.c 2.15 1996/06/08 22:38:57 matt Exp $";
#pragma on (unreferenced)

#include <i86.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <conio.h>
#include <assert.h>

#include "types.h"
#include "timer.h"
#include "ipx.h"
#include "error.h"
#include "dpmi.h"
#include "key.h"

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;

typedef struct local_address {
	ubyte address[6];
} local_address;

typedef struct net_address {
	BYTE				network_id[4];			
	local_address	node_id;
	WORD				socket_id;
} net_address;

typedef struct ipx_header {
	WORD			checksum;
	WORD			length;
	BYTE			transport_control;
	BYTE			packet_type;
	net_address	destination;
	net_address	source;
} ipx_header;

typedef struct ecb_header {
	WORD			link[2];
	WORD			esr_address[2];
	BYTE			in_use;
	BYTE			completion_code;
	WORD			socket_id;
	BYTE			ipx_reserved[14];        
	WORD			connection_id;
	local_address immediate_address;
	WORD    		fragment_count;
	WORD			fragment_pointer[2];
	WORD			fragment_size;
} ecb_header;

typedef struct packet_data {
	int			packetnum;
	byte			data[IPX_MAX_DATA_SIZE];
} packet_data;

typedef struct ipx_packet {
	ecb_header	ecb;
	ipx_header	ipx;
	packet_data	pd;
} ipx_packet;

typedef struct user_address {
	ubyte network[4];
	ubyte node[6];
	ubyte address[6];
} user_address;

#define MAX_USERS 64
int Ipx_num_users = 0;
user_address Ipx_users[MAX_USERS];

#define MAX_NETWORKS 64
int Ipx_num_networks = 0;
uint Ipx_networks[MAX_NETWORKS];

int ipx_packetnum = 0;

#define MAX_PACKETS 64

static packet_data packet_buffers[MAX_PACKETS];
static short packet_free_list[MAX_PACKETS];
static int num_packets = 0;
static int largest_packet_index = 0;
static short packet_size[MAX_PACKETS];

WORD ipx_socket=0;
ubyte ipx_installed=0;
WORD ipx_vector_segment;
WORD ipx_vector_offset;
ubyte ipx_socket_life = 0; 	// 0=closed at prog termination, 0xff=closed when requested.
DWORD ipx_network = 0;
local_address ipx_my_node;
WORD ipx_num_packets=32;		// 32 Ipx packets
ipx_packet * packets;
int neterrors = 0;
ushort ipx_packets_selector;

ecb_header * last_ecb=NULL;
int lastlen=0;

void free_packet( int id )
{
	packet_buffers[id].packetnum = -1;
	packet_free_list[ --num_packets ] = id;
	if (largest_packet_index==id)	
		while ((--largest_packet_index>0) && (packet_buffers[largest_packet_index].packetnum == -1 ));
}

int ipx_get_packet_data( ubyte * data )
{
	int i, n, best, best_id, size;

	for (i=1; i<ipx_num_packets; i++ )	{
		if ( !packets[i].ecb.in_use )	{
			got_new_packet( &packets[i].ecb );
			packets[i].ecb.in_use = 0;
			ipx_listen_for_packet(&packets[i].ecb);
		}			
	}

	best = -1;
	n = 0;
	best_id = -1;

	for (i=0; i<=largest_packet_index; i++ )	{
		if ( packet_buffers[i].packetnum > -1 ) {
			n++;
			if ( best == -1 || (packet_buffers[i].packetnum<best) )	{
				best = packet_buffers[i].packetnum;
				best_id = i;
			}
		}			
	}

	//mprintf( (0, "Best id = %d, pn = %d, last_ecb = %x, len=%x, ne = %d\n", best_id, best, last_ecb, lastlen, neterrors ));
	//mprintf( (1, "<%d> ", neterrors ));

	if ( best_id < 0 ) return 0;

	size = packet_size[best_id];
	memcpy( data, packet_buffers[best_id].data, size );
	free_packet(best_id);

	return size;
}

unsigned int swap_short( unsigned int short );
#pragma aux swap_short parm [eax] = "xchg al,ah";

void got_new_packet( ecb_header * ecb )
{
	ipx_packet * p;
	int id;
	unsigned short datasize;

	datasize = 0;
	last_ecb = ecb;
	p = (ipx_packet *)ecb;

	if ( p->ecb.in_use ) { neterrors++; return; }
	if	( p->ecb.completion_code )	{ neterrors++; return; }

	//	Error( "Recieve error %d for completion code", p->ecb.completion_code );
	
	if ( memcmp( &p->ipx.source.node_id, &ipx_my_node, 6 ) )	{
		datasize=swap_short(p->ipx.length);
		lastlen=datasize;
		datasize -= sizeof(ipx_header);
		// Find slot to put packet in...
		if ( datasize > 0 && datasize <= sizeof(packet_data) )	{
			if ( num_packets >= MAX_PACKETS ) {
				//printf( 1, "IPX: Packet buffer overrun!!!\n" );
				neterrors++;
				return;
			}		
			id = packet_free_list[ num_packets++ ];
			if (id > largest_packet_index ) largest_packet_index = id;
			packet_size[id] = datasize-sizeof(int);
			packet_buffers[id].packetnum =  p->pd.packetnum;
			if ( packet_buffers[id].packetnum < 0 ) { neterrors++; return; }
			memcpy( packet_buffers[id].data, p->pd.data, packet_size[id] );
		} else {
			neterrors++; return;
		}
	} 
	// Repost the ecb
	p->ecb.in_use = 0;
	//ipx_listen_for_packet(&p->ecb);
}

ubyte * ipx_get_my_local_address()
{
	return ipx_my_node.address;
}

ubyte * ipx_get_my_server_address()
{
	return (ubyte *)&ipx_network;
}

void ipx_listen_for_packet(ecb_header * ecb )	
{
	dpmi_real_regs rregs;
	ecb->in_use = 0x1d;
	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.ebx = 4;	// Listen For Packet function
	rregs.esi = DPMI_real_offset(ecb);
	rregs.es = DPMI_real_segment(ecb);
	dpmi_real_int386x( 0x7A, &rregs );
}

void ipx_cancel_listen_for_packet(ecb_header * ecb )	
{
	dpmi_real_regs rregs;
	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.ebx = 6;	// IPX Cancel event
	rregs.esi = DPMI_real_offset(ecb);
	rregs.es = DPMI_real_segment(ecb);
	dpmi_real_int386x( 0x7A, &rregs );
}


void ipx_send_packet(ecb_header * ecb )	
{
	dpmi_real_regs rregs;
	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.ebx = 3;	// Send Packet function
	rregs.esi = DPMI_real_offset(ecb);
	rregs.es = DPMI_real_segment(ecb);
	dpmi_real_int386x( 0x7A, &rregs );
}

typedef struct {
	ubyte 	network[4];
	ubyte		node[6];
	ubyte		local_target[6];
} net_xlat_info;

void ipx_get_local_target( ubyte * server, ubyte * node, ubyte * local_target )
{
	net_xlat_info * info;
	dpmi_real_regs rregs;
		
	// Get dos memory for call...
	info = (net_xlat_info *)dpmi_get_temp_low_buffer( sizeof(net_xlat_info) );	
	assert( info != NULL );
	memcpy( info->network, server, 4 );
	memcpy( info->node, node, 6 );
	
	memset(&rregs,0,sizeof(dpmi_real_regs));

	rregs.ebx = 2;		// Get Local Target	
	rregs.es = DPMI_real_segment(info);
	rregs.esi = DPMI_real_offset(info->network);
	rregs.edi = DPMI_real_offset(info->local_target);

	dpmi_real_int386x( 0x7A, &rregs );

	// Save the local target...
	memcpy( local_target, info->local_target, 6 );
}

void ipx_close()
{
	dpmi_real_regs rregs;
	if ( ipx_installed )	{
		// When using VLM's instead of NETX, the sockets don't
		// seem to automatically get closed, so we must explicitly
		// close them at program termination.
		ipx_installed = 0;
		memset(&rregs,0,sizeof(dpmi_real_regs));
		rregs.edx = ipx_socket;
		rregs.ebx = 1;	// Close socket
		dpmi_real_int386x( 0x7A, &rregs );
	}
}


//---------------------------------------------------------------
// Initializes all IPX internals. 
// If socket_number==0, then opens next available socket.
// Returns:	0  if successful.
//				-1 if socket already open.
//				-2	if socket table full.
//				-3 if IPX not installed.
//				-4 if couldn't allocate low dos memory
//				-5 if error with getting internetwork address

int ipx_init( int socket_number, int show_address )
{
	dpmi_real_regs rregs;
	ubyte *ipx_real_buffer;
	int i;

	atexit(ipx_close);

	ipx_packetnum = 0;

	// init packet buffers.
	for (i=0; i<MAX_PACKETS; i++ )	{
		packet_buffers[i].packetnum = -1;
		packet_free_list[i] = i;
	}
	num_packets = 0;
	largest_packet_index = 0;

	// Get the IPX vector
	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.eax=0x00007a00;
	dpmi_real_int386x( 0x2f, &rregs );

	if ( (rregs.eax & 0xFF) != 0xFF )	{
		return 3;   
	}
	ipx_vector_offset = rregs.edi & 0xFFFF;
	ipx_vector_segment = rregs.es;
	//printf( "IPX entry point at %.4x:%.4x\n", ipx_vector_segment, ipx_vector_offset );

	// Open a socket for IPX

	memset(&rregs,0,sizeof(dpmi_real_regs));
	swab( (char *)&socket_number,(char *)&ipx_socket, 2 );
	rregs.edx = ipx_socket;
	rregs.eax = ipx_socket_life;
	rregs.ebx = 0;	// Open socket
	dpmi_real_int386x( 0x7A, &rregs );
	
	ipx_socket = rregs.edx & 0xFFFF;
	
	if ( rregs.eax & 0xFF )	{
		//mprintf( (1, "IPX error opening channel %d\n", socket_number-IPX_DEFAULT_SOCKET ));
		return -2;
	}
	
	ipx_installed = 1;

	// Find our internetwork address
	ipx_real_buffer = dpmi_get_temp_low_buffer( 1024 );	// 1k block
	if ( ipx_real_buffer == NULL )	{
		//printf( "Error allocation realmode memory\n" );
		return -4;
	}

	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.ebx = 9;		// Get internetwork address
	rregs.esi = DPMI_real_offset(ipx_real_buffer);
	rregs.es = DPMI_real_segment(ipx_real_buffer);
	dpmi_real_int386x( 0x7A, &rregs );

	if ( rregs.eax & 0xFF )	{
		//printf( "Error getting internetwork address!\n" );
		return -2;
	}

	memcpy( &ipx_network, ipx_real_buffer, 4 );
	memcpy( &ipx_my_node, &ipx_real_buffer[4], 6 );

	Ipx_num_networks = 0;
	memcpy( &Ipx_networks[Ipx_num_networks++], &ipx_network, 4 );

	if ( show_address )	{
		printf( "My IPX addresss is " );
		printf( "%02X%02X%02X%02X/", ipx_real_buffer[0],ipx_real_buffer[1],ipx_real_buffer[2],ipx_real_buffer[3] );
		printf( "%02X%02X%02X%02X%02X%02X\n", ipx_real_buffer[4],ipx_real_buffer[5],ipx_real_buffer[6],ipx_real_buffer[7],ipx_real_buffer[8],ipx_real_buffer[9] );
		printf( "\n" );
	}

	packets = dpmi_real_malloc( sizeof(ipx_packet)*ipx_num_packets, &ipx_packets_selector );
	if ( packets == NULL )	{
		//printf( "Couldn't allocate real memory for %d packets\n", ipx_num_packets );
		return -4;
	}
	if (!dpmi_lock_region( packets, sizeof(ipx_packet)*ipx_num_packets ))	{
		//printf( "Couldn't lock real memory for %d packets\n", ipx_num_packets );
		return -4;
	}
	memset( packets, 0, sizeof(ipx_packet)*ipx_num_packets );

	for (i=1; i<ipx_num_packets; i++ )	{
		packets[i].ecb.in_use = 0x1d;
		//packets[i].ecb.in_use = 0;
		packets[i].ecb.socket_id = ipx_socket;
		packets[i].ecb.fragment_count = 1;
		packets[i].ecb.fragment_pointer[0] = DPMI_real_offset(&packets[i].ipx);
		packets[i].ecb.fragment_pointer[1] = DPMI_real_segment(&packets[i].ipx);
		packets[i].ecb.fragment_size = sizeof(ipx_packet)-sizeof(ecb_header);			//-sizeof(ecb_header);

		ipx_listen_for_packet(&packets[i].ecb);
	}

	packets[0].ecb.socket_id = ipx_socket;
	packets[0].ecb.fragment_count = 1;
	packets[0].ecb.fragment_pointer[0] = DPMI_real_offset(&packets[0].ipx);
	packets[0].ecb.fragment_pointer[1] = DPMI_real_segment(&packets[0].ipx);
	packets[0].ipx.packet_type = 4;		// IPX packet
	packets[0].ipx.destination.socket_id = ipx_socket;
//	memcpy( packets[0].ipx.destination.network_id, &ipx_network, 4 );
	memset( packets[0].ipx.destination.network_id, 0, 4 );

	return 0;
}

void ipx_send_packet_data( ubyte * data, int datasize, ubyte *network, ubyte *address, ubyte *immediate_address )
{
	assert(ipx_installed);

	Assert(datasize < IPX_MAX_DATA_SIZE);

	// Make sure no one is already sending something
	while( packets[0].ecb.in_use )
	{
	}
	
//	if (packets[0].ecb.completion_code)
	  //	Error("IPX: Send error %d for completion code\n", packets[0].ecb.completion_code );

	// Fill in destination address
	if ( memcmp( network, &ipx_network, 4 ) )
		memcpy( packets[0].ipx.destination.network_id, network, 4 );
	else
		memset( packets[0].ipx.destination.network_id, 0, 4 );
	memcpy( packets[0].ipx.destination.node_id.address, address, 6 );
	memcpy( packets[0].ecb.immediate_address.address, immediate_address, 6 );
	packets[0].pd.packetnum = ipx_packetnum++;

	// Fill in data to send
	packets[0].ecb.fragment_size = sizeof(ipx_header) + sizeof(int) + datasize;

	assert( datasize > 1 );
	assert( packets[0].ecb.fragment_size <= 576 );

	memcpy( packets[0].pd.data, data, datasize );

	// Send it
	ipx_send_packet( &packets[0].ecb );

}

void ipx_send_broadcast_packet_data( ubyte * data, int datasize )	
{
	int i, j;
	ubyte broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	ubyte local_address[6];

	// Set to all networks besides mine
	for (i=0; i<Ipx_num_networks; i++ )	{
		if ( memcmp( &Ipx_networks[i], &ipx_network, 4 ) )	{
			ipx_get_local_target( (ubyte *)&Ipx_networks[i], broadcast, local_address );
			ipx_send_packet_data( data, datasize, (ubyte *)&Ipx_networks[i], broadcast, local_address );
		} else {
			ipx_send_packet_data( data, datasize, (ubyte *)&Ipx_networks[i], broadcast, broadcast );
		}
	}

	//OLDipx_send_packet_data( data, datasize, (ubyte *)&ipx_network, broadcast, broadcast );

	// Send directly to all users not on my network or in the network list.
	for (i=0; i<Ipx_num_users; i++ )	{
		if ( memcmp( Ipx_users[i].network, &ipx_network, 4 ) )	{
			for (j=0; j<Ipx_num_networks; j++ )		{
				if (!memcmp( Ipx_users[i].network, &Ipx_networks[j], 4 ))
					goto SkipUser;
			}
			ipx_send_packet_data( data, datasize, Ipx_users[i].network, Ipx_users[i].node, Ipx_users[i].address );
SkipUser:
			j = 0;
		}
	}
}

// Sends a non-localized packet... needs 4 byte server, 6 byte address
void ipx_send_internetwork_packet_data( ubyte * data, int datasize, ubyte * server, ubyte *address )
{
	ubyte local_address[6];

	if ( (*(uint *)server) != 0 )	{
		ipx_get_local_target( server, address, local_address );
		ipx_send_packet_data( data, datasize, server, address, local_address );
	} else {
		// Old method, no server info.
		ipx_send_packet_data( data, datasize, server, address, address );
	}
}

int ipx_change_default_socket( ushort socket_number )
{
	int i;
	WORD new_ipx_socket;
	dpmi_real_regs rregs;

	if ( !ipx_installed ) return -3;

	// Open a new socket	
	memset(&rregs,0,sizeof(dpmi_real_regs));
	swab( (char *)&socket_number,(char *)&new_ipx_socket, 2 );
	rregs.edx = new_ipx_socket;
	rregs.eax = ipx_socket_life;
	rregs.ebx = 0;	// Open socket
	dpmi_real_int386x( 0x7A, &rregs );
	
	new_ipx_socket = rregs.edx & 0xFFFF;
	
	if ( rregs.eax & 0xFF )	{
		//printf( (1, "IPX error opening channel %d\n", socket_number-IPX_DEFAULT_SOCKET ));
		return -2;
	}

	for (i=1; i<ipx_num_packets; i++ )	{
		ipx_cancel_listen_for_packet(&packets[i].ecb);
	}

	// Close existing socket...
	memset(&rregs,0,sizeof(dpmi_real_regs));
	rregs.edx = ipx_socket;
	rregs.ebx = 1;	// Close socket
	dpmi_real_int386x( 0x7A, &rregs );

	ipx_socket = new_ipx_socket;

	// Repost all listen requests on the new socket...	
	for (i=1; i<ipx_num_packets; i++ )	{
		packets[i].ecb.in_use = 0;
		packets[i].ecb.socket_id = ipx_socket;
		ipx_listen_for_packet(&packets[i].ecb);
	}

	packets[0].ecb.socket_id = ipx_socket;
	packets[0].ipx.destination.socket_id = ipx_socket;

	ipx_packetnum = 0;
	// init packet buffers.
	for (i=0; i<MAX_PACKETS; i++ )	{
		packet_buffers[i].packetnum = -1;
		packet_free_list[i] = i;
	}
	num_packets = 0;
	largest_packet_index = 0;

	return 0;
}

void ipx_read_user_file(char * filename)
{
	FILE * fp;
	user_address tmp;
	char temp_line[132], *p1;
	int n, ln=0;

	if (!filename) return;

	Ipx_num_users = 0;

	fp = fopen( filename, "rt" );
	if ( !fp ) return;

	printf( "Broadcast Users:\n" );

	while (fgets(temp_line, 132, fp)) {
		ulong net;
		char *np;
		ln++;
		p1 = strchr(temp_line,'\n'); if (p1) *p1 = '\0';
		p1 = strchr(temp_line,';'); if (p1) *p1 = '\0';
		if (strlen(temp_line) == 0) continue;  //skip blank lines
		//n = sscanf( temp_line, "%2x%2x%2x%2x/%2x%2x%2x%2x%2x%2x", &tmp.network[0], &tmp.network[1], &tmp.network[2], &tmp.network[3], &tmp.node[0], &tmp.node[1], &tmp.node[2],&tmp.node[3], &tmp.node[4], &tmp.node[5] );
		//if ( n != 10 ) continue;
		np = strchr(temp_line,'/');
		if (!np)
			Error("Invalid entry <%s> on line %d of IPX User file <%s>",temp_line,ln,filename);
		else np++;
		n = sscanf( temp_line, "%x", &net);
		if (n != 1)
			Error("Invalid entry <%s> on line %d of IPX User file <%s>",temp_line,ln,filename);
		else {
			tmp.network[3] = (net & 0xff); net >>= 8;
			tmp.network[2] = (net & 0xff); net >>= 8;
			tmp.network[1] = (net & 0xff); net >>= 8;
			tmp.network[0] = (net & 0xff);
		}
		n = sscanf( np, "%2x%2x%2x%2x%2x%2x", &tmp.node[0], &tmp.node[1], &tmp.node[2],&tmp.node[3], &tmp.node[4], &tmp.node[5] );
		if (n != 6)
			Error("Invalid entry <%s> on line %d of IPX User file <%s>\n"
					"  Node address must be 6 bytes (12 digits)",temp_line,ln,filename);
		if ( Ipx_num_users < MAX_USERS )	{
			ubyte * ipx_real_buffer = (ubyte *)&tmp;
			ipx_get_local_target( tmp.network, tmp.node, tmp.address );
			Ipx_users[Ipx_num_users++] = tmp;
			printf( "%02X%02X%02X%02X/", ipx_real_buffer[0],ipx_real_buffer[1],ipx_real_buffer[2],ipx_real_buffer[3] );
			printf( "%02X%02X%02X%02X%02X%02X\n", ipx_real_buffer[4],ipx_real_buffer[5],ipx_real_buffer[6],ipx_real_buffer[7],ipx_real_buffer[8],ipx_real_buffer[9] );
		} else {
			printf( "Too many addresses in %s! (Limit of %d)\n", filename, MAX_USERS );
			fclose(fp);
			return;
		}
	}
	fclose(fp);

}


void ipx_read_network_file(char * filename)
{
	FILE * fp;
	user_address tmp;
	char temp_line[132], *p1;
	int i, n, ln=0;

	if (!filename) return;

	fp = fopen( filename, "rt" );
	if ( !fp ) return;

	printf( "Using Networks:\n" );
	for (i=0; i<Ipx_num_networks; i++ )		{
		ubyte * n1 = (ubyte *)&Ipx_networks[i];
		printf("* %02x%02x%02x%02x\n", n1[0], n1[1], n1[2], n1[3] );
	}

	while (fgets(temp_line, 132, fp)) {
		ulong net;
		ln++;
		p1 = strchr(temp_line,'\n'); if (p1) *p1 = '\0';
		p1 = strchr(temp_line,';'); if (p1) *p1 = '\0';
		if (strlen(temp_line) == 0) continue;  //skip blank lines
		//n = sscanf( temp_line, "%2x%2x%2x%2x", &tmp.network[0], &tmp.network[1], &tmp.network[2], &tmp.network[3] );
		//if ( n != 4 ) continue;
		n = sscanf( temp_line, "%x", &net );
		if (n != 1)
			Error("Invalid entry <%s> on line %d of IPX Net file <%s>",temp_line,ln,filename);
		else {
			tmp.network[3] = (net & 0xff); net >>= 8;
			tmp.network[2] = (net & 0xff); net >>= 8;
			tmp.network[1] = (net & 0xff); net >>= 8;
			tmp.network[0] = (net & 0xff);
		}

		if ( Ipx_num_networks < MAX_NETWORKS  )	{
			int j;
			for (j=0; j<Ipx_num_networks; j++ )	
				if ( !memcmp( &Ipx_networks[j], tmp.network, 4 ) )
					break;
			if ( j >= Ipx_num_networks )	{
				memcpy( &Ipx_networks[Ipx_num_networks++], tmp.network, 4 );
				printf("  %02x%02x%02x%02x\n", tmp.network[0], tmp.network[1], tmp.network[2], tmp.network[3] );
			}
		} else {
			printf( "Too many networks in %s! (Limit of %d)\n", filename, MAX_NETWORKS );
			fclose(fp);
			return;
		}
	}
	fclose(fp);

}

//---typedef struct rip_entry {
//---	uint	 	network;
//---	ushort	nhops;
//---	ushort	nticks;
//---} rip_entry;
//---
//---typedef struct rip_packet {
//---	ushort		operation;		//1=request, 2=response
//---	rip_entry	rip[50];
//---} rip_packet;
//---
//---
//---void  ipx_find_all_servers()
//---{
//---	int i;
//---	rip_packet * rp;
//---	assert(ipx_installed);
//---
//---	ipx_change_default_socket( 0x0453 );
//---	//	ipx_change_default_socket( 0x5304 );
//---
//---	// Make sure no one is already sending something
//---	while( packets[0].ecb.in_use )
//---	{
//---	}
//---	
//---	if (packets[0].ecb.completion_code)	{
//---		printf( "AAAA:Send error %d for completion code\n", packets[0].ecb.completion_code );
//---		//exit(1);
//---	}
//---
//---	rp = (rip_packet *)&packets[0].pd;
//---
//---	// Fill in destination address
//---	{
//---		char mzero1[] = {0,0,0,1};
//---		char mzero[] = {0,0,0,0,0,1};
//---		char immediate[6];
//---		//memcpy( packets[0].ipx.destination.network_id, &ipx_network, 4 );
//---		//memcpy( packets[0].ipx.destination.node_id.address, ipx_my_node.address, 6 );
//---
//---	  	memcpy( packets[0].ipx.destination.network_id, mzero1, 4 );
//---		memcpy( packets[0].ipx.destination.node_id.address, mzero, 6 );
//---
//---		memcpy( packets[0].ipx.destination.socket_id, &ipx_socket, 2 );
//---		memcpy( packets[0].ipx.source.network_id, &ipx_network, 4 );
//---		memcpy( packets[0].ipx.source.node_id.address, ipx_my_node.address, 6 );
//---		memcpy( packets[0].ipx.source.socket_id, &ipx_socket, 2 );
//---		//memcpy( packets[0].ecb.immediate_address.address, ipx_my_node.address, 6 );
//---		//mzero1[3] = 1;
//---		//memcpy( packets[0].ipx.destination.network_id, mzero1, 4 );
//---		//mzero[5] = 1;
//---		//memcpy( packets[0].ipx.destination.node_id.address, mzero, 6 );
//---		//ipx_get_local_target( mzero1, mzero, immediate );
//---		//memcpy( packets[0].ecb.immediate_address.address, mzero, 6 );
//---		//memcpy( packets[0].ecb.immediate_address.address, immediate, 6 );
//---		//mzero[5] = 0;
//---	}
//---
//---	packets[0].ipx.packet_type = 1;		// RIP packet
//---
//---	// Fill in data to send
//---	packets[0].ecb.fragment_size = sizeof(ipx_header) + sizeof(rip_packet);
//---	assert( packets[0].ecb.fragment_size <= 576 );
//---
//---	rp->operation = 0;		// Request
//---	for (i=0;i<50; i++)	{
//---		rp->rip[i].network = 0xFFFFFFFF;
//---		rp->rip[i].nhops = 0;
//---		rp->rip[i].nticks = 0;
//---	}
//---
//---	// Send it
//---	ipx_send_packet( &packets[0].ecb );
//---
//---	for (i=0;i<50; i++)	{
//---		if ( rp->rip[i].network != 0xFFFFFFFF )
//---			printf( "Network = %8x, Hops=%d, Ticks=%d\n", rp->rip[i].network, rp->rip[i].nhops, rp->rip[i].nticks );
//---	}
//---}
//---
//---
