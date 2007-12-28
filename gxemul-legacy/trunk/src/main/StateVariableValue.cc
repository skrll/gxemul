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
 *  $Id: StateVariableValue.cc,v 1.1 2007-12-28 19:08:44 debug Exp $
 */

#include "assert.h"
#include <sstream>

#include "EscapedString.h"
#include "StateVariableValue.h"


StateVariableValue::StateVariableValue()
	: m_type(Empty)
{
}


StateVariableValue::StateVariableValue(const string& strValue)
	: m_type(String)
	, m_str(strValue)
{
}


StateVariableValue::StateVariableValue(uint64_t value)
	: m_type(UInt64)
{
	m_value.uint64 = value;
}


StateVariableValue::StateVariableValue(const string& strType,
		const string& escapedStringValue)
{
	if (strType == "empty") {
		m_type = Empty;
		m_str = "";
	} else if (strType == "string") {
		m_type = String;
		m_str = EscapedString(escapedStringValue).Decode();
	} else if (strType == "uint8") {
		m_type = UInt8;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.uint8 = strtoull(m_str.c_str(), NULL, 0);
	} else if (strType == "uint16") {
		m_type = UInt16;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.uint16 = strtoull(m_str.c_str(), NULL, 0);
	} else if (strType == "uint32") {
		m_type = UInt32;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.uint32 = strtoull(m_str.c_str(), NULL, 0);
	} else if (strType == "uint64") {
		m_type = UInt64;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.uint64 = strtoull(m_str.c_str(), NULL, 0);
	} else if (strType == "sint8") {
		m_type = SInt8;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.sint8 = strtoll(m_str.c_str(), NULL, 0);
	} else if (strType == "sint16") {
		m_type = SInt16;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.sint16 = strtoll(m_str.c_str(), NULL, 0);
	} else if (strType == "sint32") {
		m_type = SInt32;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.sint32 = strtoll(m_str.c_str(), NULL, 0);
	} else if (strType == "sint64") {
		m_type = SInt64;
		m_str = EscapedString(escapedStringValue).Decode();
		m_value.sint64 = strtoll(m_str.c_str(), NULL, 0);
	} else {
		m_type = Unknown;
		m_str = "";
	}
}


enum StateVariableValue::Type StateVariableValue::GetType() const
{
	return m_type;
}


string StateVariableValue::GetTypeString() const
{
	switch (m_type) {
	case Empty:
		return "empty";
	case String:
		return "string";
	case UInt8:
		return "uint8";
	case UInt16:
		return "uint16";
	case UInt32:
		return "uint32";
	case UInt64:
		return "uint64";
	case SInt8:
		return "sint8";
	case SInt16:
		return "sint16";
	case SInt32:
		return "sint32";
	case SInt64:
		return "sint64";
	default:
		return "unknown";
	}
}


string StateVariableValue::ToString() const
{
	std::stringstream sstr;

	switch (m_type) {
	case Empty:
		return "";
	case String:
		return m_str;
	case UInt8:
		sstr << m_value.uint8;
		return sstr.str();
	case UInt16:
		sstr << m_value.uint16;
		return sstr.str();
	case UInt32:
		sstr << m_value.uint32;
		return sstr.str();
	case UInt64:
		sstr << m_value.uint64;
		return sstr.str();
	case SInt8:
		sstr << m_value.sint8;
		return sstr.str();
	case SInt16:
		sstr << m_value.sint16;
		return sstr.str();
	case SInt32:
		sstr << m_value.sint32;
		return sstr.str();
	case SInt64:
		sstr << m_value.sint64;
		return sstr.str();
	default:
		return "(unknown)";
	}
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

static void Test_StateVariableValue_Empty()
{
	StateVariableValue var;

	UnitTest::Assert("type should be Empty",
	    var.GetType() == StateVariableValue::Empty);
	UnitTest::Assert("type string should be 'empty'",
	    var.GetTypeString() == "empty");
	UnitTest::Assert("value should be empty string",
	    var.ToString() == "");
}

static void Test_StateVariableValue_String()
{
	StateVariableValue var("hello");

	UnitTest::Assert("type should be String",
	    var.GetType() == StateVariableValue::String);
	UnitTest::Assert("type string should be 'string'",
	    var.GetTypeString() == "string");
	UnitTest::Assert("value should be hello",
	    var.ToString() == "hello");
}

static void Test_StateVariableValue_FromTypeString_E()
{
	StateVariableValue var("empty", "\"anything\"");

	UnitTest::Assert("type should be Empty",
	    var.GetType() == StateVariableValue::Empty);
	UnitTest::Assert("value should be empty string",
	    var.ToString() == "");
}

static void Test_StateVariableValue_FromTypeString_S()
{
	StateVariableValue var("string", "\"something\"");

	UnitTest::Assert("type should be String",
	    var.GetType() == StateVariableValue::String);
	UnitTest::Assert("value should be 'something'",
	    var.ToString() == "something");
}

int StateVariableValue::RunUnitTests()
{
	int nrOfFailures = 0;

	UNITTEST(nrOfFailures, Test_StateVariableValue_Empty);
	UNITTEST(nrOfFailures, Test_StateVariableValue_String);
	UNITTEST(nrOfFailures, Test_StateVariableValue_FromTypeString_E);
	UNITTEST(nrOfFailures, Test_StateVariableValue_FromTypeString_S);

	return nrOfFailures;
}

#endif
