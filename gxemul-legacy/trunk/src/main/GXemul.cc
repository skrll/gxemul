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
 *  $Id: GXemul.cc,v 1.8 2007-12-17 13:53:32 debug Exp $
 *
 *  This file contains three things:
 *
 *	1. Doxygen documentation for the general design concepts of the
 *	   emulator. The mainpage documentation written here ends up on
 *	   the "main page" in the generated HTML documentation.
 *
 *	2. The GXemul class implementation. This is the main emulator
 *	   object, which either runs the GUI main loop, or a text console
 *	   main loop.
 *
 *	3. The main() entry point.
 */


/*! \mainpage Source code documentation
 *
 * \section intro_sec Introduction
 *
 * This is the automatically generated Doxygen documentation, built from
 * comments throughout the source code.
 *
 * See GXemul's home page for more information about %GXemul in general:
 * <a href="http://gavare.se/gxemul/">http://gavare.se/gxemul/</a>
 *
 *
 * \section concepts_sec Core concepts
 *
 * \subsection components_subsec Components
 *
 * The most important core concept in %GXemul is the Component. Examples of
 * components are processors, networks, video displays, RAM memory, busses,
 * interrupt controllers, and all other kinds of devices.
 *
 * Each component has a parent, so the full set of components in an emulation
 * are in fact a tree. The state of each component is stored within
 * that component. (The root component is a dummy container, which contains
 * zero or more sub-components, but it doesn't actually do anything else.)
 *
 * Reference counting: All components are owned by the GXemul instance, either
 * as part of the normal tree of components, or owned by an action in the
 * undo stack.
 *
 * \subsection undostack_subsec Undo stack
 *
 * Most actions that the user performs in %GXemul can be seen as reversible.
 * For example, adding a new Component to the configuration tree is a reversible
 * action. In this case, the reverse action is to simply remove that component.
 *
 * By pushing each action onto an undo stack, it is possible to implement
 * undo/redo functionality. This is available both via the GUI, and
 * when running via a text-only terminal. If an action is incapable of providing
 * undo information, then the undo stack is cleared when the action is
 * performed.
 *
 *
 * \section codestyle_sec Coding style
 *
 * No specific coding/indentation style is enforced, but there are a few simple
 * guidelines:
 *
 * <ul>
 *	<li>Avoid using non-portable code constructs. Any external library
 *		dependencies should be optional!
 *	<li>Write Doxygen documentation for everything.
 *	<li>Keep to 80 columns width.
 *	<li>Use <tt>string</tt> for strings. This is typedeffed to
 *		<tt>Glib::ustring</tt> if it is available (for unicode
 *		support), otherwise it is typedeffed to <tt>std::string</tt>.
 *	<li>Style, when writing new code, should be similar to existing code.
 * </ul>
 */


/*****************************************************************************/


#include <iostream>

#ifdef WITH_GUI
#include <gtkmm.h>
#include "GXemulWindow.h"
#endif

#include "GXemul.h"
#include "misc.h"


/**
 * Creates a GXemul instance.
 *
 * @param bWithGUI	true if the GUI is to be used, false otherwise
 * @param argc		for parsing command line options
 * @param argv		for parsing command line options
 */
GXemul::GXemul(bool bWithGUI, int argc, char *argv[])
	: m_bWithGUI(bWithGUI)
{
	/*  Print startup message:  */
	if (!m_bWithGUI) {
		std::cout << "GXemul "VERSION
		    "   --   Copyright (C) 2003-2007  Anders"
		    " Gavare\nRead the source code and/or documentation for"
		    " other Copyright messages.\n\n";
	}
}


GXemul::~GXemul()
{
}


/**
 * Run GXemul's main loop.
 *
 * @return Zero on success, non-zero on error.
 */
int GXemul::Run()
{
	if (m_bWithGUI) {
#ifdef WITH_GUI
		GXemulWindow window;
		Gtk::Main::run(window);
#else
		std::cerr << "Sorry, this installation of GXemul was "
		    "compiled without GUI support.\n";
#endif
		return 0;
	}

	return 0;
}


/*****************************************************************************/


/**
 * Checks whether GXemul was launched using the command "gxemul-gui" or not.
 *
 * @param progname argv[0] as seen from main()
 * @return true if GXemul was launched using the command name "gxemul-gui",
 *	   otherwise false
 */
static bool WithGUI(const char *progname)
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


/**
 * Program entry point.
 */
int main(int argc, char *argv[])
{
	const char *progname = argv[0];

#ifdef WITH_GUI
	Gtk::Main main(argc, argv);
#endif

	GXemul gxemul(WithGUI(progname), argc, argv);
	return gxemul.Run();
}

