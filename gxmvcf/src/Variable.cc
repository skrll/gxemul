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

#include "misc.h"
#include "gxmvcf/Variable.h"

#include "UnitTest.h"

using namespace GXmvcf;


static void Test_Variable_DefaultConstructor()
{
	Variable<string> v("someName");
	UnitTest::Assert("v should contain an empty string",
	    v.GetValue() == "");

	Variable<int> v2("someName");
	UnitTest::Assert("v2 should be 0", v2.GetValue() == 0);

	Variable<double> v3("someName");
	UnitTest::Assert("v3 should be 0.0", v3.GetValue() == 0.0);
}

static void Test_Variable_ConstructorWithArg()
{
	Variable<string> v("someName", "hello");
	UnitTest::Assert("v should contain the specified string",
	    v.GetValue() == "hello");
	UnitTest::Assert("v should contain the specified string",
	    v.GetValueRef() == "hello");

	Variable<int> v2("someName", 42);
	UnitTest::Assert("v2 should match", v2.GetValue() == 42);
	UnitTest::Assert("v2 should match", v2.GetValueRef() == 42);

	Variable<double> v3("someName", -1.293);
	UnitTest::Assert("v3 should match", v3.GetValue() == -1.293);
	UnitTest::Assert("v3 should match", v3.GetValueRef() == -1.293);
}

UNITTESTS(Variable)
{
        UNITTEST(Test_Variable_DefaultConstructor);
        UNITTEST(Test_Variable_ConstructorWithArg);
}
