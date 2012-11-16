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
static char rcsid[] = "$Id: ibitblt.c 1.16 1996/12/19 16:51:38 matt Exp $";
#pragma on (unreferenced)

#ifndef MACINTOSH


#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pa_enabl.h"                   //$$POLY_ACC
#include "types.h"
#include "gr.h"
#include "mem.h"
#include "error.h"
#include "ibitblt.h"
#include "grdef.h"

#if defined(POLY_ACC)
#include "poly_acc.h"
#endif

#define MODE_NONE		0
#define MODE_SKIP		1
#define MODE_DRAW		2

#define OPCODE_ADD 			0x81
#define OPCODE_ESI			0xC6			// Followed by a dword	(add esi, ????)
#define OPCODE_EDI		 	0xC7			// Followed by a dword  (add edi, ????)
#define OPCODE_MOV_ECX		0xB9			// Followed by a dword  (mov ecx,????)
#define OPCODE_MOVSB   		0xA4			// movsb
#define OPCODE_16BIT 		0x66			// movsw
#define OPCODE_MOVSD 		0xA5			// movsd
#define OPCODE_REP			0xF3			// rep
#define OPCODE_RET			0xC3			// ret
#define OPCODE_MOV_EAX		0xB8			// mov eax, im dword
#define OPCODE_MOV_EBX		0xBB			// mov ebx, im dword
#define OPCODE_CALL_EBX1	0xFF			// call
#define OPCODE_CALL_EBX2	0xD3			//      ebx
#define OPCODE_MOV_EDI		0xBF			// mov edi, im dword


ubyte *Code_pointer = NULL;
int Code_counter = 0;
int	ibitblt_svga_page = 0;
int is_svga = 0;
uint linear_address;



void count_block( int ecx )
{
	int blocks;

	while ( ecx > 0 )	{
		switch(ecx)	{
		case 1: Code_counter++; ecx = 0; break;	// MOVSB
		case 2: Code_counter+=2; ecx = 0; break;	// MOVSW
		case 3: Code_counter+=3; ecx = 0; break;	// MOVSW, MOVSB
		case 4: Code_counter++; ecx = 0; break; 	// MOVSD
		default:
			blocks = ecx / 4;
			if ( blocks == 1 )
				Code_counter++;	// MOVSD
			else
				Code_counter+=7;
			ecx -= blocks*4;
		}
	}
}


void move_and_count( int dsource, int ddest, int ecx )
{
	if ( ecx <= 0 )
		return;

	if ( dsource > 0 )	{
		// ADD ESI, dsource
		Code_counter += 6;
	}
	if ( !is_svga )	{
		if ( ddest > 0 )	{
			// ADD EDI, ddest
			Code_counter += 6;
		}
		count_block( ecx );
	} else {
		int p1, p2, o1;

		linear_address += ddest;		// Skip to next block

		p1 = linear_address >> 16;	o1 = linear_address & 0xFFFF;
		p2 = (linear_address+ecx)	>> 16;
		if ( p1 != ibitblt_svga_page )	{
			// Set page
			// MOV EAX, ?, CALL EBX
			Code_counter += 7;
			ibitblt_svga_page = p1;
		}

		Code_counter += 5;	// mov edi, ????

		if ( p1 == p2 )	{
			count_block( ecx );
		} else {
			int nbytes;
			nbytes = 0xFFFF-o1+1;
			count_block( nbytes );
			// set page
			// MOV EAX, 0
			Code_counter += 7;	// mov eax,???? call ebx

			ibitblt_svga_page = p2;

			Code_counter += 5;	// mov edi, ????

			nbytes = ecx - nbytes;
			if (nbytes > 0 )
				count_block( nbytes );
		}
		linear_address += ecx;
	}
}



void draw_block( int ecx )
{
	int blocks;
	int * iptr;

	while ( ecx > 0 )	{
		switch( ecx )	{
		case 1:
			// MOVSB
			*Code_pointer++ = OPCODE_MOVSB;
			ecx = 0;
			break;
		case 2:
			// MOVSW
			*Code_pointer++ = OPCODE_16BIT;
			*Code_pointer++ = OPCODE_MOVSD;
			ecx = 0;
			break;
		case 3:
			// MOVSW, MOVSB
			*Code_pointer++ = OPCODE_16BIT;
			*Code_pointer++ = OPCODE_MOVSD;
			*Code_pointer++ = OPCODE_MOVSB;
			ecx = 0;
			break;
		case 4:
			// MOVSD
			*Code_pointer++ = OPCODE_MOVSD;
			ecx = 0;
			break;
		default:
			blocks = ecx / 4;

			if ( blocks == 1 )	{
				// MOVSD
				*Code_pointer++ = OPCODE_MOVSD;
			} else {
				// MOV ECX, blocks
				*Code_pointer++ = OPCODE_MOV_ECX;
				iptr = (int *)Code_pointer;
				*iptr++ = blocks;
				Code_pointer = (ubyte *)iptr;
				// REP MOVSD
				*Code_pointer++ = OPCODE_REP;
				*Code_pointer++ = OPCODE_MOVSD;
			}
			ecx -= blocks*4;
		}
	}
}


void move_and_draw( int dsource, int ddest, int ecx )
{
	int * iptr;

	if ( ecx <= 0 )
		return;

	if ( dsource > 0 )	{
		// ADD ESI, dsource
		*Code_pointer++ = OPCODE_ADD;
		*Code_pointer++ = OPCODE_ESI;
		iptr = (int *)Code_pointer;
		*iptr++ = dsource;
		Code_pointer = (ubyte *)iptr;
	}
	if ( !is_svga )	{
		if ( ddest > 0 )	{
			// ADD EDI, ddest
			*Code_pointer++ = OPCODE_ADD;
			*Code_pointer++ = OPCODE_EDI;
			iptr = (int *)Code_pointer;
			*iptr++ = ddest;
			Code_pointer = (ubyte *)iptr;
		}
		draw_block( ecx );
	} else {
		unsigned int temp;
		int temp_offset;
		int p1, p2, o1;

		linear_address += ddest;		// Skip to next block

		p1 = linear_address >> 16;	o1 = linear_address & 0xFFFF;
		p2 = (linear_address+ecx)	>> 16;
		if ( p1 != ibitblt_svga_page )	{
			// Set page
			// MOV EAX, 0
			*Code_pointer++ = OPCODE_MOV_EAX;
			temp = p1;
			memcpy( Code_pointer, &temp, sizeof(int) );
			Code_pointer += sizeof(int);
			// CALL EBX
			*Code_pointer++ = OPCODE_CALL_EBX1;
			*Code_pointer++ = OPCODE_CALL_EBX2;
			ibitblt_svga_page = p1;
		}

		temp_offset = 0xA0000 + o1;
		*Code_pointer++ = OPCODE_MOV_EDI;
		iptr = (int *)Code_pointer;
		*iptr++ = temp_offset;
		Code_pointer = (ubyte *)iptr;

		if ( p1 == p2 )	{
			draw_block( ecx );
		} else {
			int nbytes;
			nbytes = 0xFFFF-o1+1;
			draw_block( nbytes );
			// set page
			// MOV EAX, 0
			*Code_pointer++ = OPCODE_MOV_EAX;
			temp = p2;
			memcpy( Code_pointer, &temp, sizeof(int) );
			Code_pointer += sizeof(int);
			// CALL EBX
			*Code_pointer++ = OPCODE_CALL_EBX1;
			*Code_pointer++ = OPCODE_CALL_EBX2;
			ibitblt_svga_page = p2;

			temp_offset = 0xA0000;
			*Code_pointer++ = OPCODE_MOV_EDI;
			iptr = (int *)Code_pointer;
			*iptr++ = temp_offset;
			Code_pointer = (ubyte *)iptr;

			nbytes = ecx - nbytes;
			if (nbytes > 0 )
				draw_block( nbytes );
		}
		linear_address += ecx;
	}

}

//-----------------------------------------------------------------------------------------
// Given bitmap, bmp, finds the size of the code

int gr_ibitblt_find_code_size_sub( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize, int dest_type )
{
	int x,y;
	ubyte pixel;
	int draw_mode = MODE_NONE;
	int source_offset = 0;
	int dest_offset = 0;
	int num_to_draw, draw_start_source, draw_start_dest;
	int esi, edi;

	Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );

	Code_counter = 0;

	if ( dest_type == BM_SVGA )	{
		Code_counter += 1+4;		// move ebx, gr_vesa_set_page
		Code_counter += 1+4;		// move eax, 0
		Code_counter += 2;		// call ebx
		ibitblt_svga_page = 0;
		linear_address = 0;
		is_svga = 1;
	} else {
		is_svga = 0;
	}

	esi = source_offset = 0;
	edi = dest_offset = 0;
	draw_start_source = draw_start_dest = 0;

	for ( y=sy; y<sy+sh; y++ )	{
		for ( x=sx; x<sx+sw; x++ )	{
			dest_offset = y*mask_bmp->bm_rowsize+x;
			pixel = mask_bmp->bm_data[dest_offset];
			if ( pixel!=255 )	{
				switch ( draw_mode)	{
				case MODE_DRAW:
					move_and_count( draw_start_source-esi, draw_start_dest-edi, num_to_draw );
					esi = draw_start_source + num_to_draw;
					edi = draw_start_dest + num_to_draw;
					// fall through!!!
				case MODE_NONE:
				case MODE_SKIP:
					break;
				}
				draw_mode = MODE_SKIP;
			} else {
				switch ( draw_mode)	{
				case MODE_SKIP:
				case MODE_NONE:
					draw_start_source = source_offset;
					draw_start_dest = dest_offset;
					num_to_draw = 0;
					// fall through
				case MODE_DRAW:
					num_to_draw++;
					break;
				}
				draw_mode = MODE_DRAW;
			}
			source_offset++;
		}
		if ( draw_mode == MODE_DRAW )	{
			move_and_count( draw_start_source-esi, draw_start_dest-edi, num_to_draw );
			esi = draw_start_source + num_to_draw;
			edi = draw_start_dest + num_to_draw;
		}
		draw_mode = MODE_NONE;
		source_offset += (srowsize - sw);
	}
	Code_counter++;		// for return

	//printf( "Code will be %d bytes\n", Code_counter );

	Code_counter += 16;	// for safety	 was 16

	return Code_counter;
}

int gr_ibitblt_find_code_size( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
	return gr_ibitblt_find_code_size_sub( mask_bmp, sx, sy, sw, sh, srowsize, BM_LINEAR );
}

int gr_ibitblt_find_code_size_svga( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
	return gr_ibitblt_find_code_size_sub( mask_bmp, sx, sy, sw, sh, srowsize, BM_SVGA );
}

//-----------------------------------------------------------------------------------------
// Given bitmap, bmp, create code that transfers a bitmap of size sw*sh to position
// (sx,sy) on top of bmp, only overwritting transparent pixels of the bitmap.

ubyte	*gr_ibitblt_create_mask_sub( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize, int dest_type )
{
	int x,y;
	ubyte pixel;
	int draw_mode = MODE_NONE;
	int source_offset = 0;
	int dest_offset = 0;
	int num_to_draw, draw_start_source, draw_start_dest;
	int esi, edi;
	int code_size;
	ubyte *code;
	uint temp;

	Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );

	if ( dest_type == BM_SVGA )
		code_size = gr_ibitblt_find_code_size_svga( mask_bmp, sx, sy, sw, sh, srowsize );
	else
		code_size = gr_ibitblt_find_code_size( mask_bmp, sx, sy, sw, sh, srowsize );

	code = malloc( code_size );
	if ( code == NULL )
		return NULL;

	Code_pointer = code;

	if ( dest_type == BM_SVGA )	{
		// MOV EBX, gr_vesa_setpage
		*Code_pointer++ = OPCODE_MOV_EBX;
		temp = (uint)gr_vesa_setpage;
		memcpy( Code_pointer, &temp, sizeof(int) );
		Code_pointer += sizeof(int);
		// MOV EAX, 0
		*Code_pointer++ = OPCODE_MOV_EAX;
		temp = 0;
		memcpy( Code_pointer, &temp, sizeof(int) );
		Code_pointer += sizeof(int);
		// CALL EBX
		*Code_pointer++ = OPCODE_CALL_EBX1;
		*Code_pointer++ = OPCODE_CALL_EBX2;

		ibitblt_svga_page = 0;
		is_svga = 1;
		linear_address = 0;
	} else {
		is_svga = 0;
	}
	esi = source_offset = 0;
	edi = dest_offset = 0;
	draw_start_source = draw_start_dest = 0;

	for ( y=sy; y<sy+sh; y++ )	{
		for ( x=sx; x<sx+sw; x++ )	{
			dest_offset = y*mask_bmp->bm_rowsize+x;
			pixel = mask_bmp->bm_data[dest_offset];
			if ( pixel!=255 )	{
				switch ( draw_mode)	{
				case MODE_DRAW:
					move_and_draw( draw_start_source-esi, draw_start_dest-edi, num_to_draw );
					esi = draw_start_source + num_to_draw;
					edi = draw_start_dest + num_to_draw;
					// fall through!!!
				case MODE_NONE:
				case MODE_SKIP:
					break;
				}
				draw_mode = MODE_SKIP;
			} else {
				switch ( draw_mode)	{
				case MODE_SKIP:
				case MODE_NONE:
					draw_start_source = source_offset;
					draw_start_dest = dest_offset;
					num_to_draw = 0;
					// fall through
				case MODE_DRAW:
					num_to_draw++;
					break;
				}
				draw_mode = MODE_DRAW;
			}
			source_offset++;
		}
		if ( draw_mode == MODE_DRAW )	{
			move_and_draw( draw_start_source-esi, draw_start_dest-edi, num_to_draw );
			esi = draw_start_source + num_to_draw;
			edi = draw_start_dest + num_to_draw;
		}
		draw_mode = MODE_NONE;
		source_offset += (srowsize - sw);
	}
	*Code_pointer++ = OPCODE_RET;

	if ( Code_pointer >= &code[code_size-1] )
		Error( "ibitblt overwrote allocated code block\n" );

	//printf( "Code is %d bytes\n", Code_pointer - code );

	return code;
}

ubyte	*gr_ibitblt_create_mask( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
	return gr_ibitblt_create_mask_sub( mask_bmp, sx, sy, sw, sh, srowsize, BM_LINEAR );
}

#if defined(POLY_ACC)
ulong *pa_emit_blit(int gencode, ulong *buf, int w, int h, int sx, int sy, int dx, int dy)
{
    if(w == 0 || h == 0)
        return buf;

    if(gencode)
    {
        buf[0] = (w << 16)  | h;
        buf[1] = (sx << 16) | sy;
        buf[2] = (dx << 16) | dy;
    }
    return buf + 3;
}

ubyte   *gr_ibitblt_create_mask_pa( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
    ulong *ret, *code = 0;
    int pass, x, y, n;
    ushort *s;

    Assert(mask_bmp->bm_type == BM_LINEAR15);

    srowsize /= PA_BPP;

    pa_flush();

    // make two passes, first pass gets size of output block, second actually creates data.
    for(pass = 0; pass != 2; ++pass)
    {
        for (y = sy; y < sy + sh; y++ )
        {
            // first byte of interest in mask
            s = (ushort *)(mask_bmp->bm_data + y * mask_bmp->bm_rowsize + sx * PA_BPP);
            for ( x=0; x < sw; )
            {
                for(; x != sw && (s[x] & 0x8000); ++x)              // while opaque...
                        ;
                for(n = 0; x != sw && !(s[x] & 0x8000); ++n, ++x)   // while transparent...
                        ;
                code = pa_emit_blit(pass, code, n, 1, x - n, y - sy, x + sx - n, y);
            }
        }
        if(pass == 0)
       {
            ret = malloc((int)code + sizeof(ulong));
            ret[0] = (int)code / sizeof(ulong);        // store num ulongs in list.
            code = ret + 1;
        }
    }
    return (ubyte *)ret;
}

#else
ubyte   *gr_ibitblt_create_mask_svga( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
	return gr_ibitblt_create_mask_sub( mask_bmp, sx, sy, sw, sh, srowsize, BM_SVGA );
}
#endif


void gr_ibitblt_do_asm(char *start_si, char *start_di, ubyte * code);
#pragma aux gr_ibitblt_do_asm parm [esi] [edi] [eax] modify [ecx edi esi eax] = \
	"pusha"	\
	"cld"		\
	"call	eax"	\
	"popa"


void gr_ibitblt(grs_bitmap * source_bmp, grs_bitmap * dest_bmp, ubyte * mask )
{
#if defined(POLY_ACC)
    Assert(source_bmp->bm_type == BM_LINEAR15);
    pa_ibitblt(source_bmp->bm_data, dest_bmp->bm_data, mask);
#else
	if (mask != NULL )
		gr_ibitblt_do_asm( source_bmp->bm_data, dest_bmp->bm_data, mask );
#endif
}


void	gr_ibitblt_find_hole_size( grs_bitmap * mask_bmp, int *minx, int *miny, int *maxx, int *maxy )
{
    int x, y, count=0;
#if defined(POLY_ACC)
    short c;
#else
    ubyte c;
#endif

    Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );

#if defined(POLY_ACC)
    Assert(mask_bmp->bm_type == BM_LINEAR15);
    pa_flush();
#endif

    *minx = mask_bmp->bm_w-1;
 	*maxx = 0;
	*miny = mask_bmp->bm_h-1;
	*maxy = 0;

	for ( y=0; y<mask_bmp->bm_h; y++ )
		for ( x=0; x<mask_bmp->bm_w; x++ )	{
#if defined(POLY_ACC)
            c = *(short *)(mask_bmp->bm_data + mask_bmp->bm_rowsize * y + x * PA_BPP);
            if (c >= 0)  {      // hi true means opaque.
#else
            c = mask_bmp->bm_data[mask_bmp->bm_rowsize*y+x];
			if (c == 255 )	{
#endif
                if ( x < *minx ) *minx = x;
				if ( y < *miny ) *miny = y;
				if ( x > *maxx ) *maxx = x;
				if ( y > *maxy ) *maxy = y;
				count++;
			}
		}

	if ( count == 0 )	{
		Error( "Bitmap for ibitblt doesn't have transparency!\n" );
	}
}

#else		// ifdef MACINTOSH

#include "pa_enabl.h"
#include "pstypes.h"
#include "gr.h"
#include "ibitblt.h"
#include "error.h"
#include "mem.h"

#if defined(POLY_ACC)
#include "poly_acc.h"
#endif

#define FIND_START		1
#define FIND_STOP		2

#define MAX_WIDTH				640
#define MAX_SCANLINES			480
#define MAX_HOLES				5

static short start_points[MAX_SCANLINES][MAX_HOLES];
static short hole_length[MAX_SCANLINES][MAX_HOLES];
static double *scanline = NULL;

void gr_ibitblt(grs_bitmap *src_bmp, grs_bitmap *dest_bmp, ubyte pixel_double)
{
	int x, y, sw, sh, srowsize, drowsize, dstart, sy, dy;
	ubyte *src, *dest;
	short *current_hole, *current_hole_length;

// variable setup

	if ( PAEnabled )
		return;

	sw = src_bmp->bm_w;
	sh = src_bmp->bm_h;
	srowsize = src_bmp->bm_rowsize;
	drowsize = dest_bmp->bm_rowsize;
	src = src_bmp->bm_data;
	dest = dest_bmp->bm_data;

	sy = 0;
	while (start_points[sy][0] == -1) {
		sy++;
		dest += drowsize;
	}

 	if (pixel_double) {
		ubyte *scan = (ubyte *)scanline;		// set up for byte processing of scanline

		dy = sy;
		for (y = sy; y < sy + sh; y++) {
			int i, j;

			gr_linear_movsd_double(src, scan, sw*2);
			current_hole = start_points[dy];
			current_hole_length = hole_length[dy];
			for (x = 0; x < MAX_HOLES; x++) {
				if (*current_hole == -1)
					break;
				dstart = *current_hole;
				gr_linear_movsd(&(scan[dstart]), &(dest[dstart]), *current_hole_length);
				current_hole++;
				current_hole_length++;
			}
			dy++;
			dest += drowsize;
			current_hole = start_points[dy];
			current_hole_length = hole_length[dy];
			for (x = 0;x < MAX_HOLES; x++) {
				if (*current_hole == -1)
					break;
				dstart = *current_hole;
				gr_linear_movsd(&(scan[dstart]), &(dest[dstart]), *current_hole_length);
				current_hole++;
				current_hole_length++;
			}
			dy++;
			dest += drowsize;
			src += srowsize;
		}
	} else {
		Assert(sw <= MAX_WIDTH);
		Assert(sh <= MAX_SCANLINES);
		for (y = sy; y < sy + sh; y++) {
			for (x = 0; x < MAX_HOLES; x++) {
				if (start_points[y][x] == -1)
					break;
				dstart = start_points[y][x];
				gr_linear_movsd(&(src[dstart]), &(dest[dstart]), hole_length[y][x]);
			}
			dest += drowsize;
			src += srowsize;
		}
	}
}

#if defined(POLY_ACC)

ulong *pa_emit_blit(int gencode, ulong *buf, int w, int h, int sx, int sy, int dx, int dy)
{
    if(w == 0 || h == 0)
        return buf;

    if(gencode)
    {
        buf[0] = (w << 16)  | h;
        buf[1] = (sx << 16) | sy;
        buf[2] = (dx << 16) | dy;
    }
    return buf + 3;
}

void gr_ibitblt_create_mask_pa( grs_bitmap * mask_bmp, int sx, int sy, int sw, int sh, int srowsize )
{
    ulong *ret, *code = 0;
    int pass, x, y, n;
    ushort *s;

    Assert(mask_bmp->bm_type == BM_LINEAR15);

    srowsize /= PA_BPP;

    pa_flush();

    // make two passes, first pass gets size of output block, second actually creates data.
    for(pass = 0; pass != 2; ++pass)
    {
        for (y = sy; y < sy + sh; y++ )
        {
            // first byte of interest in mask
            s = (ushort *)(mask_bmp->bm_data + y * mask_bmp->bm_rowsize + sx * PA_BPP);
            for ( x=0; x < sw; )
            {
                for(; x != sw && (s[x] & 0x8000); ++x)              // while opaque...
                        ;
                for(n = 0; x != sw && !(s[x] & 0x8000); ++n, ++x)   // while transparent...
                        ;
                code = pa_emit_blit(pass, code, n, 1, x - n, y - sy, x + sx - n, y);
            }
        }
        
        if(pass == 0) {
            ret = malloc((int)code + sizeof(ulong));
            ret[0] = (int)code / sizeof(ulong);        // store num ulongs in list.
            code = ret + 1;
        }
    }
//    return (ubyte *)ret;
}
#endif

void gr_ibitblt_create_mask(grs_bitmap *mask_bmp, int sx, int sy, int sw, int sh, int srowsize)
{
	int x, y, dest_offset;
	ubyte pixel, mode;
	int count = 0;

	Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );

	for (y = 0; y < MAX_SCANLINES; y++) {
		for (x = 0; x < MAX_HOLES; x++) {
			start_points[y][x] = -1;
			hole_length[y][x] = -1;
		}
	}

	for (y = sy; y < sy+sh; y++) {
		count = 0;
		mode = FIND_START;
		for (x = sx; x < sx + sw; x++) {
			if ((mode == FIND_START) && (mask_bmp->bm_data[mask_bmp->bm_rowsize*y+x] == TRANSPARENCY_COLOR)) {
				start_points[y][count] = x;
				mode = FIND_STOP;
			} else if ((mode == FIND_STOP) && (mask_bmp->bm_data[mask_bmp->bm_rowsize*y+x] != TRANSPARENCY_COLOR)) {
				hole_length[y][count] = x - start_points[y][count];
				count++;
				mode = FIND_START;
			}
		}
		if (mode == FIND_STOP) {
			hole_length[y][count] = x - start_points[y][count];
			count++;
		}
		Assert(count <= MAX_HOLES);
	}
}

#if defined(POLY_ACC)

void gr_ibitblt_find_hole_size_pa( grs_bitmap * mask_bmp, int *minx, int *miny, int *maxx, int *maxy )
{
    int x, y, count=0;
    short c;

    Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );

    Assert(mask_bmp->bm_type == BM_LINEAR15);
    while(!pa_idle());

    *minx = mask_bmp->bm_w-1;
 	*maxx = 0;
	*miny = mask_bmp->bm_h-1;
	*maxy = 0;

	for ( y=0; y<mask_bmp->bm_h; y++ )
		for ( x=0; x<mask_bmp->bm_w; x++ )	{
            c = *(short *)(mask_bmp->bm_data + mask_bmp->bm_rowsize * y + x * PA_BPP);
            if (c >= 0)  {      // hi true means opaque.
                if ( x < *minx ) *minx = x;
				if ( y < *miny ) *miny = y;
				if ( x > *maxx ) *maxx = x;
				if ( y > *maxy ) *maxy = y;
				count++;
			}
		}

	if ( count == 0 )	{
		Error( "Bitmap for ibitblt doesn't have transparency!\n" );
	}
}
#endif

void gr_ibitblt_find_hole_size(grs_bitmap *mask_bmp, int *minx, int *miny, int *maxx, int *maxy)
{
	ubyte c;
	int x, y, count = 0;

#if defined(POLY_ACC)
	if ( PAEnabled ) {
		gr_ibitblt_find_hole_size_pa( mask_bmp, minx, miny, maxx, maxy );
		return;
	}
#endif
	
	Assert( (!(mask_bmp->bm_flags&BM_FLAG_RLE)) );
	Assert( mask_bmp->bm_flags&BM_FLAG_TRANSPARENT );

	*minx = mask_bmp->bm_w - 1;
	*maxx = 0;
	*miny = mask_bmp->bm_h - 1;
	*maxy = 0;

	if (scanline == NULL)
		scanline = (double *)malloc(sizeof(double) * (MAX_WIDTH / sizeof(double)));

	for (y = 0; y < mask_bmp->bm_h; y++) {
		for (x = 0; x < mask_bmp->bm_w; x++) {
			c = mask_bmp->bm_data[mask_bmp->bm_rowsize*y+x];
			if (c == TRANSPARENCY_COLOR) {				// don't look for transparancy color here.
				count++;
				if (x < *minx) *minx = x;
				if (y < *miny) *miny = y;
				if (x > *maxx) *maxx = x;
				if (y > *maxy) *maxy = y;
			}
		}
	}
	Assert (count);
}

#endif		// ifdef MACINTOSH
