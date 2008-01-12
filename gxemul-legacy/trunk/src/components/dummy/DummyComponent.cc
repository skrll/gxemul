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
 *  $Id: DummyComponent.cc,v 1.7 2008-01-12 08:29:56 debug Exp $
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

	component = Component::CreateComponent("dummy");
	UnitTest::Assert("creating a dummy component should be possible",
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

static void Test_DummyComponent_AddChild_UniqueName()
{
	refcount_ptr<Component> dummy = new DummyComponent;
	refcount_ptr<Component> dummyChildA = new DummyComponent;
	refcount_ptr<Component> dummyChildB = new DummyComponent;
	refcount_ptr<Component> dummyChildC = new DummyComponent;

	StateVariableValue name;
	UnitTest::Assert("dummyChildA should have no initial name",
	    dummyChildA->GetVariable("name", &name) == false);

	dummy->AddChild(dummyChildA);

	UnitTest::Assert("dummyChildA should have a name now",
	    dummyChildA->GetVariable("name", &name) == true);
	UnitTest::Assert("dummyChildA's name mismatch",
	    name.ToString(), "dummy0");

	dummy->AddChild(dummyChildB);

	UnitTest::Assert("dummyChildA should still have a name",
	    dummyChildA->GetVariable("name", &name) == true);
	UnitTest::Assert("dummyChildA's name changed unexpectedly?",
	    name.ToString(), "dummy0");

	UnitTest::Assert("dummyChildB should have a name now",
	    dummyChildB->GetVariable("name", &name) == true);
	UnitTest::Assert("dummyChildB's name mismatch",
	    name.ToString(), "dummy1");

	dummy->AddChild(dummyChildC);

	UnitTest::Assert("dummyChildC should have a name now",
	    dummyChildC->GetVariable("name", &name) == true);
	UnitTest::Assert("dummyChildC's name mismatch",
	    name.ToString(), "dummy2");

	dummy->RemoveChild(dummyChildA);

	UnitTest::Assert("dummyChildB should still have a name, after"
		" removing child A",
	    dummyChildB->GetVariable("name", &name) == true);
	UnitTest::Assert("dummyChildB's should not have changed",
	    name.ToString(), "dummy1");
}

static void Test_DummyComponent_GeneratePath()
{
	refcount_ptr<Component> dummyA = new DummyComponent;
	refcount_ptr<Component> dummyB = new DummyComponent;
	refcount_ptr<Component> dummyC = new DummyComponent;

	UnitTest::Assert("generated path with no name",
	    dummyA->GeneratePath(), "(dummy)");

	dummyB->AddChild(dummyA);

	UnitTest::Assert("generated path with default name fallback",
	    dummyA->GeneratePath(), "(dummy).dummy0");

	dummyC->AddChild(dummyB);

	UnitTest::Assert("generated path with two parent levels",
	    dummyA->GeneratePath(), "(dummy).dummy0.dummy0");

	dummyC->RemoveChild(dummyB);

	UnitTest::Assert("generated path with one parent level again",
	    dummyA->GeneratePath(), "dummy0.dummy0");
}

static void Test_DummyComponent_LookupPath()
{
	refcount_ptr<Component> dummyA = new DummyComponent;

	dummyA->SetVariable("name", StateVariableValue("hello"));

	refcount_ptr<Component> component1 = dummyA->LookupPath("nonsense");
	UnitTest::Assert("nonsense lookup should fail",
	    component1.IsNULL() == true);

	refcount_ptr<Component> component2 = dummyA->LookupPath("hello");
	UnitTest::Assert("lookup should have found the component itself",
	    component2 == dummyA);

	refcount_ptr<Component> child = new DummyComponent;
	refcount_ptr<Component> childchild = new DummyComponent;
	child->SetVariable("name", StateVariableValue("x"));
	childchild->SetVariable("name", StateVariableValue("y"));
	dummyA->AddChild(child);
	child->AddChild(childchild);

	refcount_ptr<Component> component3 = dummyA->LookupPath("hello");
	UnitTest::Assert("lookup should still succeed, when dummyA has"
		" children",
	    component3 == dummyA);

	refcount_ptr<Component> component4 = dummyA->LookupPath("hello.z");
	UnitTest::Assert("lookup of nonsense child of dummyA should fail",
	    component4.IsNULL() == true);

	refcount_ptr<Component> component5 = dummyA->LookupPath("hello.x");
	UnitTest::Assert("lookup of child of dummyA should succeed",
	    component5 == child);

	refcount_ptr<Component> component6 = dummyA->LookupPath("hello.x.y");
	UnitTest::Assert("lookup of grandchild of dummyA should succeed",
	    component6 == childchild);
}

static void Test_DummyComponent_FindPathByPartialMatch()
{
	refcount_ptr<Component> root = new DummyComponent;
	refcount_ptr<Component> machine0 = new DummyComponent;
	refcount_ptr<Component> machine1 = new DummyComponent;
	refcount_ptr<Component> machine2 = new DummyComponent;
	refcount_ptr<Component> machine3 = new DummyComponent;
	refcount_ptr<Component> m0isabus0 = new DummyComponent;
	refcount_ptr<Component> m1pcibus0 = new DummyComponent;
	refcount_ptr<Component> m1pcibus1 = new DummyComponent;
	refcount_ptr<Component> m2pcibus0 = new DummyComponent;
	refcount_ptr<Component> m3otherpci = new DummyComponent;

	root->SetVariable("name", StateVariableValue("root"));
	machine0->SetVariable("name", StateVariableValue("machine0"));
	machine1->SetVariable("name", StateVariableValue("machine1"));
	machine2->SetVariable("name", StateVariableValue("machine2"));
	machine3->SetVariable("name", StateVariableValue("machine3"));
	m0isabus0->SetVariable("name", StateVariableValue("isabus0"));
	m1pcibus0->SetVariable("name", StateVariableValue("pcibus0"));
	m1pcibus1->SetVariable("name", StateVariableValue("pcibus1"));
	m2pcibus0->SetVariable("name", StateVariableValue("pcibus0"));
	m3otherpci->SetVariable("name", StateVariableValue("otherpci"));

	root->AddChild(machine0);
	root->AddChild(machine1);
	root->AddChild(machine2);
	root->AddChild(machine3);

	machine0->AddChild(m0isabus0);
	machine1->AddChild(m1pcibus0);
	machine1->AddChild(m1pcibus1);
	machine2->AddChild(m2pcibus0);
	machine3->AddChild(m3otherpci);

	vector<string> matches;

	matches = root->FindPathByPartialMatch("nonsense");
	UnitTest::Assert("there should be no nonsense matches",
	    matches.size(), 0);
	
	matches = root->FindPathByPartialMatch("");
	UnitTest::Assert("empty string should return all components",
	    matches.size(), 10);

	matches = root->FindPathByPartialMatch("pci");
	UnitTest::Assert("pci matches mismatch",
	    matches.size(), 3);
	UnitTest::Assert("pci match 0 mismatch",
	    matches[0], "root.machine1.pcibus0");
	UnitTest::Assert("pci match 1 mismatch",
	    matches[1], "root.machine1.pcibus1");
	UnitTest::Assert("pci match 2 mismatch",
	    matches[2], "root.machine2.pcibus0");

	matches = root->FindPathByPartialMatch("machine1");
	UnitTest::Assert("machine1 match mismatch",
	    matches.size(), 1);
	UnitTest::Assert("machine1 match 0 mismatch",
	    matches[0], "root.machine1");

	matches = root->FindPathByPartialMatch("machine2.pcibus");
	UnitTest::Assert("machine2.pcibus match mismatch",
	    matches.size(), 1);
	UnitTest::Assert("machine2.pcibus match 0 mismatch",
	    matches[0], "root.machine2.pcibus0");

	matches = root->FindPathByPartialMatch("machine.pcibus");
	UnitTest::Assert("machine.pcibus should have no matches",
	    matches.size(), 0);
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

static void Test_DummyComponent_Clone()
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
	dummyChildA2->SetVariable("z", StateVariableValue(-1));

	dummyChildA->AddChild(dummyChildA1);
	dummyChildA->AddChild(dummyChildA2);
	dummy->AddChild(dummyChildA);

	Checksum originalChecksum;
	dummy->AddChecksum(originalChecksum);

	refcount_ptr<Component> clone = dummy->Clone();

	Checksum cloneChecksum;
	clone->AddChecksum(cloneChecksum);

	UnitTest::Assert("clone should have same checksum",
	    originalChecksum == cloneChecksum);

	dummy->SetVariable("x", StateVariableValue("modified"));

	Checksum originalChecksumAfterModifyingOriginal;
	dummy->AddChecksum(originalChecksumAfterModifyingOriginal);
	Checksum cloneChecksumAfterModifyingOriginal;
	clone->AddChecksum(cloneChecksumAfterModifyingOriginal);

	UnitTest::Assert("original should have changed checksum",
	    originalChecksum != originalChecksumAfterModifyingOriginal);
	UnitTest::Assert("clone should have same checksum",
	    cloneChecksum == cloneChecksumAfterModifyingOriginal);

	clone->SetVariable("x", StateVariableValue("modified"));

	Checksum originalChecksumAfterModifyingClone;
	dummy->AddChecksum(originalChecksumAfterModifyingClone);
	Checksum cloneChecksumAfterModifyingClone;
	clone->AddChecksum(cloneChecksumAfterModifyingClone);

	UnitTest::Assert("original should have same checksum, after the "
		"clone has been modified",
	    originalChecksumAfterModifyingClone ==
	    originalChecksumAfterModifyingOriginal);
	UnitTest::Assert("modified clone should have same checksum as "
		"modified original",
	    cloneChecksumAfterModifyingClone ==
	    originalChecksumAfterModifyingOriginal);
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

UNITTESTS(DummyComponent)
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
	UNITTEST(Test_DummyComponent_AddChild_UniqueName);

	// Path tests
	UNITTEST(Test_DummyComponent_GeneratePath);
	UNITTEST(Test_DummyComponent_LookupPath);
	UNITTEST(Test_DummyComponent_FindPathByPartialMatch);

	// Get/Set state variables
	UNITTEST(Test_DummyComponent_GetSet_Variables);

	// Clone
	UNITTEST(Test_DummyComponent_Clone);

	// Serialization/deserialization
	UNITTEST(Test_DummyComponent_SerializeDeserialize);
}

#endif

