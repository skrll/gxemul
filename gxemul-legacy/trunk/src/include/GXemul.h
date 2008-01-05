#ifndef GXEMUL_H
#define	GXEMUL_H

/*
 *  Copyright (C) 2007-2008  Anders Gavare.  All rights reserved.
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
 *  $Id: GXemul.h,v 1.12 2008-01-05 13:13:49 debug Exp $
 */

#include "misc.h"

#include "ActionStack.h"
#include "CommandInterpreter.h"
#include "Component.h"
#include "UI.h"


/**
 * \brief The main emulator class.
 *
 * Its main purpose is to run the GUI main loop, or the text terminal
 * main loop.
 *
 * A %GXemul instance basically has the following member variables:
 *
 * <ol>
 *	<li>a tree of components, which make up the full
 *		state of the current emulation setup
 *	<li>an ActionStack, for undo/redo functionality
 *	<li>a CommandInterpreter
 *	<li>a RunState
 * </ol>
 *
 * The implementation file, GXemul.cc, contains both the class implementation
 * and the main() function of the application. The %GXemul class is also
 * responsible for starting execution of unit tests.
 */
class GXemul
{
public:
	enum RunState
	{
		Paused,
		Running,
		Quitting
	};

public:
	/**
	 * \brief Creates a GXemul instance.
	 *
	 * @param bWithGUI      true if the GUI is to be used, false otherwise
	 */
	GXemul(bool bWithGUI);

	/**
	 * \brief Parses command line arguments.
	 *
	 * @param argc for parsing command line options
	 * @param argv for parsing command line options
	 * @return true if options were parsable, false if there was
	 *		some error.
	 */
	bool ParseOptions(int argc, char *argv[]);

	/**
	 * \brief Runs GXemul's main loop.
	 *
	 * This can be either a GUI main loop, or
	 * a text terminal based main loop.
	 *
	 * @return Zero on success, non-zero on error.
	 */
	int Run();

	/**
	 * \brief Gets a reference to the CommandInterpreter.
	 *
	 * @return a reference to the %GXemul instance' CommandInterpreter
	 */
	CommandInterpreter& GetCommandInterpreter();

	/**
	 * \brief Gets a reference to the ActionStack.
	 *
	 * @return a reference to the %GXemul instance' ActionStack
	 */
	ActionStack& GetActionStack();

	/**
	 * \brief Gets a pointer to the %GXemul instance' active UI.
	 *
	 * Note: May return NULL if no UI has been initialized.
	 *
	 * @return A pointer to the UI in use.
	 */
	UI* GetUI();

	/**
	 * \brief Gets a pointer to the root configuration component.
	 *
	 * @return A pointer to the root component. If no configuration tree
	 *	is loaded, then this is at least an empty dummy component.
	 *	(The return value is never NULL.)
	 */
	refcount_ptr<Component> GetRootComponent();

	/**
	 * \brief Sets the RunState.
	 *
	 * @param newState The new RunState.
	 */
	void SetRunState(RunState newState);

	/**
	 * \brief Gets the current RunState.
	 *
	 * @return The current RunState.
	 */
	RunState GetRunState() const;

	/**
	 * \brief Gets the current RunState as a string.
	 *
	 * @return The current RunState, formatted as a string.
	 */
	string GetRunStateAsString() const;


private:
	/**
	 * \brief Prints help message to std::cout.
	 *
	 * @param bLong true if the long help message should be printed,
	 *		false to only print a short message.
	 */
	void PrintUsage(bool bLong) const;

private:
	RunState		m_runState;
	bool			m_bWithGUI;
	bool			m_bRunUnitTests;
	refcount_ptr<UI>	m_ui;
	refcount_ptr<Component>	m_rootComponent;
	ActionStack		m_actionStack;
	CommandInterpreter	m_commandInterpreter;
};

#endif	// GXEMUL_H
