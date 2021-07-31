/*
 * audio_parse.c
 *        
 *
 * Copyright (C) 2006 Marcus Metzler <mocm [at] mocm.de>
 *                    
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef __KERNEL__
#include <stdio.h>
#include <string.h>
#endif

#include "audio_parse2.h"

#define IN_DEBUG 1
#define DEBUG 1

const uint16_t bitrates[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
    }
};

const uint16_t freqs[3] = { 44100, 48000, 32000 };

static uint32_t samples[5] = { 384, 1152, 1152, 1536, 0};

unsigned int ac3_bitrates[32] =
    {32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640,
     0,0,0,0,0,0,0,0,0,0,0,0,0};
static uint8_t ac3half[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 0, 0, 0, 0};
uint32_t ac3_freq[4] = {480, 441, 320, 0};


static const int ac3_channels[8] = {
        2, 1, 2, 3, 3, 4, 4, 5
};

static int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static int aac_channels[8] = {
    0, 1, 2, 3, 4, 5, 6, 8
};




static uint8_t lfeon[8] = {0x10, 0x10, 0x04, 0x04, 0x04, 0x01, 0x04, 0x01};

static inline void *mycopy(uint8_t *target, uint8_t *src, int length)
{
	if (length <= 0) return target;
	return memcpy(target, src, length);

}

static inline int maincopy(audio_frame_t *af, uint8_t * src, int length)
{
	void *ret;

	if (af->mainpos + length > af->mainsize){
#ifdef DEBUG
		fprintf(stderr,"Data (%d) to long for main buffer (%d) \n",length, af->mainsize - af->mainpos);
#endif
		exit(-1);
	}

	ret = mycopy(af->mainbuf + af->mainpos, src, length);
	af->mainpos += length;
	return 0;
}

static inline int synccopy(audio_frame_t *af, uint8_t * src, int length)
{
	void *ret;

	if (af->spos + length > SYNCSIZE){
#ifdef DEBUG
		fprintf(stderr,"Data (%d) to long for syncbuffer (%d) \n",length, SYNCSIZE - af->spos);
#endif
		return -1;
	}

	ret = mycopy(af->syncbuf + af->spos, src, length);
	af->spos += length;
	return 0;
}


static inline void reset_audio( audio_frame_t *af )
{
	af->set = 0;
	af->mainpos = 0;
	af->lastframe = 0;
}

static uint64_t add_pts_audio(uint64_t pts, audio_frame_t *aframe, uint32_t frames)
{
	uint64_t newpts=0;
	uint32_t samp = 0;

	samp = samples[aframe->layer-1];
	if (aframe->type == AAC)
		samp = aframe->mpg25;

	if (!aframe->frequency) {
		aframe->frequency=48000;
#ifdef DEBUG
		fprintf(stderr, "audio_parse: Warning, frequency set to 0!\n");
#endif
	}
	newpts = (pts + (frames * samp * 90000UL)/aframe->frequency); 

	return (newpts&MAXPTS); //33 bits PTS
}

int find_audio_sync(uint8_t *inbuf, long off, int type, int le)
{
	int found = 0;
	int c=0;
	int l;
	uint8_t b1,b2,m2;

	b1 = 0x00;
	b2 = 0x00;
	m2 = 0xFF;
	switch(type){
	case AAC:
		b1 = 0xFF;
		b2 = 0xF0;
		m2 = 0xF0;
		l = AACMIN;
		break;
	case AC3:
		b1 = 0x0B;
		b2 = 0x77;
		l = AC3MIN;
		break;

	case MPEG_AUDIO:
		b1 = 0xFF;
		b2 = 0xF0;
		m2 = 0xF0;
		l = MPEG12MIN;
		break;

	default:
#ifdef IN_DEBUG
		fprintf(stderr,"  Error: Unknown audio format\n");
#endif
		return -1;
	}

	c = off;
	while ( c-off < le){
		uint8_t *b;
		b = inbuf+c;

		switch(found){

		case 0:
			if ( *b == b1) found = 1;
			break;
			
		case 1:
			if ( (*b&m2) == b2){
				if (l+c-1>le){
#ifdef IN_DEBUG
					fprintf(stderr,"  Error: Partial Header found\n");
#endif

					return -2;
				}
				return c-1-off;	
			} else if ( *b != b1) found = 0;
		}
		c++;
	}	
	if (found){
#ifdef IN_DEBUG
		fprintf(stderr,"  Error: Partial header found\n");
#endif
		return -2;
	} else {
#ifdef IN_DEBUG
		fprintf(stderr,"  Error: No header found\n");
#endif
		return -1;
	}
}


static void calculate_mpg_framesize(audio_frame_t *af)
{
	int frame_size;

	frame_size = af->bit_rate/1000;
	if (!frame_size) return;
	switch(af->layer) {
	case 1:
		frame_size = (frame_size * 12000) / af->sample_rate;
		frame_size = (frame_size + af->padding) * 4;
		break;
	case 2:
		frame_size = (frame_size * 144000) / af->sample_rate;
		frame_size += af->padding;
		break;
	default:
	case 3:
		frame_size = (frame_size * 144000) / (af->sample_rate << af->lsf);
		frame_size += af->padding;
		break;
	}
	af->framesize = frame_size;
}



int check_audio_header(uint8_t *inbuf, audio_frame_t *af, long  off, int le)
{
	uint8_t *headr;
	uint8_t frame;
	int fr;
	int half = 0;
	int c=0;
		
	if ( le <= 0) return -2;
	if ( (c = find_audio_sync(inbuf, off, af->type, le)) != 0 ) {
		if (c==-2){
#ifdef IN_DEBUG
			fprintf(stderr,"Incomplete audio header\n");
#endif
			return -2;
		}
#ifdef IN_DEBUG
		fprintf(stderr,"  Error in audio header\n");
#endif
		return -1;
	}

	headr = inbuf+c;

	switch (af->type){
	default: 
#ifdef IN_DEBUG
		fprintf(stderr,"  Error: Unknown audio format\n");
#endif
		return -4;

	case MPEG_AUDIO:
		if (af->padding != ((headr[2] >> 1) & 1)){
			af->padding = (headr[2] >> 1) & 1;
			calculate_mpg_framesize(af);
#ifdef IN_DEBUG
			fprintf(stderr,"padding changed : %d\n",af->padding);
#endif

		}

		if ( af->layer != 4-((headr[1] & 0x06) >> 1) ){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio layer\n");
#endif
			if ( headr[1] == 0xff){
				return -3;
			} else {
				return -3;
			}
		}
		if ( af->bit_rate != 
		     (	af->bit_rate = bitrates[af->lsf][af->layer-1][(headr[2] >> 4 )]*1000)){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio bit rate\n");
#endif
			return -3;
		}
		break;
		
 	case AC3:
		frame = (headr[4]&0x3F);
		if (af->bit_rate != ac3_bitrates[frame>>1]*1000){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio bit rate\n");
#endif
			return -3;
		}
		half = ac3half[15 & (headr[5] >> 3)];
		fr = (headr[4] & 0xc0) >> 6;

		if (fr == 3 ) return -3;

		if (af->frequency != ((ac3_freq[fr] *100) >> half)){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio frequency\n");
#endif
			return -3;
		}
		
		break;

 	case AAC:
		af->framesize = ((headr[3] & 0x03) << 11) | (headr[4] << 3) | (headr[5] >> 5); 
		af->mpg25 = 1024*((headr[6] & 0x03)+1); 
		fr = (headr[2] & 0x3c) >> 2;
		if(af->frequency != aac_sample_rates[fr]){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio frequency\n");
#endif
			return -3;
		}
		frame = ((headr[2] & 0x01) << 2) | ((headr[3] & 0xc0) >> 6);
		if( af->nb_channels != aac_channels[frame]){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong number of channels\n");
#endif
			return -3;
		}
		break;
		
	}
	
	return c;
}


static int get_mpeg12_info(uint8_t *inbuf, audio_frame_t *af, long off, int le) 
{
	int c = 0;
	int fr =0;
	uint8_t *headr,mode;
	int sample_rate_index;
	
	af->set=0;

	if ( (c = find_audio_sync(inbuf, off, MPEG_AUDIO, le)) < 0 ) 
		return c;
	headr = inbuf+c;
	
	af->layer = 4 - ((headr[1] & 0x06) >> 1);
	if (af->layer >3){
#ifdef IN_DEBUG
		fprintf(stderr,"Unknown audio layer\n");
#endif
		return -1;
	}
#ifdef IN_DEBUG
	fprintf(stderr,"Audiostream: layer: %d", af->layer);
#endif
	if (headr[1] & (1<<4)) {
		af->lsf = (headr[1] & (1<<3)) ? 0 : 1;
		af->mpg25 = 0;
#ifdef IN_DEBUG
	fprintf(stderr,"  version: 1");
#endif
	} else {
		af->lsf = 1;
		af->mpg25 = 1;
#ifdef IN_DEBUG
	fprintf(stderr,"  version: 2");
#endif
	}
	/* extract frequency */
	sample_rate_index = (headr[2] >> 2) & 3;
	if (sample_rate_index > 2){
#ifdef IN_DEBUG
		fprintf(stderr,"Unknown sample rate\n");
#endif
		return -1;
	}
	af->sample_rate = freqs[sample_rate_index] >> (af->lsf + af->mpg25);
	

	af->padding = (headr[2] >> 1) & 1;
	
	af->bit_rate = bitrates[af->lsf][af->layer-1][(headr[2] >> 4 )]*1000;

#ifdef IN_DEBUG
	if (af->bit_rate == 0)
		fprintf (stderr,"  Bit rate: free");
	else if (af->bit_rate == 0xf)
		fprintf (stderr,"  BRate: reserved");
	else
		fprintf (stderr,"  BRate: %d kb/s", af->bit_rate/1000);
#endif

	fr = (headr[2] & 0x0c ) >> 2;

	af->frequency = freqs[fr];
	
#ifdef IN_DEBUG
	if ( fr == 3)
		fprintf (stderr, "  Freq: reserved");
	else
		fprintf (stderr,"  Freq: %2.1f kHz", 
			 af->frequency/1000.);
#endif

	af->set = 1;

	calculate_mpg_framesize(af);

	if (!af->framesize){
#ifdef IN_DEBUG
		fprintf(stderr,"Unknown frame size\n");
#endif
		return -1;
	}
#ifdef IN_DEBUG
	fprintf(stderr," frame size: %d  padding: %d", af->framesize, af->padding);
#endif
       mode = (headr[3] >> 6) & 3;

       if ( mode == MPA_MONO){
	       af->nb_channels = 1;
       } else {
	       af->nb_channels = 2;
       }

#ifdef IN_DEBUG
	fprintf(stderr," channels: %d \n", af->nb_channels);
#endif
	return c;
}

static int get_ac3_info(uint8_t *inbuf, audio_frame_t *af, long off, int le)
{
	int c=0;
	uint8_t *headr;
	uint8_t frame;
	int half = 0;
	int fr;
	int flags, acmod;

	af->set=0;

	if ((c = find_audio_sync(inbuf, off, AC3, le)) < 0 ) 
		return c;

	headr = inbuf+c;

	af->layer = 4;  // 4 for AC3
#ifdef IN_DEBUG
	fprintf (stderr,"AC3 stream:");
#endif
	frame = (headr[4]&0x3F);
	af->bit_rate = ac3_bitrates[frame>>1]*1000;
	if (!af->bit_rate ){
#ifdef IN_DEBUG
		fprintf (stderr," unknown bit rate");
#endif
		return -1;
	}
	half = ac3half[15 & (headr[5] >> 3)];
#ifdef IN_DEBUG
	fprintf (stderr,"  bit rate: %d kb/s", af->bit_rate/1000);
#endif
	fr = (headr[4] & 0xc0) >> 6;
	if (fr >2 ){
#ifdef IN_DEBUG
		fprintf (stderr," unknown frequency");
#endif
		return -1;
	}
	af->frequency = (ac3_freq[fr] *100) >> half;

#ifdef IN_DEBUG
	fprintf (stderr,"  freq: %d Hz\n", af->frequency);
#endif

	switch (headr[4] & 0xc0) {
	case 0:
		af->framesize = 4 * af->bit_rate/1000;
		break;

	case 0x40:
		af->framesize = 2 * (320 * af->bit_rate / 147000 + (frame & 1));
		break;

	case 0x80:
		af->framesize = 6 * af->bit_rate/1000;
		break;
	}

	if (!af->framesize){
#ifdef IN_DEBUG
		fprintf (stderr," unknown frame size");
#endif
		return -1;
	}
#ifdef IN_DEBUG
	fprintf (stderr,"  frame size %d ", af->framesize);
#endif

	acmod = headr[6] >> 5;
	flags = ((((headr[6] & 0xf8) == 0x50) ? A52_DOLBY : acmod) |
		 ((headr[6] & lfeon[acmod]) ? A52_LFE : 0));
	af->nb_channels = ac3_channels[ flags & 7];
	if ( flags & A52_LFE)
		af->nb_channels++;

#ifdef IN_DEBUG
	fprintf(stderr," channels: %d \n", af->nb_channels);
#endif


	af->set = 1;
	return c;
}



static int get_aac_info(uint8_t *inbuf, audio_frame_t *af, long off, int le)
{

	int c=0;
	uint8_t *headr;
	uint8_t sr=0;
	uint8_t rdb=0;
	uint8_t chan=0;

	af->set=0;

	if ((c = find_audio_sync(inbuf, off, AAC, le)) < 0 ) 
		return c;

	headr = inbuf+c;
	af->layer = 5; // 5 for AAC

#ifdef IN_DEBUG
	fprintf (stderr,"AAC stream:");
#endif

	sr = (headr[2] & 0x3c) >> 2;
	if(!(af->frequency = aac_sample_rates[sr]))
		return -1;
	
#ifdef IN_DEBUG
	fprintf (stderr,"  freq: %d Hz ", af->frequency);
#endif

	chan = ((headr[2] & 0x01) << 2) | ((headr[3] & 0xc0) >> 6);
	if(!( af->nb_channels = aac_channels[chan]))
		return -1;

#ifdef IN_DEBUG
	fprintf(stderr," channels: %d ", af->nb_channels);
#endif

	af->framesize = ((headr[3] & 0x03) << 11) | (headr[4] << 3) | (headr[5] >> 5); 

#ifdef IN_DEBUG
	fprintf (stderr,"  frame size %d ", (int)af->framesize);
#endif
	rdb = (headr[6] & 0x03); 
	af->mpg25 = 1024*(rdb+1); // safe somewhere

	af->bit_rate = af->framesize * 8 * af->frequency/(1024 *(1+rdb)) ;

#ifdef IN_DEBUG
	fprintf (stderr,"  BRate: %d kb/s\n", (int)af->bit_rate/1000);
#endif

	af->set=1;
	return c;
}





static int find_first_sync(uint8_t *inbuf, int le,
		    audio_frame_t *af, int off)
{
	int c = 0;
	int minb = 0;

	int (*get_info)(uint8_t *inb, audio_frame_t *af, long of, int l)=NULL;
	
	
	switch (af->type){
	default: 
#ifdef IN_DEBUG
		fprintf (stderr,"Error: unknown audio format");
#endif
		return -1;
	case MPEG_AUDIO:
		minb = MPEG12MIN;
		get_info = get_mpeg12_info;
		break;

	case AC3:
		minb = AC3MIN;
		get_info = get_ac3_info;
		break;

	case AAC:
		minb = AACMIN;
		get_info = get_aac_info;
		break;
	}
	af->minb = minb;
	
	if (!af->spos){
		c = get_info(inbuf, af, 0, le);
		switch(c){
		case -1:
			return c;
			break;
			
		case -2: //need at least minb-1 bytes from last buffer
			if (le >= minb-1){
				synccopy (af, inbuf+le-minb-1, minb-1);
			} else {
				synccopy (af, inbuf, le);
			}
#ifdef IN_DEBUG
			fprintf (stderr,"not enough data\n");
#endif

			return -1;
			break;
		default:
			return c;
		}
	} else {
		int l = minb - af->spos;
		if ( le >= l){
			synccopy(af, inbuf, l);
			c = get_info(af->syncbuf, af, 0, af->spos);
		} else {
			synccopy (af, inbuf, le);
			c = -1;
		}
		return c;
	}
#ifdef IN_DEBUG
	fprintf (stderr,"Warning: No sync byte found");
#endif
	return -1;
}


int audio_parse(uint8_t *buf, int olen, audio_frame_t *af, uint64_t pts)
{
	int c=0;
	int msize = af->mainsize- af->mainpos;
	int flen = 0;
	int minb = 0;
	int fdiff;
	int len = olen;

	if (! (pts & NOPTS) ) af->nextpts = pts;

	if (!af->set){
		c = find_first_sync(buf, len, af, 0);

		if (c < 0){
#ifdef DEBUG
		  fprintf(stdout,"  WARNING Can't find sync\n");
#endif			
		  return c;
		}
		if (af->config)
			if (af->config(af)<0) {
				af->set=0;
#ifdef IN_DEBUG
				fprintf (stderr,"Warning: audio format not configured\n");
#endif

				return -1;
			}

		flen = af->framesize;
		if (msize < flen){
#ifdef IN_DEBUG
			fprintf (stderr,"Warning: frame longer than data\n");
#endif
			return -3;
		}
		af->lastframe = 0;
		if ( pts & NOPTS ) af->nextpts = 0;
		
		if (af->spos){
			int l = af->spos-c;
			if (l<=0){
#ifdef IN_DEBUG
				fprintf (stderr,"What?\n");
#endif
				return -1;
			}
			maincopy(af, af->syncbuf+c,l);
			len -= l;
			c=0;
			af->spos = 0;
		}else len -= c;
	}

	if ( af->lastpts & NOPTS) af->lastpts = af->nextpts;

	minb = af->minb; 

	while ( len ){

		fdiff = af->mainpos - af->lastframe;
	

// check audio header of next frame (also check for mpeg padding)

		if (fdiff < minb){
			if (len > minb){
				if (c < olen){
					maincopy(af, buf+c, minb );
				} else {
					reset_audio(af);
#ifdef IN_DEBUG
					fprintf (stderr,"What2?\n");
#endif
					return -1;
				}
				len -= minb;
				c += minb;
				fdiff = af->mainpos - af->lastframe;
			} else {
				if (c < olen){
					maincopy(af, buf+c, len);
				} else {
					reset_audio(af);
#ifdef IN_DEBUG
					fprintf (stderr,"What3?\n");
#endif
					return -1;
				}
				len = 0;
				return 0;
			}
		}

		if (fdiff >= minb){
			int ac=0;
			
			if ( (ac = check_audio_header(af->mainbuf+af->lastframe, 
						      af, 0, fdiff)) ){
#ifdef DEBUG
				fprintf(stdout,"  ERROR %d in frame 0x%0x 0x%0x 0x%0x\n", ac,
						af->mainbuf[af->lastframe], 
						af->mainbuf[af->lastframe+1], 
						af->mainbuf[af->lastframe+2]);
		
#endif			
				switch (ac){

				case -1: // found no audio header
					 // resync
					reset_audio(af);
					return -1;
					break;

				case -2: // found partial header
					break;

				case -3: // found wrong header
					// what now?
					// resync?
					reset_audio(af);
					return -1;
					break;
					
				default: // header in wrong position
					af->lastframe += ac;
					break;
				}
				
				fdiff = af->mainpos - af->lastframe;
			}
			flen = af->framesize;
		}
		
		if ( len > flen - fdiff){
			if (c < olen && flen > fdiff){
				maincopy(af, buf+c, flen -fdiff);
				len -= flen - fdiff;
				c += flen - fdiff;
				fdiff = 0;

				af->callback(af, af->lastframe, 
					     af->framesize, af->lastpts);
				
				af->nextpts = add_pts_audio(af->lastpts, af, 1);
				af->lastpts = af->nextpts;
				if ( len == 0) af->lastpts = NOPTS;
				af->lastframe += af->framesize;  
				if (af->mainsize - af->lastframe < af->framesize){
					memmove(af->mainbuf, af->mainbuf + af->lastframe, af->mainpos -af->lastframe);
					af->mainpos = af->mainpos - af->lastframe;
					af->lastframe = 0;
				}
			} else {
				reset_audio(af);
#ifdef IN_DEBUG
				fprintf (stderr,"What4?\n");
#endif
				return -1;
			}
		} else {
			if (c < olen){
				maincopy(af, buf+c, len);
			} else {
				reset_audio(af);
#ifdef IN_DEBUG
				fprintf (stderr,"What4?\n");
#endif
				return -1;
			}
			len = 0;
		}
	}
	return 0;
}
