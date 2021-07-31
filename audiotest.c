#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#ifdef PARSER
#include "audio_parse2.h"
#else
#include "audio_parse.h"
#endif

#define ADAPT_FIELD    0x20
#define PID_MASK_HI    0x1F
#define TS_SIZE 188
#define BUFFY 1024*TS_SIZE

#define MAINSIZE 1024*7

uint8_t mainbuf[MAINSIZE];
int fd=-1;

void printpts(int64_t pts)
{
	if (pts < 0){
		fprintf(stderr,"-");
		pts = -pts;
	}
	pts &= MAXPTS;
	fprintf(stdout,"%2d:%02d:%02d.%03d ",
		(unsigned int)(pts/90000.)/3600,
		((unsigned int)(pts/90000.)%3600)/60,
		((unsigned int)(pts/90000.)%3600)%60,
		(((unsigned int)(pts/90.)%3600000)%60000)%1000
		);
}

int callback(audio_frame_t *af, int start, int len, uint64_t pts)
{
	fprintf(stdout,"start %d  len %d  PTS: ",start, len);
	printpts(pts);
	fprintf(stdout,"\n");
	if (fd>= 0) write(fd,mainbuf+ start, len);
	return 0;
} 

int err_callback(audio_frame_t *af, int errnum, int len)
{
	return errnum;
} 

int find_ts_sync(uint8_t *buf, int len)
{
	int i=0;

	while (i < len && buf[i] != 0x47 ) i++;

	if (i == len) return -1;
	else return i;
}

uint16_t get_pid(uint8_t *pid)
{
	uint16_t pp = 0;

	pp = (pid[0] & PID_MASK_HI)<<8;
	pp |= pid[1];

	return pp;
}

void myexit()
{

	fprintf(stderr,"usage: audiotest [opt] filename\n");
	fprintf(stderr,"     : -o        output filename\n");
	fprintf(stderr,"     : -p        pid if not raw\n");
	fprintf(stderr,"     : -r        raw input not TS\n");
	fprintf(stderr,"     : -t        type of audio (MPEG/AC3/AAC/LPCM\n");
	exit(1);
}

int main(int argc, char **argv)
{
        int c;
	int fd_in;
        char *stype = "MPEG";
	int type = 0;
	uint8_t buf[BUFFY];
	int count = 0;
	audio_frame_t af;
        char *filename = NULL;
	uint16_t pid=0;
	int i=0;
	int rest, packs;
	int brest=0;
	int l=0;
	int raw = 0;

        while (1) {
                int option_index = 0;
                static struct option long_options[] = {
			{"type", required_argument, NULL, 't'},
			{"of",required_argument, NULL, 'o'},
			{"pid",required_argument, NULL, 'p'},
			{"raw",required_argument, NULL, 'r'},
			{0, 0, 0, 0}
		};
                c = getopt_long (argc, argv, 
				 "t:o:p:r",
                                 long_options, &option_index);
                if (c == -1)
                        break;

                switch (c) {
                case 't':
                        stype = optarg;
                        break;
                case 'o':
                        filename = optarg;
                        break;
                case 'p':
                        pid = strtol(optarg,(char **)NULL, 0);
                        break;
                case 'r':
			raw = 1;
                        break;
		}
	}

	if (!raw && !pid) myexit();
	if (filename){
		if ((fd = open(filename,O_WRONLY|O_CREAT
				      |O_TRUNC|O_LARGEFILE,
				      S_IRUSR|S_IWUSR|S_IRGRP|
				      S_IWGRP|
				      S_IROTH|S_IWOTH)) < 0){
			perror("Error opening output file");
			exit(1);
		}
		fprintf(stderr,"Output File is: %s\n", 
			filename);
	}
        if (optind == argc-1) {
                if ((fd_in = open(argv[optind] ,O_RDONLY| O_LARGEFILE)) < 0) {
                        perror("Error opening input file ");
                        exit(1);
                }
                fprintf(stderr,"Reading from %s\n", argv[optind]);
        } else {
		fprintf(stderr,"using stdin as input\n");
		fd_in = STDIN_FILENO;
        }

	type = UNKNOWN;

        if (!strncmp(stype,"MPEG",4))
		type = MPEG_AUDIO;
	else if (!strncmp(stype,"AC3",3))
		type = AC3;
	else if (!strncmp(stype,"LPCM",4))
		type = LPCM;
	else if (!strncmp(stype,"AAC",3))
		type = AAC;

	memset(&af,0,sizeof(af));
	memset(mainbuf, 0,MAINSIZE);
	af.callback = callback;
#ifdef PARSER
	af.err_callback = err_callback;
#endif
	af.type = type;
	af.mainbuf = mainbuf;
	af.mainsize = MAINSIZE;
	af.lastpts = NOPTS;
	af.nextpts = NOPTS;


	if (raw){
		while ( (count = read(fd_in,buf,BUFFY)) > 0 ){
			
			audio_parse(buf,count,&af, NOPTS);
		}
		
		return 0;
	}



	count = read(fd_in,buf,BUFFY);
	fprintf(stderr,"Looking for TS sync\n");
	i = find_ts_sync(buf, count);
	if (i < 0) myexit();
	fprintf(stderr,"found sync\n");


	rest = count - i;
	packs = rest / TS_SIZE;
	
	for (l= 0; l < packs;l++){
		int pos = i+TS_SIZE*l;
		int off = 0;

		if (buf[pos] != 0x47) myexit();
		if ( buf[pos+1]&0x80){
			fprintf(stderr,"Error in TS for PID: %d\n",pid);
			continue;
		}

	
		if ( pid == get_pid(buf+pos+1) ) {
			if ( buf[pos+3] & ADAPT_FIELD) {  // adaptation field?
				off = buf[pos+4] + 1;
				if (off+4 >= TS_SIZE) continue;
			}
			pos += 4+off;
			audio_parse(buf+pos+1, TS_SIZE-4-off, &af,NOPTS);
		}
	}
	
	brest = rest - TS_SIZE*packs; 
	
	if ( brest >0){
		memmove(buf, buf+TS_SIZE*packs, brest);
		
	}

	while ( (count = read(fd_in,buf+brest,BUFFY-brest)) > 0 ){
		rest = count;
		packs = rest / TS_SIZE;
	

		for ( l= 0; l < packs;l++){
			int pos = i+TS_SIZE*l;
			int off = 0;

			if (buf[pos] != 0x47) myexit();

			
			if ( pid == get_pid(buf+pos+1) ) {
				if ( buf[pos+3] & ADAPT_FIELD) {  // adaptation field?
					off = buf[pos+4] + 1;
					if (off+4 >= TS_SIZE) continue;
				}
				pos += 4+off;
				audio_parse(buf+pos+1, TS_SIZE-4-off, &af,NOPTS);
			}
		}
		
		brest = rest - TS_SIZE*packs; 
		
		if ( brest >0){
			memmove(buf, buf+TS_SIZE*packs, brest);
			
		}
		
	}

	return 0;
}
