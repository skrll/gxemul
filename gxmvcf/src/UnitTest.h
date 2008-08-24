#ifndef UNITTEST_H
#define	UNITTEST_H

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
 */

#include "misc.h"
#include "gxmvcf/gxmvcf.h"

#include <exception>


/**
 * \brief An exception thrown by unit test cases that fail.
 */
class UnitTestFailedException
	: public std::exception
{
public:
	UnitTestFailedException(const string& strMessage)
		: m_strMessage(strMessage)
	{
	}

	virtual ~UnitTestFailedException() throw ()
	{
	}

	/**
	 * \brief Retrieves the error message associated with the exception.
	 *
	 * @return a const reference to the error message string
	 */
	const string& GetMessage() const
	{
		return m_strMessage;
	}

private:
	string	m_strMessage;
};


/**
 * \brief A collection of helper functions, for writing simple unit tests.
 */
class UnitTest
{
public:
	/**
	 * \brief Runs all unit tests.
	 *
	 * Unit tests for all classes that the <tt>configure</tt>
	 * script detected as using UNITTESTS(classname) are executed.
	 *
	 * If a test fails, the UNITTEST(testname) macro in the unit testing
	 * framework takes care of outputting a line identifying that test to
	 * std::cerr.
	 *
	 * @return zero if no unit tests failed, 1 otherwise.
	 */
	static int RunTests();


	/*******************************************************************/
	/*            Helper functions, used by unit test cases            */
	/*******************************************************************/

	/**
	 * \brief Asserts that a boolean condition is correct.
	 *
	 * If the condition is not true, then Fail is called with the failure
	 * message.
	 *
	 * The failure message can be something like "expected xyz",
	 * or "the list should be empty at this point".
	 *
	 * @param strFailMessage failure message to print to std::cerr
	 * @param condition The result of some operation, which should be true.
	 */
	static void Assert(const string& strFailMessage, bool condition);

	/**
	 * \brief Asserts that two uint64_t values are equal.
	 *
	 * If the values are not equal, Fail is called with the failure message.
	 *
	 * The failure message can be something like "expected xyz",
	 * or "the list should be empty at this point".
	 *
	 * @param strFailMessage Failure message to print to std::cerr.
	 * @param actualValue The actual value.
	 * @param expectedValue The expected value.
	 */
	static void Assert(const string& strFailMessage,
	    uint64_t actualValue, uint64_t expectedValue);

	/**
	 * \brief Asserts that two string values are equal.
	 *
	 * If the values are not equal, Fail is called with the failure message.
	 *
	 * The failure message can be something like "expected xyz",
	 * or "the list should be empty at this point".
	 *
	 * @param strFailMessage Failure message to print to std::cerr.
	 * @param actualValue The actual value.
	 * @param expectedValue The expected value.
	 */
	static void Assert(const string& strFailMessage,
	    const string& actualValue, const string& expectedValue);

	/**
	 * \brief Fails a unit test unconditionally, by throwing a
	 * UnitTestFailedException.
	 *
	 * @param strMessage failure message
	 */
	static void Fail(const string& strMessage);
};


#include <iostream>
/**
 * \brief Helper for unit test case execution.
 *
 * The main purpose, appart from making the code look more compact,
 * of having a macro for this is that the <tt>configure</tt> script finds
 * all .cc files that use this macro, and generates lists of header files
 * to include and lists of RunUnitTests functions to call. These are then
 * included and run from UnitTest::RunTests.
 *
 * See the comment for UNITTEST for details on how to use it.
 */
#define UNITTESTS(class) \
	void RunUnitTests_ ## class (int& nSucceeded, int& nFailures)

/**
 * \brief Helper for unit test case execution.
 *
 * For each test case that throws a UnitTestFailedException, the number
 * of failures is increased. For test cases that don't fail, the number
 * of successful test cases is increased instead.
 *
 * Usage: (usually at the end of a class implementation file)<pre>
 *	\#ifndef WITHOUTUNITTESTS
 *
 *	static void MyClass::Test_MyClass_SomeTest()
 *	{
 *		UnitTest::Assert("expected blah blah", bool_condition);
 *		... more asserts here ...
 *	}
 *
 *	...
 *
 *	UNITTESTS(MyClass)
 *	{
 *		UNITTEST(Test_MyClass_SomeTest);
 *		UNITTEST(Test_MyClass_AnotherTest);
 *		... more test cases here ...
 *	}
 *
 *	\#endif // WITHOUTUNITTESTS</pre>
 */
#define UNITTEST(functionname)	try {					\
	std::cout << "### " #functionname "\n";				\
	(functionname)();						\
	++ (nSucceeded);						\
} catch (UnitTestFailedException& ex) {					\
	std::cerr << "\n### " #functionname " (" __FILE__ " line "	\
		<< __LINE__ << ") failed!\n"				\
		"    > " << ex.GetMessage() << "\n";			\
	++ (nFailures);   						\
}


#endif	// UNITTEST_H
