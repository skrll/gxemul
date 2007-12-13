/*
 *  Copyright (C) 2003-2007  Anders Gavare.  All rights reserved.
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
 *
 *
 *  $Id: GXemul.cc,v 1.5 2007-12-13 12:30:09 debug Exp $
 */


/*  For Doxygen:  */

/*! \mainpage Source code documentation
 *
 * This is the automatically generated Doxygen documentation, built from
 * comments throughout the source code.
 *
 * GXemul's home page: <a href="http://gavare.se/gxemul/">http://gavare.se/gxemul/</a>
 */

#include <iostream>

#ifdef WITH_GUI
#include <gtkmm.h>
#include "GXemulWindow.h"
#endif

#include "GXemul.h"
#include "misc.h"


char *progname;


/**
 * Checks whether GXemul was launched using the command "gxemul-gui" or not.
 *
 * @returns true if GXemul was launched using the command name "gxemul-gui", otherwise false.
 */
static bool WithGui(const char *progname)
{
	const char *p = strrchr(progname, '/');
	if (p == NULL)
		p = progname;
	else
		p++;

	if (strcmp(p, "gxemul-gui") == 0)
		return true;

	return false;
}


GXemul::GXemul(int argc, char *argv[])
{
	/*  Print startup message:  */
	if (!WithGui(progname)) {
		std::cout << "GXemul "VERSION"    Copyright (C) 2003-2007  Anders"
		    " Gavare\nRead the source code and/or documentation for"
		    " other Copyright messages.\n\n";
	}
}


GXemul::~GXemul()
{
}


/**
 * @return Zero on success, non-zero on error.
 */
int GXemul::Run()
{
	if (WithGui(progname)) {
#ifdef WITH_GUI
		GXemulWindow window;
		Gtk::Main::run(window);
#else
		fprintf(stderr, "Sorry, this installation of GXemul was "
		    "compiled without GUI support.\n");
#endif
		return 0;
	}

	return 0;
}


/*****************************************************************************/

/**
 * Program entry point.
 */
int main(int argc, char *argv[])
{
	progname = argv[0];

#ifdef WITH_GUI
	Gtk::Main main(argc, argv);
#endif

	GXemul gxemul(argc, argv);
	return gxemul.Run();
}

