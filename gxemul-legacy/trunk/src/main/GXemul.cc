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
 *  $Id: GXemul.cc,v 1.20 2008-01-12 08:29:56 debug Exp $
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
 * <ul><li><b>NOTE:</b> %GXemul 0.5.0 is a work in progress. Nothing is really
 * emulated yet, I am working on core infrastructural things that are needed
 * first, before delving into actual emulation. If you are interested about
 * the current state of development, please see the
 * <a href="../../TODO.html">TODO</a> file.</ul>
 *
 * See the <a href="../../index.html">main documentation</a> for more
 * information about this version of %GXemul.
 *
 * See GXemul's home page for more information about %GXemul in general:
 * <a href="http://gavare.se/gxemul/">http://gavare.se/gxemul/</a>
 *
 * Some people perhaps get a nice warm fuzzy feeling by knowing how a program
 * starts up. In %GXemul's case, the main() function creates a GXemul instance,
 * and after letting it parse command line options, calls GXemul::Run().
 * This is the main loop. It doesn't really do much, it simply calls the UI's
 * main loop, e.g. ConsoleUI::MainLoop().
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
 * Individual components are implemented in <tt>src/components/</tt>, with
 * header files in <tt>src/include/components/</tt>.
 *
 * \subsection undostack_subsec Actions, and the Undo stack
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
 * It is required that, in order to let the undo/redo functionality work
 * properly, any modification of the component tree (this includes adding or
 * removing components, changing the state of components etc) is done through
 * Actions.
 *
 * The stack is implemented by the ActionStack class. A GXemul instance
 * has one such stack.
 *
 * \subsection commandinterpreter_subsec Command interpreter
 *
 * A GXemul instance has a CommandInterpreter, which is the part that parses a
 * command line, figures out which Command is to be executed, and executes it.
 * The %CommandInterpreter can be given a complete command line as a string, or
 * it can be given one character (or keypress) at a time. In the later case,
 * the TAB key either completes the word currently being written, or writes
 * out a list of possible completions.
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
 * ReferenceCountable helper class, and using refcount_ptr instead of
 * just T* when pointing to such objects.
 *
 * \subsection unittest_subsec Unit tests
 *
 * Wherever it makes sense, unit tests should be written to make sure
 * that the code is correct. The UnitTest class contains static helper
 * functions for writing unit tests, such as UnitTest::Assert. To add unit
 * tests to a class, the class should be UnitTestable, and in particular, it
 * should implement UnitTestable::RunUnitTests by using the
 * UNITTESTS macro. Individual test cases are then called, as static
 * non-member functions, using the UNITTEST(testname) macro.
 *
 * Unit tests are normally executed by <tt>make test</tt>. This is implicitly
 * done when doing <tt>make install</tt> as well, for non-release builds.
 * It is recommended to run the configure script with the <tt>--debug</tt>
 * option; this enables Wu Yongwei's new/debug memory leak detector. (It does
 * not seem to work too well with GTKMM, so adding <tt>--without-gtkmm</tt> is
 * also useful.)
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
 *	<li>Avoid using non-portable code constructs. If possible, avoid
 *		using GNU-specific C++.
 *	<li>Any external library dependencies should be optional! Why?
 *		Because one of the best regression tests for the emulator is
 *		to build and run the emulator inside an emulated guest
 *		operating system. If dependencies need to be installed before
 *		being able to do such a test build, it severely increases the
 *		cost (in time) to do such a regression test.
 *	<li>Write <a href="http://en.wikipedia.org/wiki/Doxygen">
 *		Doxygen</a> documentation for everything.
 *		<ul>
 *			<li>Run <tt>make documentation</tt> often to check
 *				that the generated documentation is correct.
 *			<li>In general, the documentation should be in the .h
 *				file only, not in the .cc file.
 *			<li>Keep the Main page (this page) updated with
 *				clear descriptions of the core concepts.
 *		</ul>
 *	<li>Write unit tests whenever it makes sense.
 *	<li>Use <a href="http://en.wikipedia.org/wiki/Hungarian_notation">
 *		Hungarian notation</a> for symbol/variable names sparingly,
 *		e.g. use <tt>m_</tt> for member variables, <tt>p</tt> can
 *		sometimes be used for pointers, etc. but don't overuse
 *		Hungarian notation otherwise.
 *	<li>Keep to 80 columns width for all source code.
 *	<li>Use <tt>string</tt> for strings. This is typedeffed to
 *		<a href="http://www.gtkmm.org/docs/glibmm-2.4/docs/reference/html/classGlib_1_1ustring.html">
 *		<tt>Glib::ustring</tt></a> if it is available (for
 *		<a href="http://en.wikipedia.org/wiki/Unicode">unicode</a>
 *		support), otherwise it is typedeffed to <tt>std::string</tt>.
 *		Do not use <tt>char</tt> for characters, use <tt>stringchar</tt>
 *		instead (typedeffed to <tt>gunichar</tt> if unicode is used).
 *	<li>Use i18n macros for user-visible strings, where it makes sense.
 *	<li>.cc and .h files should be named exactly what the class name is,
 *		not only for consistency for humans, but also because the
 *		<tt>configure</tt> script relies on this when building a list
 *		of classes that have the UNITTESTS macro in them.
 *	<li>Simple and easy-to-read code is preferable; only optimize after
 *		profiling, when it is known which code paths are in need
 *		of optimization.
 *	<li>Use several different compilers; try to build often on as many
 *		different systems as possible, to spot compatibility issues.
 *	<li>Insert uppercase <tt>TODO</tt> if something is unclear at the time
 *		of implementation. Periodically do a <tt>grep -R TODO src</tt>
 *		to hunt down these TODOs and fix them permanently.
 *	<li>Use const references for argument passing, to avoid copying.
 *	<li>All functionality should be available both via text-only
 *		terminals and the GUI. In practice, this means that all GUI
 *		actions should result in commands sent to the
 *		CommandInterpreter.
 * </ul>
 */


/*****************************************************************************/


#include <assert.h>
#include <iostream>

#include "misc.h"

// UIs:
#include "ui/console/ConsoleUI.h"
#ifdef WITH_GTKMM
#include <gtkmm.h>
#include "ui/gtkmm/GtkmmUI.h"
#endif
#include "ui/nullui/NullUI.h"

#include "GXemul.h"
#include "actions/LoadEmulationAction.h"
#include "components/DummyComponent.h"
#include "UnitTest.h"

#include <unistd.h>

/// For command line parsing using getopt().
extern char *optarg;

/// For command line parsing using getopt().
extern int optind;


GXemul::GXemul(bool bWithGUI)
	: m_runState(Running)
	, m_bWithGUI(bWithGUI)
	, m_bRunUnitTests(false)
	, m_ui(new NullUI(this))
	, m_rootComponent(new DummyComponent)
	, m_commandInterpreter(this)
{
	ClearEmulation();
}


void GXemul::ClearEmulation()
{
	m_rootComponent = new DummyComponent;
	m_rootComponent->SetVariable("name", StateVariableValue("root"));
	m_emulationFileName = "";
}


bool GXemul::ParseOptions(int argc, char *argv[])
{
	bool optionsEnoughToStartRunning = false;
	int ch;

	// REMEMBER to keep the following things in synch:
	//	1. The help message.
	//	2. The option parsing in ParseOptions.
	//	3. The man page.

	const char *opts = "hVW:";

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'V':
			SetRunState(Paused);

			// Note: -V allows the user to start the console version
			// of gxemul without an initial emulation setup.
			optionsEnoughToStartRunning = true;
			break;
		case 'W':
			if (string(optarg) == "unittest") {
				m_bRunUnitTests = true;
				optionsEnoughToStartRunning = true;
			} else {
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

	// Any remaining arguments?
	//  1. If a machine template has been selected, then treat the following
	//     arguments as files to load. (Legacy compatibility with previous
	//     versions of GXemul.)
	//  2. Otherwise, treat the argument as a configuration file.
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		// TODO: Machine templates.
		
		// Config file:
		if (argc == 1) {
			string configfileName = argv[0];

			optionsEnoughToStartRunning = true;
			
			refcount_ptr<Action> loadAction =
			    new LoadEmulationAction(*this, configfileName, "");
			GetActionStack().PushActionAndExecute(loadAction);
			
			if (GetRootComponent()->GetChildren().size() == 0) {
				std::cerr << "Failed to load configuration from"
				    " " << argv[1] << ". Aborting.\n";
				return false;
			}
		} else {
			std::cerr << "More than one configfile name supplied?"
			    " Aborting.\n";
			return false;
		}
	}

	// gxemul-gui can always start without an initial emulation:
	if (m_bWithGUI)
		optionsEnoughToStartRunning = true;

	if (optionsEnoughToStartRunning) {
		return true;
	} else {
		PrintUsage(false);
		return false;
	}
}


void GXemul::PrintUsage(bool longUsage) const
{
	if (!longUsage) {
		std::cout << "Insufficient command line arguments given to"
		    " start an emulation. You have\n"
		    "the following alternatives:\n"
		    "\n"
		    "1. Run  gxemul  with a configuration file (.gxemul).\n"
		    "2. Run  gxemul  with machine selection options, which "
		    "creates an emulation\n"
		    "   from a template machine.\n"
		    "3. Run  gxemul -V  with no other options, which causes"
		    " gxemul to be started\n"
		    "   with no emulation loaded at all.\n"
		    "4. Run  gxemul-gui  which starts the GUI"
		    " version of GXemul.\n"
		    "\n"
		    "Run  gxemul -h  for help on command line options.\n\n";
		return;
	}

	std::cout << "Usage: gxemul [options] [configfile]\n"
		     "   or  gxemul-gui [options] [configfile]\n\n"
		     "where options may be:\n\n";

	// REMEMBER to keep the following things in synch:
	//	1. The help message.
	//	2. The option parsing in ParseOptions.
	//	3. The man page.

	std::cout <<
		"  -h             Displays this help message.\n"
		"  -V             Starts in the Paused state. (Can also be used"
			" to start\n"
		"                 without loading an emulation at all.)\n"
		// -W is undocumented. It is only used internally.
		"\n"
		"and configfile is a file with the .gxemul extension.\n" 
		"\n";
}


int GXemul::Run()
{
	// Run unit tests? Then only run those, and then exit.
	if (m_bRunUnitTests)
		return UnitTest::RunTests();

	// Default to the console UI:
	m_ui = new ConsoleUI(this);

	// But switch to a graphical UI if gxemul-gui was used
	// to launch gxemul:
	if (m_bWithGUI) {
#ifdef WITH_GTKMM
		m_ui = new GtkmmUI(this);
#else
		std::cerr << "Sorry, this installation of GXemul was "
		    "compiled without GUI support.\n";
		return 0;
#endif
	}

	m_ui->Initialize();
	m_ui->ShowStartupBanner();
	m_ui->MainLoop();

	return 0;
}


const string& GXemul::GetEmulationFilename() const
{
	return m_emulationFileName;
}


void GXemul::SetEmulationFilename(const string& filename)
{
	m_emulationFileName = filename;
}


CommandInterpreter& GXemul::GetCommandInterpreter()
{
	return m_commandInterpreter;
}


ActionStack& GXemul::GetActionStack()
{
	return m_actionStack;
}


UI* GXemul::GetUI()
{
	return m_ui;
}


refcount_ptr<Component> GXemul::GetRootComponent()
{
	return m_rootComponent;
}


void GXemul::SetRootComponent(refcount_ptr<Component> newRootComponent)
{
	assert(!newRootComponent.IsNULL());
	m_rootComponent = newRootComponent;
}


void GXemul::SetRunState(RunState newState)
{
	m_runState = newState;
}


GXemul::RunState GXemul::GetRunState() const
{
	return m_runState;
}


string GXemul::GetRunStateAsString() const
{
	switch (m_runState) {
	case Paused:
		return "Paused";
	case Running:
		return "Running";
	case Quitting:
		return "Quitting";
	}

	return "Unknown";
}


/*****************************************************************************/


/**
 * \brief Checks whether GXemul was launched using the command
 *		"gxemul-gui" or not.
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
 * \brief Program entry point.
 */
int main(int argc, char *argv[])
{
	const char *progname = argv[0];

#ifdef WITH_GTKMM
	// Special case: Gtk::Main must be initialized early on, to make
	// things such as unicode locales etc. work correctly.
	Gtk::Main main(argc, argv);
#endif

	GXemul gxemul(WithGUI(progname));

	if (!gxemul.ParseOptions(argc, argv))
		return 1;

	return gxemul.Run();
}

