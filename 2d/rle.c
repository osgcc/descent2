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
static char rcsid[] = "$Id: rle.c 1.27 1996/03/14 10:22:05 matt Exp $";
#pragma on (unreferenced)

#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "pa_enabl.h"                   //$$POLY_ACC
#include "mem.h"
#include "mono.h"


#include "gr.h"
#include "grdef.h"

#if !defined(MACINTOSH)
#include "dpmi.h"
#endif

#include "error.h"
#include "key.h"

#if defined(POLY_ACC)
#include "poly_acc.h"
#endif

//#define RLE_CODE 		0xC0
//#define NOT_RLE_CODE	63

#define RLE_CODE 			0xE0
#define NOT_RLE_CODE		31

ubyte *gr_rle_decode_asm( ubyte * src, ubyte * dest );

#if !defined(MACINTOSH)
#pragma aux gr_rle_decode_asm parm [esi] [edi] value [edi] modify exact [eax ebx ecx edx esi edi] = \
"  cld					"\
"	xor	ecx, ecx		"\		
"	cld					"\		
"	jmp	NextByte		"\																						
"							"\																				
"Unique:					"\																				
"	mov	[edi],al		"\
"	inc	edi			"\		
"							"\																				
"NextByte:				"\																			
"	mov	al,[esi]		"\		
"	inc	esi			"\		
"							"\																		
"	mov	ah, al		"\
"	and	ah, 0xE0    "\
"  cmp	ah, 0xE0		"\
"	jne   Unique		"\		
"							"\																		
"	mov	cl, al		"\		
"	and	cl, 31  		"\		
"	je		done			"\
"							"\												
"	mov	al,[esi]		"\		
"	inc	esi			"\		
"	mov	ah, al		"\
"	shr	ecx,1			"\		
"	rep	stosw			"\		
"	jnc	NextByte		"\
"	mov	[edi],al		"\
"	inc	edi			"\		
"							"\
"	jmp	NextByte		"\	
"							"\				
"done:					";

void gr_rle_decode( ubyte * src, ubyte * dest, int dest_len )
{
	ubyte *dest_end;

	dest_end = gr_rle_decode_asm( src, dest );

	Assert(dest_end-src < dest_len);
}

#else 		// !defined(MACINTOSH)

void gr_rle_decode( ubyte * src, ubyte * dest )
{
	int i;
	ubyte data, count = 0;

	while(1)	{
		data = *src++;
		if ( (data & RLE_CODE) != RLE_CODE ) {
			*dest++ = data;
		} else {
			count = data & NOT_RLE_CODE;
			if (count==0) return;
			data = *src++;
			for (i=0; i<count; i++ )	
				*dest++ = data;
		}
	}
}

#endif

void rle_stosb(char *dest, int len, int color);

#if !defined(MACINTOSH)

#pragma aux rle_stosb = "cld rep	stosb" parm [edi] [ecx] [eax] modify exact [edi ecx];

#else

void rle_stosb(char *dest, int len, int color)
{
	int i;
	for (i=0; i<len; i++ )
		*dest++ = color;
}

#endif

// Given pointer to start of one scanline of rle data, uncompress it to
// dest, from source pixels x1 to x2.
void gr_rle_expand_scanline_masked( ubyte *dest, ubyte *src, int x1, int x2  )
{
	int i = 0;
	ubyte count;
	ubyte color;

	if ( x2 < x1 ) return;

	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE)==RLE_CODE )	{
			count = color & (~RLE_CODE);
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		if ( color != TRANSPARENCY_COLOR )	rle_stosb( dest, count, color );
		return;
	}

	if ( color != TRANSPARENCY_COLOR )	rle_stosb( dest, count, color );
	dest += count;
	i += count;

	while( i <= x2 )		{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == (RLE_CODE) )	{
			count = color & (~RLE_CODE);
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
			if ( color != TRANSPARENCY_COLOR )rle_stosb( dest, count, color );
			i += count;
			dest += count;
		} else {
			count = x2-i+1;
			if ( color != TRANSPARENCY_COLOR )rle_stosb( dest, count, color );
			i += count;
			dest += count;
		}

	}	
}

void gr_rle_expand_scanline( ubyte *dest, ubyte *src, int x1, int x2  )
{
	int i = 0;
	ubyte count;
	ubyte color;

	if ( x2 < x1 ) return;

	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE)==RLE_CODE )	{
			count = color & (~RLE_CODE);
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		rle_stosb( dest, count, color );
		return;
	}

	rle_stosb( dest, count, color );
	dest += count;
	i += count;

	while( i <= x2 )		{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE)==RLE_CODE )	{
			count = color & (~RLE_CODE);
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
			rle_stosb( dest, count, color );
			i += count;
			dest += count;
		} else {
			count = x2-i+1;
			rle_stosb( dest, count, color );
			i += count;
			dest += count;
		}
	}	
}


int gr_rle_encode( int org_size, ubyte *src, ubyte *dest )
{
	int i;
	ubyte c, oc;
	ubyte count;
	ubyte *dest_start;

	dest_start = dest;
	oc = *src++;
	count = 1;

	for (i=1; i<org_size; i++ )	{
		c = *src++;							
		if ( c!=oc )	{
			if ( count )	{
				if ( (count==1) && ((oc & RLE_CODE)!=RLE_CODE) )	{
					*dest++ = oc;
					Assert( oc != RLE_CODE );
				} else {
					count |= RLE_CODE;
					*dest++ = count;
					*dest++ = oc;
				}
			}
			oc = c;
			count = 0;
		}
		count++;
		if ( count == NOT_RLE_CODE )	{
			count |= RLE_CODE;
			*dest++=count;
			*dest++=oc;
			count = 0;
		}
	}
	if (count)	{
		if ( (count==1) && ((oc & RLE_CODE)!=RLE_CODE) )	{
			*dest++ = oc;
			Assert( oc != RLE_CODE );
		} else {
			count |= RLE_CODE;
			*dest++ = count;
			*dest++ = oc;
		}
	}
	*dest++ = RLE_CODE;

	return dest-dest_start;
}


int gr_rle_getsize( int org_size, ubyte *src )
{
	int i;
	ubyte c, oc;
	ubyte count;
	int dest_size=0;

	oc = *src++;
	count = 1;

	for (i=1; i<org_size; i++ )	{
		c = *src++;							
		if ( c!=oc )	{
			if ( count )	{
				if ( (count==1) && ((oc & RLE_CODE)!=RLE_CODE) )	{
					dest_size++;
				} else {
					dest_size++;
					dest_size++;
				}
			}
			oc = c;
			count = 0;
		}
		count++;
		if ( count == NOT_RLE_CODE )	{
			dest_size++;
			dest_size++;
			count = 0;
		}
	}
	if (count)	{
		if ( (count==1) && ((oc & RLE_CODE)!=RLE_CODE) )	{
			dest_size++;
		} else {
			dest_size++;
			dest_size++;
		}
	}
	dest_size++;

	return dest_size;
}

int gr_bitmap_rle_compress( grs_bitmap * bmp )
{
	int y, d1, d;
	int doffset;
	ubyte *rle_data;
	int large_rle = 0;

// first must check to see if this is large bitmap.

	for (y=0; y<bmp->bm_h; y++ )	{
		d1= gr_rle_getsize( bmp->bm_w, &bmp->bm_data[bmp->bm_w*y] );
		if (d1 > 255) {
			large_rle = 1;
			break;
		}
	}

	rle_data=malloc( (bmp->bm_w+1)* bmp->bm_h );
	if (rle_data==NULL) return 0;
	if (!large_rle)
		doffset = 4 + bmp->bm_h;
	else
		doffset = 4 + (2 * bmp->bm_h);		// each row of rle'd bitmap has short instead of byte offset now
		
	for (y=0; y<bmp->bm_h; y++ )	{
		d1= gr_rle_getsize( bmp->bm_w, &bmp->bm_data[bmp->bm_w*y] );
		if ( ((doffset+d1) > bmp->bm_w*bmp->bm_h) || (d1 > (large_rle?32767:255) ) )	{
			free(rle_data);
			return 0;
		}
		d = gr_rle_encode( bmp->bm_w, &bmp->bm_data[bmp->bm_w*y], &rle_data[doffset] );
		Assert( d==d1 );
		doffset	+= d;
		if (large_rle)
			*((short *)&(rle_data[(y*2)+4])) = (short)d;
		else
			rle_data[y+4] = d;
	}
	//mprintf( 0, "Bitmap of size %dx%d, (%d bytes) went down to %d bytes\n", bmp->bm_w, bmp->bm_h, bmp->bm_h*bmp->bm_w, doffset );
	memcpy( 	rle_data, &doffset, 4 );
	memcpy( 	bmp->bm_data, rle_data, doffset );
	free(rle_data);
	bmp->bm_flags |= BM_FLAG_RLE;
	if (large_rle)
		bmp->bm_flags |= BM_FLAG_RLE_BIG;
	return 1;
}

#define MAX_CACHE_BITMAPS 32

typedef struct rle_cache_element {
	grs_bitmap * rle_bitmap;
	ubyte * rle_data;
	grs_bitmap * expanded_bitmap;			
	int last_used;
} rle_cache_element;

int rle_cache_initialized = 0;
int rle_counter = 0;
int rle_next = 0;
rle_cache_element rle_cache[MAX_CACHE_BITMAPS];

int rle_hits = 0;
int rle_misses = 0;

void rle_cache_close()
{
	if (rle_cache_initialized)	{
		int i;
		rle_cache_initialized = 0;
		for (i=0; i<MAX_CACHE_BITMAPS; i++ )	{
			gr_free_bitmap(rle_cache[i].expanded_bitmap);
		}
	}
}

void rle_cache_init()
{
	int i;
	for (i=0; i<MAX_CACHE_BITMAPS; i++ )	{
		rle_cache[i].rle_bitmap = NULL;
		rle_cache[i].expanded_bitmap = gr_create_bitmap( 64, 64 );
		rle_cache[i].last_used = 0;
		Assert( rle_cache[i].expanded_bitmap != NULL );
	}	
	rle_cache_initialized = 1;
	atexit( rle_cache_close );
}

void rle_cache_flush()
{
	int i;
	for (i=0; i<MAX_CACHE_BITMAPS; i++ )	{
		rle_cache[i].rle_bitmap = NULL;
		rle_cache[i].last_used = 0;
	}	
}

void rle_expand_texture_sub( grs_bitmap * bmp, grs_bitmap * rle_temp_bitmap_1 )
{
	unsigned char * dbits;
	unsigned char * sbits;
	int i;
	unsigned char * dbits1;

	sbits = &bmp->bm_data[4 + bmp->bm_h];
	dbits = rle_temp_bitmap_1->bm_data;

	rle_temp_bitmap_1->bm_flags = bmp->bm_flags & (~BM_FLAG_RLE);

	for (i=0; i < bmp->bm_h; i++ )    {
#ifndef MACINTOSH
		dbits1=(unsigned char *)gr_rle_decode_asm( sbits, dbits );
#else
		gr_rle_decode( sbits, dbits );
#endif
		sbits += (int)bmp->bm_data[4+i];
		dbits += bmp->bm_w;
#ifndef MACINTOSH
		Assert( dbits == dbits1 );		// Get John, bogus rle data!
#endif
	}
}

#if defined(POLY_ACC)
grs_bitmap *rle_get_id_sub(grs_bitmap *bmp)
{
	int i;

	for (i=0;i<MAX_CACHE_BITMAPS;i++) {
		if (rle_cache[i].expanded_bitmap == bmp) {
			return rle_cache[i].rle_bitmap;
		}
	}
	return NULL;
}
#endif


grs_bitmap * rle_expand_texture( grs_bitmap * bmp )
{
	int i;
	int lowest_count, lc;
	int least_recently_used;

	if (!rle_cache_initialized) rle_cache_init();

	Assert( !(bmp->bm_flags & BM_FLAG_PAGED_OUT) );

	lc = rle_counter;
	rle_counter++;
	if ( rle_counter < lc )	{
		for (i=0; i<MAX_CACHE_BITMAPS; i++ )	{
			rle_cache[i].rle_bitmap = NULL;
			rle_cache[i].last_used = 0;
		}
	}

//	if (((rle_counter % 100)==1) && (rle_hits+rle_misses > 0))
//		mprintf(( 0, "RLE-CACHE %d%%, H:%d, M:%d\n", (rle_misses*100)/(rle_hits+rle_misses), rle_hits, rle_misses ));

	lowest_count = rle_cache[rle_next].last_used;
	least_recently_used = rle_next;
	rle_next++;
	if ( rle_next >= MAX_CACHE_BITMAPS )
		rle_next = 0;
		
	for (i=0; i<MAX_CACHE_BITMAPS; i++ )	{
		if (rle_cache[i].rle_bitmap == bmp) 	{
			rle_hits++;
			rle_cache[i].last_used = rle_counter;
			return rle_cache[i].expanded_bitmap;
		}
		if ( rle_cache[i].last_used < lowest_count )	{
			lowest_count = rle_cache[i].last_used;
			least_recently_used = i;
		}
	}	

	Assert(bmp->bm_w<=64 && bmp->bm_h<=64);	//dest buffer is 64x64
	rle_misses++;
	rle_expand_texture_sub( bmp, rle_cache[least_recently_used].expanded_bitmap );
	rle_cache[least_recently_used].rle_bitmap = bmp;
	rle_cache[least_recently_used].last_used = rle_counter;
	return rle_cache[least_recently_used].expanded_bitmap;
}


void gr_rle_expand_scanline_generic( grs_bitmap * dest, int dx, int dy, ubyte *src, int x1, int x2  )
{
	int i = 0, j;
	int count;
	ubyte color;

	if ( x2 < x1 ) return;

	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		for ( j=0; j<count; j++ )
			gr_bm_pixel( dest, dx++, dy, color );
		return;
	}

	for ( j=0; j<count; j++ )
		gr_bm_pixel( dest, dx++, dy, color );
	i += count;

	while( i <= x2 )		{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
			for ( j=0; j<count; j++ )
				gr_bm_pixel( dest, dx++, dy, color );
			i += count;
		} else {
			count = x2-i+1;
			for ( j=0; j<count; j++ )
				gr_bm_pixel( dest, dx++, dy, color );
			i += count;
		}
	}	
}

void gr_rle_expand_scanline_generic_masked( grs_bitmap * dest, int dx, int dy, ubyte *src, int x1, int x2  )
{
	int i = 0, j;
	int count;
	ubyte color;

	if ( x2 < x1 ) return;

	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		if (color != TRANSPARENCY_COLOR) {	
			for ( j=0; j<count; j++ )
				gr_bm_pixel( dest, dx++, dy, color );
		}
		return;
	}

	if ( color != TRANSPARENCY_COLOR ) {
		for ( j=0; j<count; j++ )
			gr_bm_pixel( dest, dx++, dy, color );
	} else
		dx += count;
	i += count;

	while( i <= x2 )		{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
			if ( color != TRANSPARENCY_COLOR ) {
				for ( j=0; j<count; j++ )
					gr_bm_pixel( dest, dx++, dy, color );
			} else
				dx += count;
			i += count;
		} else {
			count = x2-i+1;
			if ( color != TRANSPARENCY_COLOR ) {
				for ( j=0; j<count; j++ )
					gr_bm_pixel( dest, dx++, dy, color );
			} else
				dx += count;
			i += count;
		}
	}	
}


void gr_rle_expand_scanline_svga_masked( grs_bitmap * dest, int dx, int dy, ubyte *src, int x1, int x2  )
{
	int i = 0, j;
	int count;
	ubyte color;
	ubyte * vram = (ubyte *)0xA0000;
	int VideoLocation,page,offset;

	if ( x2 < x1 ) return;

	VideoLocation = (unsigned int)dest->bm_data + (dest->bm_rowsize * dy) + dx;
	page    = VideoLocation >> 16;
	offset  = VideoLocation & 0xFFFF;

	gr_vesa_setpage( page );

	if ( (offset + (x2-x1+1)) < 65536 )	{
		// We don't cross a svga page, so blit it fast!
		gr_rle_expand_scanline_masked( &vram[offset], src, x1, x2 );
		return;
	}
	
	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		if (color != TRANSPARENCY_COLOR) {	
			for ( j=0; j<count; j++ )	{
				vram[offset++] = color;
				if ( offset >= 65536 ) {
					offset -= 65536;
					page++;
					gr_vesa_setpage(page);
				}
			}
		}
		return;
	}

	if ( color != TRANSPARENCY_COLOR ) {
		for ( j=0; j<count; j++ )	{
			vram[offset++] = color;
			if ( offset >= 65536 ) {
				offset -= 65536;
				page++;
				gr_vesa_setpage(page);
			}
		}
	} else	{
		offset += count;
		if ( offset >= 65536 ) {
			offset -= 65536;
			page++;
			gr_vesa_setpage(page);
		}
	}
	i += count;

	while( i <= x2 )		{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( (color & RLE_CODE) == RLE_CODE )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
			if ( color != TRANSPARENCY_COLOR ) {
				for ( j=0; j<count; j++ )	{
					vram[offset++] = color;
					if ( offset >= 65536 ) {
						offset -= 65536;
						page++;
						gr_vesa_setpage(page);
					}
				}
			} else	{
				offset += count;
				if ( offset >= 65536 ) {
					offset -= 65536;
					page++;
					gr_vesa_setpage(page);
				}
			}
			i += count;
		} else {
			count = x2-i+1;
			if ( color != TRANSPARENCY_COLOR ) {
				for ( j=0; j<count; j++ )	{
					vram[offset++] = color;
					if ( offset >= 65536 ) {
						offset -= 65536;
						page++;
						gr_vesa_setpage(page);
					}
				}
			} else	{
				offset += count;
				if ( offset >= 65536 ) {
					offset -= 65536;
					page++;
					gr_vesa_setpage(page);
				}
			}
			i += count;
		}
	}	
}

