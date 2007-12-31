/*
 *  Copyright (C) 2003-2008  Anders Gavare.  All rights reserved.
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
 *  $Id: GXemul.cc,v 1.15 2007-12-31 11:50:19 debug Exp $
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
 * See the <a href="../../index.html">main documentation</a> for more
 * information about this version of %GXemul.
 *
 * See GXemul's home page for more information about %GXemul in general:
 * <a href="http://gavare.se/gxemul/">http://gavare.se/gxemul/</a>
 *
 * Most of the source code in %GXemul centers around a few core concepts.
 * An overview of these concepts are given below. Anyone who wishes to
 * delve into the source code should be familiar with them.
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
 * are in fact a tree. A GXemul instance has one such tree. The state of each
 * component is stored within that component. (The root component is a dummy
 * container, which contains zero or more sub-components, but it doesn't
 * actually do anything else.)
 *
 * \subsection undostack_subsec Undo stack of Actions
 *
 * Most actions that the user performs in %GXemul can be seen as reversible.
 * For example, adding a new Component to the configuration tree is a reversible
 * action. In this case, the reverse action is to simply remove that component.
 *
 * By pushing each such Action onto an undo stack, it is possible to implement
 * undo/redo functionality. This is available both via the GUI, and
 * when running via a text-only terminal. If an action is incapable of
 * providing undo information, then the undo stack is automatically cleared when
 * the action is performed.
 *
 * The stack is implemented by the ActionStack class. A GXemul instance
 * has one such stack.
 *
 * \subsection commandinterpreter_subsec Command interpreter
 *
 * A GXemul instance has a CommandInterpreter, which is the part that parses a
 * command and executes it. The %CommandInterpreter can be given a complete
 * command as a string, or it can be given one character (or keypress) at a
 * time. In the later case, the TAB key either completes the word currently
 * being written, or writes out a list of possible completions.
 *
 * When running <tt>gxemul</tt>, the %CommandInterpreter is the UI as seen
 * by the user. When running <tt>gxemul-gui</tt>, the %CommandInterpreter is
 * located in a window. The functionality should be the same in both cases.
 *
 * \subsection refcount_subsec Reference counting
 *
 * All components are owned by the GXemul instance, either as part of the
 * normal tree of components, or owned by an action in the undo stack.
 * Reference counting is implemented for a class T by using the
 * ReferenceCountable helper class, and using refcount_ptr<class T> instead of
 * just T* when pointing to such objects.
 *
 * \subsection unittest_subsec Unit tests
 *
 * Wherever it makes sense, unit tests should be written to make sure
 * that the code is correct. The UnitTest class contains static helper
 * functions for writing unit tests. To add unit tests to a class, the
 * class should be UnitTestable, and in particular, it should implement
 * UnitTestable::RunUnitTests. The exact way tests are performed may
 * differ between different classes, but using the UNITTEST(n,test) macro
 * in src/include/UnitTest.h is preferable.
 *
 * Unit tests are normally executed by <tt>make test</tt>. This is implicitly
 * done when doing <tt>make install</tt> as well. It is recommended to run
 * the configure script with the <tt>--debug</tt> option; this enables Wu
 * Yongwei's new/debug memory leak detector. (It does not seem to work too
 * well with GTKMM, so adding <tt>--without-gtkmm</tt> is also useful.)
 *
 *
 * \section codeguidelines_sec Coding guidelines
 *
 * The most important guideline is:
 *
 * <ul>
 *	<li>Newly written code should be similar to existing code.
 * </ul>
 *
 * But also:
 * <ul>
 *	<li>Avoid using non-portable code constructs. Any external library
 *		dependencies should be optional! In particular, any
 *		GUI code (using gtkmm) should go into src/gui/.
 *	<li>Write <a href="http://en.wikipedia.org/wiki/Doxygen">
 *		Doxygen</a> documentation for everything. Run
 *		<tt>make documentation</tt> often to check that the
 *		documentation is correct. In general, the documentation
 *		should be in the .h file only, not in the .cc file.
 *	<li>Write unit tests whenever it makes sense.
 *	<li>Use <a href="http://en.wikipedia.org/wiki/Hungarian_notation">
 *		Hungarian notation</a> for symbol/variable names sparingly,
 *		e.g. use <tt>m_</tt> for member variables, <tt>p</tt> can
 *		sometimes be used for pointers, etc. but don't overuse
 *		Hungarian notation otherwise.
 *	<li>Keep to 80 columns width, if possible.
 *	<li>Use <tt>string</tt> for strings. This is typedeffed to
 *		<a href="http://www.gtkmm.org/docs/glibmm-2.4/docs/reference/html/classGlib_1_1ustring.html">
 *		<tt>Glib::ustring</tt></a> if it is available (for
 *		<a href="http://en.wikipedia.org/wiki/Unicode">unicode</a>
 *		support), otherwise it is typedeffed to <tt>std::string</tt>.
 *		Do not use <tt>char</tt> for characters, use <tt>stringchar</tt>
 *		instead (typedeffed to <tt>gunichar</tt> if unicode is used).
 *	<li>Use i18n macros for user-visible strings, where it makes sense.
 *	<li>Simple and easy-to-read code is preferable; only optimize after
 *		profiling, when it is known which code paths are in need
 *		of optimization.
 *	<li>Insert uppercase "TODO" if something is unclear at the time of
 *		implementation. Periodically do a <tt>grep -R TODO src</tt>
 *		to hunt down these TODOs and fix them permanently.
 *	<li>Use const references for argument passing, to avoid copying.
 *	<li>All functionality should be available both via text-only
 *		terminals and the GUI. In practice, this means that all GUI
 *		actions should result in commands sent to the
 *		CommandInterpreter.
 * </ul>
 */


/*****************************************************************************/


#include <iostream>

#include "misc.h"

#include "ui/console/ConsoleUI.h"
#ifdef WITH_GTKMM
#include <gtkmm.h>
#include "ui/gtkmm/GtkmmUI.h"
#endif

#include "GXemul.h"
#include "UnitTest.h"

#include <unistd.h>
extern char *optarg;


GXemul::GXemul(bool bWithGUI)
	: m_bWithGUI(bWithGUI)
	, m_bRunUnitTests(false)
	, m_commandInterpreter(this)
{
}


bool GXemul::ParseOptions(int argc, char *argv[])
{
	int ch;
	const char *opts = "W:";

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'W':
			if (string(optarg) == "unittest")
				m_bRunUnitTests = true;
			else {
				PrintUsage(false);
				return false;
			}
			break;
		case 'h':
			PrintUsage(true);
			return false;
		default:
			PrintUsage(false);
			return false;
		}
	}

	return true;
}


static void PrintBanner()
{
	std::cout <<
	    "GXemul "VERSION"      Copyright (C) 2003-2008  Anders Gavare\n\n";
}


void GXemul::PrintUsage(bool bLong) const
{
	PrintBanner();

	std::cout << "Usage: gxemul [options]\n"
		     "   or  gxemul-gui [options]\n\n"
		     "where options may be:\n\n";

	std::cout <<
		"  -h             display help message\n"
#ifndef WITHOUTUNITTESTS
		"  -W unittest    run unit tests\n"
#endif
		"\n";
}


int GXemul::Run()
{
	// Print startup message:
	if (!m_bWithGUI)
		PrintBanner();

	// Run unit tests? Then only run those, and then exit.
	if (m_bRunUnitTests)
		return UnitTest::RunTests();

	// Choose UI...
	if (m_bWithGUI) {
#ifdef WITH_GTKMM
		m_ui = new GtkmmUI(this);
#else
		std::cerr << "Sorry, this installation of GXemul was "
		    "compiled without GUI support.\n";
		return 0;
#endif
	} else {
		m_ui = new ConsoleUI(this);
	}

	// ... and run the main loop:
	m_ui->MainLoop();

	return 0;
}


CommandInterpreter& GXemul::GetCommandInterpreter()
{
	return m_commandInterpreter;
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

#ifdef WITH_GTKMM
	Gtk::Main main(argc, argv);
#endif

	GXemul gxemul(WithGUI(progname));

	if (!gxemul.ParseOptions(argc, argv))
		return 1;

	return gxemul.Run();
}

