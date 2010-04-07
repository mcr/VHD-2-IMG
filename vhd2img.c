#if 0
This is a sample of a BSD style license.
--------------------------------------------------------------------
Copyright (c) 2008 Jim Studt
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endif


#if 0

================= What Passes for Documentation ====================

This program converts VHD disk images into a simple file of bytes.
The program is smart enough to not write areas full of zeroes so on
unix the file will have holes and won't take up the full amount of
space. (Typically the converted image will be slightly smaller than
the original.)

If you are wanting a VMDK disk image, then you will want to use the
qemu-img program from the qemu project to covert from the raw format
to VMDK.

You invoke the program with two arguments: input file and output file.

You may not use stdin or stdout as arguments, this program seeks.a

Both "dynamic" and "fixed" VHD files are supported, though only "dynamic"
has been tested.

You could add "differencing" format fairly easily if you had a mind to.

The VHD spec is graciously provided by Microsoft at...
    http://www.microsoft.com/technet/virtualserver/downloads/vhdspec.mspx

This program works on both Linux and Mac OS X, though Mac OS X uses HFS+
as a default filesystem and it doesn't support holes so the output file
will be quite large.

To build, type "make".

Do not fear the code. There are 400 lines in this file, but the code is mostly
error checking and diagnostics.

I will not be updating this program. I got my data converted and I'm done.

#endif

/*
** some defines to make Linux happy with big files
*/
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE /* enable fseeko */

#include <stdio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static int verbose = 1;

struct VHD_footer {
    char cookie[8];
    unsigned long features;
    unsigned long version;
    unsigned long long dataOffset;
    unsigned long timeStamp;
    char creatorApplication[4];
    unsigned long creatorVersion;
    char creatorOS[4];
    unsigned long long originalSize;
    unsigned long long currentSize;
    unsigned short cylinders;
    unsigned char heads;
    unsigned char sectors;
    unsigned long diskType;
    unsigned long checksum;
    unsigned char uniqueId[16];
    unsigned char savedState;
    unsigned char padding[427];
} __attribute__((__packed__));

struct VHD_dynamic {
    char cookie[8];
    unsigned long long dataOffset;
    unsigned long long tableOffset;
    unsigned long headerVersion;
    unsigned long maxTableEntries;
    unsigned long blockSize;
    unsigned long checksum;
    unsigned char parentUniqueId[16];
    unsigned long parentTimeStamp;
    unsigned long reserved1;
    unsigned char parentUnicodeName[512];
    struct {
	unsigned char platformCode[4];
	unsigned long platformDataSpace;
	unsigned long platformDataLength;
	unsigned long reserved;
	unsigned long long platformDataOffset;
    } __attribute__((__packed__)) partentLocator[8];
    unsigned char reserved2[256];
} __attribute__((__packed__));

/*
** network byte order to host, "double" i.e. long long
*/
static unsigned long long ntohd( unsigned long long v)
{
    if ( htons(1) == 1) return v;
    else {
	return (unsigned long long)ntohl( v&0x00000000ffffffff) << 32 | 
	    (unsigned long long)ntohl( (v>>32)&0x00000000ffffffff);
    }
}

static void dump_footer(struct VHD_footer *footer)
{
    int i;

    fprintf(stderr,"==================== VHD Footer =====================\n");
    fprintf(stderr,"%24s : '%8.8s'\n", "cookie", footer->cookie);
    fprintf(stderr,"%24s : %08x\n", "features", ntohl(footer->features));
    fprintf(stderr,"%24s : %08x\n", "version", ntohl(footer->version));
    fprintf(stderr,"%24s : %016llx\n", "data offset", ntohd(footer->dataOffset));
    fprintf(stderr,"%24s : %08x\n", "time stamp", ntohl(footer->timeStamp));
    fprintf(stderr,"%24s : '%4.4s'\n", "creator application", footer->creatorApplication);
    fprintf(stderr,"%24s : %08x\n", "creator version", ntohl(footer->creatorVersion));
    fprintf(stderr,"%24s : '%4.4s'\n", "creator os", footer->creatorOS);
    fprintf(stderr,"%24s : %016llx\n", "original size", ntohd(footer->originalSize));
    fprintf(stderr,"%24s : %016llx\n", "current size", ntohd(footer->currentSize));
    fprintf(stderr,"%24s : %d\n", "cylinders", ntohs(footer->cylinders));
    fprintf(stderr,"%24s : %d\n", "heads", footer->heads);
    fprintf(stderr,"%24s : %d\n", "sectors", footer->sectors);
    fprintf(stderr,"%24s : %08x\n", "disk type", ntohl(footer->diskType));
    fprintf(stderr,"%24s : %08x\n", "disk type", ntohl(footer->checksum));
    fprintf(stderr,"%24s : ", "unique id");
    for ( i = 0; i < 16; i++) {
	fprintf(stderr,"%02x%s", footer->uniqueId[i], "...-.-.-.-......"[i] == '-' ? "-" : "");
    }
    fprintf(stderr,"\n");
    fprintf(stderr,"%24s : %02x\n", "saved state", footer->savedState);
    /* should add the parent locator entries if I ever see a differenced file */
}

static void dump_dynamic(struct VHD_dynamic *dynamic)
{
    int i;

    fprintf(stderr,"==================== VHD Dynamic =====================\n");
    fprintf(stderr,"%24s : '%8.8s'\n", "cookie", dynamic->cookie);
    fprintf(stderr,"%24s : %016llx\n", "data offset", ntohd(dynamic->dataOffset));
    fprintf(stderr,"%24s : %016llx\n", "table offset", ntohd(dynamic->tableOffset));
    fprintf(stderr,"%24s : %08x\n", "version", ntohl(dynamic->headerVersion));
    fprintf(stderr,"%24s : %d\n", "max table entries", ntohl(dynamic->maxTableEntries));
    fprintf(stderr,"%24s : %d\n", "block size", ntohl(dynamic->blockSize));
    fprintf(stderr,"%24s : %08x\n", "checksum", ntohl(dynamic->checksum));
    fprintf(stderr,"%24s : ", "parent unique id");
    for ( i = 0; i < 16; i++) {
	fprintf(stderr,"%02x%s", dynamic->parentUniqueId[i], "...-.-.-.-......"[i] == '-' ? "-" : "");
    }
    fprintf(stderr,"\n");
    fprintf(stderr,"%24s : %08x\n", "parent time stamp", ntohl(dynamic->parentTimeStamp));
    fprintf(stderr,"%24s : %08x\n", "reserved", ntohl(dynamic->reserved1));
    fprintf(stderr,"%24s : ", "parent unicode name");
    for ( i = 0; i < 512; i++) {
	fprintf(stderr,"%02x %s", dynamic->parentUniqueId[i], (i%32)==31 ? "\n" : "");
    }

}

int main( int argc, char **argv)
{
    /*
    ** Make sure we were compiled correctly.
    */
    assert( sizeof(struct VHD_footer)==512);
    assert( sizeof(struct VHD_dynamic)==1024);

    if ( argc != 3) {
	fprintf(stderr,"Usage: vhd2img INPUTFILE OUTPUTFILE\n");
	exit(1);
    }

    FILE *in = fopen(argv[1],"rb");
    if ( !in) {
	fprintf(stderr,"Failed to open input file '%s': %s\n", argv[1], strerror(errno));
	exit(1);
    }

    FILE *out = fopen(argv[2],"wb");
    if ( !out) {
	fprintf(stderr,"Failed to open output file '%s': %s\n", argv[1], strerror(errno));
	exit(1);
    }

    struct VHD_footer footer;
    off_t footerOffset;

    if ( fseeko( in, -512, SEEK_END) != 0) {
	fprintf(stderr,"Failed to seek to the footer: %s\n", strerror(errno));
	exit(1);
    }

    footerOffset = ftello(in);

    if ( fread( &footer, sizeof(footer), 1, in) != 1) {
	fprintf(stderr,"Failed to read footer of input file: %s\n", strerror(errno));
	exit(1);
    }

    if ( memcmp( footer.cookie, "conectix", 8) != 0) {
	fprintf(stderr,"Unknown signature, this is not recognized as a VHD file.\n");
	exit(1);
    }

    if ( verbose) dump_footer(&footer);
    
    switch(htonl(footer.diskType)) {
      case 4:
	fprintf(stderr,"Differencing VHDs not supported.\n");
	exit(1);
      case 2:
	/*
	** Warning: untested code. I don't have one of these files.
	*/
	if ( verbose) fprintf(stderr,"Processing fixed VHD...\n");
	{
	    off_t o = 0;
	    char buf[512];

	    if ( fseeko(in,(off_t)0,SEEK_SET) != 0) {
		fprintf(stderr,"Failed to rewind input file: %s\n", strerror(errno));
		exit(1);
	    }
	    for ( o = 0; o < footerOffset; o += 512) {
		if ( fread( buf, 512, 1, in) != 1) {
		    fprintf(stderr,"Failed to read input file: %s\n", strerror(errno));
		    exit(1);
		}
		if ( fwrite( buf, 512, 1, out) != 1) {
		    fprintf(stderr,"Failed to write output file: %s\n", strerror(errno));
		    exit(1);
		}
	    }
	    fprintf(stderr,"Completed. %lld bytes written.\n", (long long)footerOffset);
	    return 0;
	}
      case 3:
	if ( verbose) fprintf(stderr,"Processing dynamic VHD...\n");
	break;
      default:
	fprintf(stderr,"Disk type %08x not recognized.\n", htonl(footer.diskType));
	exit(1);
    }


    struct VHD_dynamic dynamic;
    
    if ( fseeko(in,ntohd(footer.dataOffset),SEEK_SET) != 0) {
	fprintf(stderr,"Failed to seek to dynamic disk header: %s\n", strerror(errno));
	exit(1);
    }
    
    if ( fread( &dynamic, sizeof(dynamic), 1, in) != 1) {
	fprintf(stderr,"Failed ot read dynamic disk header: %s\n", strerror(errno));
	exit(1);
    }
    
    if ( verbose) dump_dynamic( &dynamic);
    
    unsigned int blockBitmapSectorCount = (ntohl(dynamic.blockSize)/512/8+511)/512;
    unsigned int sectorsPerBlock = ntohl(dynamic.blockSize)/512;
    unsigned char *bitmap = malloc(blockBitmapSectorCount*512);

    if ( verbose) fprintf(stderr, "block bitmap sector count is %d\n", blockBitmapSectorCount);

    if ( fseeko(in,ntohd(dynamic.tableOffset),SEEK_SET) != 0) {
	fprintf(stderr,"Failed to seek to block allocation table: %s\n", strerror(errno));
	exit(1);
    }
    
    unsigned long bats = ntohl( dynamic.maxTableEntries);
    unsigned long *bat = calloc( sizeof(*bat) , bats);
    if ( fread( bat, sizeof(*bat), bats, in) != bats) {
	fprintf(stderr,"Failed to read block allocation table: %s\n", strerror(errno));
	exit(1);
    }
    
    
    if ( fseeko(out, bats * sectorsPerBlock * 512 - 1,SEEK_SET) != 0) {
	fprintf(stderr,"Failed to seek to the end of the image: %s\n", strerror(errno));
	exit(1);
    }
    if ( fwrite("", 1, 1, out) != 1) {
	fprintf(stderr,"Failed to write the last byte of image: %s\n", strerror(errno));
	exit(1);
    }

    unsigned int b;
    unsigned long emptySectors = 0;
    unsigned long usedSectors = 0;
    unsigned long usedZeroes = 0;
    char buf[512];

    for ( b = 0; b < bats; b++) {
	if ( ntohl(bat[b]) == 0xffffffff)  {
	    emptySectors += sectorsPerBlock;
	    continue;      /* totally empty block */
	}

	unsigned long long bo = ntohl(bat[b])*512LL;

	if ( bo > footerOffset) {
	    fprintf(stderr,"Bad block offset\n");
	    exit(1);
	}
	if ( fseeko(in, bo, SEEK_SET) != 0) {
	    fprintf(stderr,"Failed to seek to data block bitmap: %s\n", strerror(errno));
	    exit(1);
	}
	if ( fread( bitmap, 512*blockBitmapSectorCount, 1, in) != 1) {
	    fprintf(stderr,"Failed to read block bitmap(%lld): %s\n", bo, strerror(errno));
	    exit(1);
	}

	unsigned int s,k;
	unsigned long long opos = 0xffffffffffffffffLL;

	if ( fseeko(in, bo+512*blockBitmapSectorCount, SEEK_SET) != 0) {
	    fprintf(stderr,"Failed to seek to input sectors: %s\n", strerror(errno));
	    exit(1);
	}
	for ( s = 0; s < sectorsPerBlock; s++) {
	    if ( fread( buf, 512, 1, in) != 1) {
		fprintf(stderr,"Failed to read sector: %s\n", strerror(errno));
		exit(1);
	    }

	    int empty = 1;
	    for ( k = 0; k < 512; k++) {
		if ( buf[k]) {
		    empty = 0;
		    break;
		}
	    }

	    if ( (bitmap[s/8] & (1<<(7-s%8))) == 0) {
		emptySectors++;
		if ( !empty) {
		    fprintf(stderr,"block %d, sector %d should be empty and isn't.\n", b, s);
		}
	    } else {
		usedSectors++;
		if ( empty) {
		    usedZeroes++;
		} else {
		    unsigned long long pos = (b*sectorsPerBlock + s) * 512LL;
		    if ( pos != opos) {
			if ( fseeko( out, pos, SEEK_SET) != 0) {
			    fprintf(stderr,"Failed to seek output file for sector %lld (block %d of %lu,sector %d of %d): %s\n", 
				    (b*sectorsPerBlock + s) * 512LL, b,bats,s,sectorsPerBlock,
				    strerror(errno));
			    exit(1);
			}
			opos = pos;
		    }
		    if ( fwrite( buf, 512, 1, out) != 1) {
			fprintf(stderr,"Failed to write sector: %s\n", strerror(errno));
			exit(1);
		    }
		    opos += 512LL;
		}
	    }
	}
    }

    if ( verbose) {
	fprintf(stderr,"%4.1f%% of the sectors were used.\n", 
		100.0*usedSectors/(usedSectors+emptySectors));
	fprintf(stderr,"%4.1f%% of the sectors are used after removing zeroes.\n", 
		100.0*(usedSectors-usedZeroes)/(usedSectors+emptySectors));
    }

    return 0;
}

