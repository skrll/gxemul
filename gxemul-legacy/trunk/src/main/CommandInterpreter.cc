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
 *
 *  $Id: CommandInterpreter.cc,v 1.1 2007-12-29 16:18:51 debug Exp $
 */

#include "assert.h"

#include "GXemul.h"
#include "CommandInterpreter.h"


CommandInterpreter::CommandInterpreter(GXemul* owner)
	: m_GXemul(owner)
{
	// It would be bad to run without a working GXemul instance.
	assert(m_GXemul != NULL);
}


void CommandInterpreter::AddKey(char key)
{
	switch (key) {
	case '\b':
		// Backspace removes the last character, if any:
		if (!m_currentCommandString.empty())
			m_currentCommandString.resize(
			    m_currentCommandString.length() - 1);
		break;
	case '\e':
		// Escape key handling: TODO
		std::cerr << "TODO: Escape key handling!\n";
		assert(false);
		break;
	case '\t':
		// Tab completion: TODO
		std::cerr << "TODO: TAB completion!\n";
		assert(false);
		break;
	case '\n':
	case '\r':
		// Newline executes the command, if it is non-empty:
		if (!m_currentCommandString.empty()) {
			RunCommand(m_currentCommandString);
			m_currentCommandString = "";
		}
		break;
	default:
		// Most other keys just add a character to the command:
		m_currentCommandString += key;
	}
}


bool CommandInterpreter::RunCommand(const string& command)
{
	// TODO
	
	return false;
}


const string& CommandInterpreter::GetCurrentCommandBuffer() const
{
	return m_currentCommandString;
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

static void Test_CommandInterpreter_KeyBuffer()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("buffer should initially be empty",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('a');

	UnitTest::Assert("buffer should contain 'a'",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('A');

	UnitTest::Assert("buffer should contain 'aA'",
	    ci.GetCurrentCommandBuffer() == "aA");

	ci.AddKey('\b');

	UnitTest::Assert("buffer should contain 'a' again",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('\b');

	UnitTest::Assert("buffer should now be empty '' again",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('\b');

	UnitTest::Assert("buffer should still be empty",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('a');

	UnitTest::Assert("buffer should contain 'a' again",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('Q');

	UnitTest::Assert("buffer should contain 'aQ'",
	    ci.GetCurrentCommandBuffer() == "aQ");

	ci.AddKey('\n');

	UnitTest::Assert("buffer should be empty after executing '\\n'",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('Z');

	UnitTest::Assert("new command should have been possible",
	    ci.GetCurrentCommandBuffer() == "Z");
}

static void Test_CommandInterpreter_NonExistingCommand()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("nonsense command should fail",
	    ci.RunCommand("nonexistingcommand") == false);
}

static void Test_CommandInterpreter_Simple_Version()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("simple command should succeed",
	    ci.RunCommand("version") == true);
}

int CommandInterpreter::RunUnitTests()
{
	int nrOfFailures = 0;

	// Key and current buffer:
	UNITTEST(nrOfFailures, Test_CommandInterpreter_KeyBuffer);

	// RunCommand:
	UNITTEST(nrOfFailures, Test_CommandInterpreter_NonExistingCommand);
	UNITTEST(nrOfFailures, Test_CommandInterpreter_Simple_Version);

	return nrOfFailures;
}

#endif
