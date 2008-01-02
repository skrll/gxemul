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
 *  $Id: DummyComponent.cc,v 1.5 2008-01-02 10:56:40 debug Exp $
 */

#include "components/DummyComponent.h"
#include "Checksum.h"


DummyComponent::DummyComponent()
	: Component("dummy")
{
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

static void Test_DummyComponent_CreateComponent()
{
	refcount_ptr<Component> component;

	UnitTest::Assert("creating a dummy component should be possible",
	    Component::CreateComponent("dummy", component) == true);

	UnitTest::Assert("the component should not be null",
	    component.IsNULL() == false);

	UnitTest::Assert("the name should be 'dummy'",
	    component->GetClassName() == "dummy");
}

static void Test_DummyComponent_GetSetParent()
{
	refcount_ptr<Component> dummyA = new DummyComponent;
	refcount_ptr<Component> dummyB = new DummyComponent;

	UnitTest::Assert("parent should initially be NULL",
	    dummyA->GetParent() == NULL);

	dummyA->SetParent(dummyB);

	UnitTest::Assert("parent should be dummyB",
	    dummyA->GetParent() == dummyB);

	dummyA->SetParent(NULL);

	UnitTest::Assert("parent should now be NULL",
	    dummyA->GetParent() == NULL);
}

static void Test_DummyComponent_AddChild_Sets_Parent()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;

	UnitTest::Assert("child A's parent should initially be NULL",
	    dummyChildA->GetParent() == NULL);

	dummy->AddChild(dummyChildA);

	UnitTest::Assert("child A's parent should now be dummy",
	    dummyChildA->GetParent() == dummy);
}

static void Test_DummyComponent_AddChildren_Count()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;
	refcount_ptr<Component> dummyChildB = new DummyComponent;
	refcount_ptr<Component> dummyChildC = new DummyComponent;

	UnitTest::Assert("there should initially be no child components",
	    dummy->GetChildren().size() == 0);

	dummy->AddChild(dummyChildA);
	dummy->AddChild(dummyChildB);
	dummy->AddChild(dummyChildC);

	UnitTest::Assert("there should be 3 child components",
	    dummy->GetChildren().size() == 3);
}

static void Test_DummyComponent_Add_Tree_Of_Children()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;
	refcount_ptr<Component> dummyChildB = new DummyComponent;
	refcount_ptr<Component> dummyChildC = new DummyComponent;

	dummyChildA->AddChild(dummyChildB);
	dummyChildA->AddChild(dummyChildC);
	dummy->AddChild(dummyChildA);

	UnitTest::Assert("there should be 1 child component",
	    dummy->GetChildren().size() == 1);
	UnitTest::Assert("there should be 2 child components in dummyChildA",
	    dummyChildA->GetChildren().size() == 2);
}

static void Test_DummyComponent_RemoveChild()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;
	refcount_ptr<Component> dummyChildB = new DummyComponent;
	refcount_ptr<Component> dummyChildC = new DummyComponent;

	dummyChildA->AddChild(dummyChildB);
	dummyChildA->AddChild(dummyChildC);
	dummy->AddChild(dummyChildA);

	UnitTest::Assert("there should be 1 child component",
	    dummy->GetChildren().size() == 1);
	UnitTest::Assert("child A's parent should be dummy",
	    dummyChildA->GetParent() == dummy);
	UnitTest::Assert("there should be 2 child components in dummyChildA",
	    dummyChildA->GetChildren().size() == 2);

	dummy->RemoveChild(dummyChildA);

	UnitTest::Assert("there should now be 0 child components",
	    dummy->GetChildren().size() == 0);

	UnitTest::Assert(
	    "there should still be 2 child components in dummyChildA",
	    dummyChildA->GetChildren().size() == 2);
	UnitTest::Assert("child A should have no parent",
	    dummyChildA->GetParent() == NULL);
}

static void Test_DummyComponent_GetSet_Variables()
{
	refcount_ptr<Component> dummy = new DummyComponent;

	UnitTest::Assert("variable variablename should not be set yet",
	    dummy->GetVariable("variablename", NULL) == false);

	StateVariableValue value("hello");
	dummy->SetVariable("variablename", value);

	StateVariableValue retrievedValue;

	UnitTest::Assert("variable variablename should be set",
	    dummy->GetVariable("variablename", &retrievedValue) == true);
	UnitTest::Assert("retrieved value should be hello",
	    retrievedValue.ToString() == "hello");

	StateVariableValue value2("hello2");
	dummy->SetVariable("variablename", value2);

	UnitTest::Assert("variable variablename should still be set",
	    dummy->GetVariable("variablename", &retrievedValue) == true);
	UnitTest::Assert("retrieved value should be hello2",
	    retrievedValue.ToString() == "hello2");
}

static void Test_DummyComponent_SerializeDeserialize()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;
	refcount_ptr<Component> dummyChildA1 = new DummyComponent;
	refcount_ptr<Component> dummyChildA2 = new DummyComponent;

	dummy->SetVariable("x", StateVariableValue("value 1"));
	dummy->SetVariable("y", StateVariableValue("value 2"));

	dummyChildA1->SetVariable("x", StateVariableValue("value\nhello"));
	dummyChildA2->SetVariable("something", StateVariableValue());
	dummyChildA2->SetVariable("numericTest", StateVariableValue(123));
	dummyChildA2->SetVariable("numericTest2", StateVariableValue(0));
	dummyChildA2->SetVariable("numericTest3", StateVariableValue(-1));

	dummyChildA->AddChild(dummyChildA1);
	dummyChildA->AddChild(dummyChildA2);
	dummy->AddChild(dummyChildA);

	UnitTest::Assert("serialize/deserialize consistency failure",
	    dummy->CheckConsistency() == true);
}

void DummyComponent::RunUnitTests(int& nSucceeded, int& nFailures)
{
	// Creation using CreateComponent
	UNITTEST(Test_DummyComponent_CreateComponent);

	// Parent tests
	UNITTEST(Test_DummyComponent_GetSetParent);
	
	// Add/Remove children
	UNITTEST(Test_DummyComponent_AddChild_Sets_Parent);
	UNITTEST(Test_DummyComponent_AddChildren_Count);
	UNITTEST(Test_DummyComponent_Add_Tree_Of_Children);
	UNITTEST(Test_DummyComponent_RemoveChild);

	// Get/Set state variables
	UNITTEST(Test_DummyComponent_GetSet_Variables);

	// Serialization/deserialization
	UNITTEST(Test_DummyComponent_SerializeDeserialize);
}

#endif

