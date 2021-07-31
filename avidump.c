/*
 * avidump.c: AVI stream dumper
 *        
 *
 * Copyright (C) 2003 - 2006
 *                    Marcus Metzler <mocm [at] mocm.de>
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

uint32_t getle32(uint8_t *buf)
{
	return (buf[3]<<24)|(buf[2]<<16)|(buf[1]<<8)|buf[0];
}

uint32_t getbe32(uint8_t *buf)
{
	return (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
}

void printhead(uint8_t *buf)
{
	printf("%c%c%c%c ", buf[0], buf[1], buf[2], buf[3]);
}

uint32_t getsize(int fd)
{
	int len;
	uint8_t buf[4];

	len=read(fd, buf, 4);
	return getle32(buf);
}

int avidump(int fd)
{
	int len;
	uint32_t size, head, todo=0;
	uint8_t buf[8];
	int skip=0;

	while ((len=read(fd, buf, 4))==4) {
		head=getbe32(buf);
		printhead(buf);
		
		switch (head) {
		case 0x52494646: //RIFF
			size=getsize(fd);
			printf("%u ", size);
			read(fd, buf, 4);
			printhead(buf);
			todo=size;
			break;

		case 0x4c495354: //LIST
			size=getsize(fd);
			printf("%u ", size);
			todo=size;
			break;

		case 0x6864726c: //hdrl
			break;

		case 0x7374726c: //strl
			break;

		case 0x4a554e4b:  //JUNK
		case 0x73747268: //strh
		case 0x73747266: //strf
		case 0x73747264: //strd
		case 0x7374726e: //strn
			size=getsize(fd);
			printf("%u ", size);
			skip=1;
			break;

		case 0x6d6f7669: //movi
			break;

		case 0x30317762: //01wb
		case 0x30316463: //01dc
		case 0x30306463: //00dc
			size=getsize(fd);
			printf("%u ", size);
			size=(size+3)&~3;
			skip=1;
			break;

		case 0x494e464f: //INFO
			read(fd, buf, 4);
			printhead(buf);
			size=getsize(fd);
			printf("%u ", size);
			skip=1;
			break;

		case 0x61766968: //avih
			size=getsize(fd);
			printf("%u ", size);
			skip=1;
			break;
		}
		printf("\n");
		if (skip)
			lseek(fd, size, SEEK_CUR);
		while (todo) 
			todo-=avidump(fd);
		return size;
	}
}

main(int argc, char **argv)
{
	int fd;

	if (argc<2)
		return -1;

	fd=open(argv[1], O_RDONLY);
	
	if (fd<0)
		return fd;

	avidump(fd);
}
