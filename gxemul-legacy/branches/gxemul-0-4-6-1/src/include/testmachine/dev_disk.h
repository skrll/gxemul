#ifndef	TESTMACHINE_DISK_H
#define	TESTMACHINE_DISK_H

/*
 *  Definitions used by the "disk" device in GXemul.
 *
 *  $Id: dev_disk.h,v 1.2 2006-07-05 05:38:36 debug Exp $
 *  This file is in the public domain.
 */


#define	DEV_DISK_ADDRESS		0x13000000
#define	    DEV_DISK_OFFSET		    0x0000
#define	    DEV_DISK_ID			    0x0010
#define	    DEV_DISK_START_OPERATION	    0x0020
#define	    DEV_DISK_STATUS		    0x0030
#define	    DEV_DISK_BUFFER		    0x4000

#define	    DEV_DISK_BUFFER_LEN		0x200

/*  Operations:  */
#define	DEV_DISK_OPERATION_READ		0
#define	DEV_DISK_OPERATION_WRITE	1


#endif	/*  TESTMACHINE_DISK_H  */
