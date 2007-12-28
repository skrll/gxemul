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
 *  $Id: StateVariable.cc,v 1.1 2007-12-28 19:08:44 debug Exp $
 */

#include "EscapedString.h"
#include "StateVariable.h"


StateVariable::StateVariable(const string& name,
				const StateVariableValue& value)
	: m_name(name)
	, m_value(value)
{
}


const string& StateVariable::GetName() const
{
	return m_name;
}


const StateVariableValue& StateVariable::GetValue() const
{
	return m_value;
}


void StateVariable::SetValue(const StateVariableValue& newValue)
{
	m_value = newValue;
}


string StateVariable::Serialize(SerializationContext& context) const
{
	return
	    context.Tabs() + m_value.GetTypeString() + " " + m_name + " " +
	    EscapedString(m_value.ToString()).Generate() + "\n";
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

static void Test_StateVariable_Default()
{
	StateVariableValue value1("value 1");
	StateVariableValue value2;

	StateVariable var("hello", value1);

	UnitTest::Assert("name should be hello",
	    var.GetName() == "hello");
	UnitTest::Assert("type should be String",
	    var.GetValue().GetType() == StateVariableValue::String);
	UnitTest::Assert("value should be value 1",
	    var.GetValue().ToString() == "value 1");

	var.SetValue(value2);

	UnitTest::Assert("type should now be Empty",
	    var.GetValue().GetType() == StateVariableValue::Empty);
	UnitTest::Assert("value should now be empty string",
	    var.GetValue().ToString() == "");
}

static void Test_StateVariable_Serialize_String()
{
	StateVariableValue value("value world");
	StateVariable var("hello", value);

	SerializationContext dummyContext;

	UnitTest::Assert("variable serialization mismatch?",
	    var.Serialize(dummyContext) == "string hello \"value world\"\n");
}

static void Test_StateVariable_Serialize_Empty()
{
	StateVariableValue value;
	StateVariable var("hello", value);

	SerializationContext dummyContext;

	UnitTest::Assert("variable serialization mismatch?",
	    var.Serialize(dummyContext) == "empty hello \"\"\n");
}

static void Test_StateVariable_Serialize_StringWithEscapes()
{
	string s = "a\\b\tc\nd\re\bf\"g'h";
	StateVariableValue value(s);
	StateVariable var("hello", value);

	SerializationContext dummyContext;

	UnitTest::Assert("variable serialization mismatch?",
	    var.Serialize(dummyContext) ==
	    "string hello " + EscapedString(s).Generate() + "\n");
}

int StateVariable::RunUnitTests()
{
	int nrOfFailures = 0;

	UNITTEST(nrOfFailures, Test_StateVariable_Default);
	UNITTEST(nrOfFailures, Test_StateVariable_Serialize_String);
	UNITTEST(nrOfFailures, Test_StateVariable_Serialize_Empty);
	UNITTEST(nrOfFailures, Test_StateVariable_Serialize_StringWithEscapes);

	return nrOfFailures;
}

#endif

