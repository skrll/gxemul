#ifndef	GXMVCF_H
#define	GXMVCF_H

/*
 *  Copyright (C) 2008  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 */


#include <sys/types.h>
#include <inttypes.h>

#define	GXMVCF_COPYRIGHT_MSG	"Copyright (C) 2008  Anders Gavare"


// Use Glib::ustring if available, otherwise std::string. Define
// stringchar to be the type of a character.
#ifdef WITH_GTKMM
#include <glibmm/ustring.h>
typedef Glib::ustring string;
typedef gunichar stringchar;
#else   // !WITH_GTKMM
#include <string>
typedef std::string string;
typedef char stringchar;
#endif

#include <iostream>
using std::ostream;

// Use Glib's I18N support if available
#ifdef WITH_GTKMM
#include <glibmm/i18n.h>
#else   // !WITH_GTKMM
#define	_(x)	(x)
#endif

#include <memory>
using std::auto_ptr;

#include <list>
using std::list;

#include <map>
using std::map;
using std::multimap;

#include <set>
using std::set;

#include <sstream>
using std::stringstream;

#include <vector>
using std::vector;

using std::min;
using std::max;
using std::pair;


// Reference counting is needed in lots of places, so it is best to
// include it from this file.
#include "refcount_ptr.h"  


#endif	/*  GXMVCF_H  */
