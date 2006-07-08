#!/bin/sh
#
#  $Id: test_netbsd_pmax_install.sh,v 1.1 2006-07-08 21:49:08 debug Exp $
#
#  Litet enkelt test f�r att m�ta hur l�ng tid det tar att installera
#  en full NetBSD/pmax 3.0 f�r R3000, utan interaktion.
#  Starta med:
#
#	experiments/test_netbsd_pmax_install.sh
#

rm -f nbsd_pmax.img
dd if=/dev/zero of=nbsd_pmax.img bs=1024 count=1 seek=1900000
time experiments/test_netbsd_pmax_install.expect 

