/*
 * audio_parse.h
 *        
 *
 * Copyright (C) 2003 Marcus Metzler <mocm [at] mocm.de>
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

#ifndef _AUDIO_PARSE_H_
#define _AUDIO_PARSE_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
//typedef u64 uint64_t;
//typedef u32 uint32_t;
//typedef u16 uint16_t;
//typedef u8  uint8_t;
#else
#include <stdint.h>
#endif

enum {
  NONE=0,AC3, MPEG_AUDIO, AAC, LPCM, UNKNOWN
};

#define MPEG12MIN  5
#define AC3MIN     7
#define AACMIN     8
#define MPA_MONO   3
#define A52_LFE   16
#define A52_DOLBY 10

#define NOPTS    (0x8000000000000000ULL)
#define MAXPTS   (0x00000001FFFFFFFFULL)
#define SYNCSIZE 20

struct audio_frame_s;

typedef int (*audio_callback)(struct audio_frame_s *, int, int, uint64_t);
typedef int (*audio_err_callback)(struct audio_frame_s *, int, int);
typedef int (*audio_config)(struct audio_frame_s *);

typedef
struct audio_frame_s{
	int type;
	int set;
	int layer;
	int padding;
	int sample_rate;
	int mpg25;
	int lsf;
	uint8_t  nb_channels;
	uint32_t bit_rate;
	uint32_t frequency;
	uint32_t emphasis;
	uint32_t framesize;
	uint8_t syncbuf[SYNCSIZE];
	int spos;
	audio_callback callback;
	audio_err_callback err_callback;
	uint8_t *mainbuf;
	int mainpos;
	int mainsize;
	int lastframe;
	uint64_t lastpts;
	uint64_t nextpts;
	int minb;	
	void *priv;
	audio_config config;
} audio_frame_t;

int find_audio_sync(uint8_t *inbuf, long off, int type, int le);
int check_audio_header(uint8_t *inbuf, audio_frame_t *af, long  off, int le);
int audio_parse(uint8_t *buf, int len, audio_frame_t *af, uint64_t pts);

#endif /*_AUDIO_PARSE_H_*/
