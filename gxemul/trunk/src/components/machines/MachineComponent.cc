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

#include "components/MachineComponent.h"


MachineComponent::MachineComponent()
	: Component("machine", "machine")
{
}


refcount_ptr<Component> MachineComponent::Create()
{
	return new MachineComponent();
}


string MachineComponent::GetAttribute(const string& attributeName)
{
	if (attributeName == "machine")
		return "yes";

	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "A generic component which acts as a base component"
		    " for all machines.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

#include "ComponentFactory.h"

static void Test_MachineComponent_IsMachine()
{
	UnitTest::Assert("the MachineComponent should be a machine",
	    ComponentFactory::HasAttribute("machine", "machine"));
}

static void Test_MachineComponent_IsStable()
{
	UnitTest::Assert("the MachineComponent should be stable",
	    ComponentFactory::HasAttribute("machine", "stable"));
}

UNITTESTS(MachineComponent)
{
	UNITTEST(Test_MachineComponent_IsMachine);
	UNITTEST(Test_MachineComponent_IsStable);
}

#endif

