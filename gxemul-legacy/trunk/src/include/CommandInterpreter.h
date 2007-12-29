#ifndef COMMANDINTERPRETER_H
#define	COMMANDINTERPRETER_H

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
 *  $Id: CommandInterpreter.h,v 1.1 2007-12-29 16:18:51 debug Exp $
 */

#include "misc.h"

#include "UnitTest.h"


class GXemul;


/**
 * \brief An interactive command interpreter.
 *
 * A command interpreter can execute commands in the form of complete strings,
 * or it can be given one character (keypress) at a time to build up a command.
 * When given individual keypresses, the command interpreter echoes back
 * what is to be printed. It also supports TAB completion.
 */
class CommandInterpreter
	: public UnitTestable
{
public:
	/**
	 * \brief Constructs a %CommandInterpreter.
	 *
	 * @param owner the GXemul instance that owns the %CommandInterpreter
	 */
	CommandInterpreter(GXemul* owner);

	/**
	 * \brief Adds a character (keypress).
	 *
	 * @param key the character/key to add
	 */
	void AddKey(char key);

	/**
	 * \brief Runs a command, given as a string.
	 *
	 * @param command the command to run
	 * @return true if the command was run, false if the command was
	 *	not known
	 */
	bool RunCommand(const string& command);

	/**
	 * \brief Retrieves the current command buffer.
	 *
	 * @return a string representing the current command buffer
	 */
	const string& GetCurrentCommandBuffer() const;


	/********************************************************************/

	static int RunUnitTests();

private:
	GXemul*		m_GXemul;
	string		m_currentCommandString;
};


#endif	// COMMANDINTERPRETER_H
