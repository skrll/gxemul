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
 *  $Id: CommandInterpreter.cc,v 1.5 2008-01-02 12:39:13 debug Exp $
 */

#include "assert.h"
#include <iostream>

#include "GXemul.h"
#include "CommandInterpreter.h"

// Individual commands:
#include "commands/QuitCommand.h"
#include "commands/VersionCommand.h"


CommandInterpreter::CommandInterpreter(GXemul* owner)
	: m_GXemul(owner)
{
	// It would be bad to run without a working GXemul instance.
	assert(m_GXemul != NULL);

	// Add the default built-in commands:
	AddCommand(new QuitCommand);
	AddCommand(new VersionCommand);
}


void CommandInterpreter::AddCommand(refcount_ptr<Command> command)
{
	m_commands[command->GetCommandName()] = command;
}


void CommandInterpreter::ShowInputLineCharacter(stringchar key)
{
	if (m_GXemul->GetUI() != NULL)
		m_GXemul->GetUI()->ShowInputLineCharacter(key);
}


bool CommandInterpreter::AddKey(stringchar key)
{
	switch (key) {

	case '\0':
		// Add nothing.
		break;

	case '\177':	// ASCII 127 (octal 177) = del
	case '\b':	// backspace
		// Backspace removes the last character, if any:
		if (!m_currentCommandString.empty()) {
			m_currentCommandString.resize(
			    m_currentCommandString.length() - 1);

			ShowInputLineCharacter('\b');
			ShowInputLineCharacter(' ');
			ShowInputLineCharacter('\b');
		}
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
		ShowInputLineCharacter('\n');

		if (!m_currentCommandString.empty()) {
			RunCommand(m_currentCommandString);
			m_currentCommandString = "";
		}
		break;

	default:
		// Most other keys just add a character to the command:
		m_currentCommandString += key;

		ShowInputLineCharacter(key);
	}

	// Return value is true for newline, false otherwise:
	return key == '\n' || key == '\r';
}


void CommandInterpreter::ClearCurrentCommandBuffer()
{
	m_currentCommandString = "";
}


void CommandInterpreter::ReshowCurrentCommandBuffer()
{
	for (size_t i=0; i<m_currentCommandString.length(); i++)
		ShowInputLineCharacter(m_currentCommandString[i]);
}


static void SplitIntoWords(const string& command,
	string& commandName, vector<string>& arguments)
{
	// Split command into words, ignoring all whitespace:
	size_t pos = 0;
	while (pos < command.length()) {
		// Skip initial whitespace:
		while (pos < command.length() && command[pos] == ' ')
			pos ++;
		
		if (pos >= command.length())
			break;
		
		// This is a new word. Add all characters, until
		// whitespace or end of string:
		string newWord = "";
		while (pos < command.length() && command[pos] != ' ') {
			newWord += command[pos];
			pos ++;
		}

		if (commandName.empty())
			commandName = newWord;
		else
			arguments.push_back(newWord);
	}
}


bool CommandInterpreter::RunCommand(const string& command)
{
	string commandName;
	vector<string> arguments;
	SplitIntoWords(command, commandName, arguments);

	Commands::iterator it = m_commands.find(commandName);
	if (it == m_commands.end()) {
		std::cout << commandName <<
		    ": unknown command. Type  help  for help.\n";
		return false;
	}

	(it->second)->Execute(*m_GXemul, arguments);
	
	return true;
}


const string& CommandInterpreter::GetCurrentCommandBuffer() const
{
	return m_currentCommandString;
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

static void Test_CommandInterpreter_AddKey_ReturnValue()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("addkey of regular char should return false",
	    ci.AddKey('a') == false);

	UnitTest::Assert("addkey of nul char should return false",
	    ci.AddKey('\0') == false);

	UnitTest::Assert("addkey of newline should return true",
	    ci.AddKey('\n') == true);

	UnitTest::Assert("addkey of carriage return should return true too",
	    ci.AddKey('\r') == true);
}

static void Test_CommandInterpreter_KeyBuffer()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("buffer should initially be empty",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('a');		// normal char

	UnitTest::Assert("buffer should contain 'a'",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('\0');	// nul char should have no effect

	UnitTest::Assert("buffer should still contain only 'a'",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('A');		// multiple chars
	ci.AddKey('B');
	UnitTest::Assert("buffer should contain 'aAB'",
	    ci.GetCurrentCommandBuffer() == "aAB");

	ci.AddKey('\177');	// del

	UnitTest::Assert("buffer should contain 'aA' (del didn't work?)",
	    ci.GetCurrentCommandBuffer() == "aA");

	ci.AddKey('\b');	// backspace

	UnitTest::Assert("buffer should contain 'a' again (BS didn't work)",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('\b');

	UnitTest::Assert("buffer should now be empty '' again",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('\b');	// cannot be emptier than... well... empty :)

	UnitTest::Assert("buffer should still be empty",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('a');

	UnitTest::Assert("buffer should contain 'a' again",
	    ci.GetCurrentCommandBuffer() == "a");

	ci.AddKey('Q');

	UnitTest::Assert("buffer should contain 'aQ'",
	    ci.GetCurrentCommandBuffer() == "aQ");

	ci.AddKey('\n');	// newline should execute the command

	UnitTest::Assert("buffer should be empty after executing '\\n'",
	    ci.GetCurrentCommandBuffer() == "");

	ci.AddKey('Z');
	ci.AddKey('Q');

	UnitTest::Assert("new command should have been possible",
	    ci.GetCurrentCommandBuffer() == "ZQ");

	ci.AddKey('\r');	// carriage return should work like newline

	UnitTest::Assert("buffer should be empty after executing '\\r'",
	    ci.GetCurrentCommandBuffer() == "");
}

static void Test_CommandInterpreter_NonExistingCommand()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("nonexisting (nonsense) command should fail",
	    ci.RunCommand("nonexistingcommand") == false);
}

static void Test_CommandInterpreter_Simple()
{
	GXemul gxemul(false);
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	UnitTest::Assert("simple command should succeed",
	    ci.RunCommand("version") == true);

	UnitTest::Assert("simple command with whitespace should succeed",
	    ci.RunCommand("   version   ") == true);
}

void CommandInterpreter::RunUnitTests(int& nSucceeded, int& nFailures)
{
	// Key and current buffer:
	UNITTEST(Test_CommandInterpreter_AddKey_ReturnValue);
	UNITTEST(Test_CommandInterpreter_KeyBuffer);

	// RunCommand:
	UNITTEST(Test_CommandInterpreter_NonExistingCommand);
	UNITTEST(Test_CommandInterpreter_Simple);
}

#endif
