/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: UnitTest.cc,v 1.2 2007-12-18 13:57:13 debug Exp $
 */

#include <iostream>

#include "misc.h"
#include "UnitTest.h"

#include "ActionStack.h"


void UnitTest::Assert(const string& strFailMessage, bool bCondition)
{
	if (!bCondition)
		Fail(strFailMessage);
}


void UnitTest::Fail(const string& strMessage)
{
	throw UnitTestFailedException(strMessage);
}


#ifdef WITHOUTUNITTESTS


int UnitTest::RunTests()
{
	std::cerr << "Skipping unit tests, because WITHOUTUNITTESTS "
	    "was defined.\n";

	return 0;
}


#else	// !WITHOUTUNITTESTS


int UnitTest::RunTests()
{
	int nrOfErrors = 0;

	// Run tests in all specified classes:
	nrOfErrors += ActionStack::RunUnitTests();

	if (nrOfErrors == 0)
		std::cerr << "All tests passed.\n";
	else
		std::cerr << nrOfErrors << " TESTS FAILED!\n";

	// Returns 0 if there were no errors.
	return nrOfErrors > 0;
}


#endif	// !WITHOUTUNITTESTS
