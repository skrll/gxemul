/*
 *  Copyright (C) 2003,2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: diskimage.c,v 1.8 2004-04-02 05:47:23 debug Exp $
 *
 *  Disk image support.
 *
 *  TODO:  diskimage_remove() ?
 *         Actually test diskimage_access() to see that it works.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "misc.h"
#include "diskimage.h"


struct diskimage {
	char		*fname;
	off_t		total_size;
	int		writable;

	FILE		*f;
};


#define	MAX_DISKIMAGES		8

static struct diskimage *diskimages[MAX_DISKIMAGES];
static int n_diskimages = 0;


/*
 *  diskimage_exist():
 *
 *  Returns 1 if the specified disk_id exists, 0 otherwise.
 */
int diskimage_exist(int disk_id)
{
	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL)
		return 0;

	return 1;
}


/*
 *  diskimage_getsize():
 *
 *  Returns -1 if the specified disk_id does not exists, otherwise
 *  the size of the disk image is returned.
 */
int64_t diskimage_getsize(int disk_id)
{
	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL)
		return -1;

	return diskimages[disk_id]->total_size;
}


/*
 *  diskimage_scsicommand():
 *
 *  Returns 1 if ok, 0 on error.
 *  If ok, then *return_buf_ptr is set to a newly malloced buffer. It is up to
 *  the caller to free this buffer. *return_len is set to the length.
 */
int diskimage_scsicommand(int disk_id, unsigned char *buf, int len, unsigned char **return_buf_ptr, int *return_len)
{
	unsigned char *retbuf = NULL;
	int retlen = 0;

	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL || len<1)
		return 0;

	debug("[ diskimage_scsicommand(id=%i) cmd=0x%02x: ", disk_id, buf[0]);
	switch (buf[0]) {
	case SCSICMD_TEST_UNIT_READY:
		debug("TEST_UNIT_READY");
		if (len != 6)
			debug(" (weird len=%i)", len);

		/*  TODO: bits 765 of buf[1] contains the LUN  */

		/*  Return status:  */
		retlen = 1;
		retbuf = malloc(retlen);
		if (retbuf == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(retbuf, 0, sizeof(retbuf));
		break;
	case SCSICMD_INQUIRY:
		debug("INQUIRY");
		if (len != 6)
			debug(" (weird len=%i)", len);
		if (buf[1] != 0x00) {
			fatal(" INQUIRY with buf[1]=0x%02x not yet implemented\n");
			exit(1);
		}
		/*  Return values:  */
		retlen = buf[4];
		if (retlen < 36) {
			fatal("WARNING: SCSI inquiry len=%i, <36!\n", retlen);
			retlen = 36;
		}
		retbuf = malloc(retlen);
		if (retbuf == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(retbuf, 0, sizeof(retbuf));
		retbuf[0] = 0x00;	/*  Direct-access disk  */
		retbuf[1] = 0x00;	/*  non-removable  */
		retbuf[2] = 0x02;	/*  SCSI-2  */
		retbuf[6] = 0x04;	/*  ACKREQQ  */
		retbuf[7] = 0x60;	/*  WBus32, WBus16  */
		memcpy(retbuf+8, "VENDORID", 8);
		memcpy(retbuf+16, "PRODUCT ID      ", 16);
		memcpy(retbuf+32, "V0.0", 4);
		break;
	case SCSIBLOCKCMD_READ_CAPACITY:
		fatal("[ SCSI READ_CAPACITY: TODO ]\n");
		break;
	case 0x03:
	case 0x08:
	case 0x15:
	case 0x1a:
	case 0x1b:
		fatal("[ SCSI 0x%02x: TODO ]\n", buf[0]);
		break;
	default:
		fatal("unimplemented SCSI command 0x%02x\n", buf[0]);
		exit(1);
	}
	debug(" ]\n");

	*return_buf_ptr = retbuf;
	*return_len = retlen;
	return 1;
}


/*
 *  diskimage_access():
 *
 *  Read from or write to a disk image.
 *
 *  Returns 1 if the access completed successfully, 0 otherwise.
 */
int diskimage_access(int disk_id, int writeflag, off_t offset, unsigned char *buf, size_t len)
{
	int len_done;

	if (disk_id >= n_diskimages || diskimages[disk_id]==NULL) {
		fatal("trying to access a non-existant disk image (%i)\n", disk_id);
		exit(1);
	}

	fseek(diskimages[disk_id]->f, offset, SEEK_SET);

	if (writeflag) {
		if (!diskimages[disk_id]->writable)
			return 0;

		len_done = fwrite(buf, 1, len, diskimages[disk_id]->f);
	} else {
		len_done = fread(buf, 1, len, diskimages[disk_id]->f);
		if (len_done < len)
			memset(buf + len_done, 0, len-len_done);
	}

	/*  Warn about non-complete data transfers:  */
	if (len_done != len) {
		fatal("diskimage_access(): disk_id %i, transfer not completed. len=%i, len_done=%i\n",
		    disk_id, len, len_done);
		return 0;
	}

	return 1;
}


/*
 *  diskimage_add():
 *
 *  Add a disk image.
 *
 *  Returns an integer >= 0 identifying the disk image.
 */
int diskimage_add(char *fname)
{
	int id;
	FILE *f;

	if (n_diskimages >= MAX_DISKIMAGES) {
		fprintf(stderr, "too many disk images\n");
		exit(1);
	}

	id = n_diskimages;

	diskimages[id] = malloc(sizeof(struct diskimage));
	if (diskimages[id] == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(diskimages[id], 0, sizeof(struct diskimage));
	diskimages[id]->fname = malloc(strlen(fname) + 1);
	if (diskimages[id]->fname == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	strcpy(diskimages[id]->fname, fname);

	/*  Measure total_size:  */
	f = fopen(fname, "r");
	if (f == NULL) {
		perror(fname);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	diskimages[id]->total_size = ftell(f);
	fclose(f);

	diskimages[id]->writable = access(fname, W_OK) == 0? 1 : 0;

	diskimages[id]->f = fopen(fname, diskimages[id]->writable? "r+" : "r");
	if (diskimages[id]->f == NULL) {
		perror(fname);
		exit(1);
	}

	n_diskimages ++;

	return id;
}


/*
 *  diskimage_dump_info():
 *
 *  Debug dump of all diskimages that are loaded.
 *
 *  TODO:  The word 'adding' isn't really correct, as all diskimages
 *         are actually already added when this function is called.
 */
void diskimage_dump_info(void)
{
	int i;

	for (i=0; i<n_diskimages; i++) {
		debug("adding diskimage %i: '%s', %s, %li bytes (%li sectors)\n",
		    i, diskimages[i]->fname,
		    diskimages[i]->writable? "read/write" : "read-only",
		    (long int) diskimages[i]->total_size,
		    (long int) (diskimages[i]->total_size / 512));
	}
}

