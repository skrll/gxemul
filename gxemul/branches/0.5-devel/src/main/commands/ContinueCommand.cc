/*
 *  Copyright (C) 2008-2009  Anders Gavare.  All rights reserved.
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

#include "commands/ContinueCommand.h"
#include "GXemul.h"


ContinueCommand::ContinueCommand()
	: Command("continue", "")
{
}


ContinueCommand::~ContinueCommand()
{
}


void ContinueCommand::Execute(GXemul& gxemul, const vector<string>& arguments)
{
	gxemul.GetActionStack().Clear();
	gxemul.SetDirtyFlag(true);
	gxemul.SetRunState(GXemul::Running);
}


string ContinueCommand::GetShortDescription() const
{
	return _("Continues the current emulation.");
}


string ContinueCommand::GetLongDescription() const
{
	return _("Continues the emulation, by setting the "
	    "current RunState to Running.");
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

static void Test_ContinueCommand_Affect_RunState()
{
	refcount_ptr<Command> cmd = new ContinueCommand;
	vector<string> dummyArguments;
	
	GXemul gxemul;

	UnitTest::Assert("the default GXemul instance should be NotRunning",
	    gxemul.GetRunState() == GXemul::NotRunning);

	gxemul.SetRunState(GXemul::Paused);

	UnitTest::Assert("the runstate should now be Paused",
	    gxemul.GetRunState() == GXemul::Paused);

	cmd->Execute(gxemul, dummyArguments);

	UnitTest::Assert("runstate should have been changed to Running",
	    gxemul.GetRunState() == GXemul::Running);
}

static void Test_ContinueCommand_Affect_DirtyFlag()
{
	refcount_ptr<Command> cmd = new ContinueCommand;
	vector<string> dummyArguments;

	GXemul gxemul;

	UnitTest::Assert("the model should not be dirty initially",
	    gxemul.GetDirtyFlag() == false);

	cmd->Execute(gxemul, dummyArguments);

	UnitTest::Assert("the model should be dirty when running",
	    gxemul.GetDirtyFlag() == true);
}

static void Test_ContinueCommand_ClearsActionStack()
{
	GXemul gxemul;
	CommandInterpreter& ci = gxemul.GetCommandInterpreter();

	gxemul.SetRunState(GXemul::Paused);

	ci.RunCommand("add dummy");
	UnitTest::Assert("action stack should contain something",
	    gxemul.GetActionStack().GetNrOfUndoableActions(), 1);

	ci.RunCommand("continue");

	UnitTest::Assert("action stack should have been cleared",
	    gxemul.GetActionStack().GetNrOfUndoableActions(), 0);
}

UNITTESTS(ContinueCommand)
{
	UNITTEST(Test_ContinueCommand_Affect_RunState);
	UNITTEST(Test_ContinueCommand_Affect_DirtyFlag);
	UNITTEST(Test_ContinueCommand_ClearsActionStack);
}

#endif
