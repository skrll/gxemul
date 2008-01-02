#ifndef STATEVARIABLEVALUE_H
#define	STATEVARIABLEVALUE_H

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
 *  $Id: StateVariableValue.h,v 1.2 2008-01-02 10:56:41 debug Exp $
 */

#include "misc.h"

#include "UnitTest.h"


/**
 * \brief A %StateVariableValue is a typed value.
 */
class StateVariableValue
	: public UnitTestable
{
public:
	/**
	 * \brief An enumeration of the possible types of a %StateVariableValue.
	 */
	enum Type {
		Unknown = 0,
		Empty,
		String,
		UInt8,
		UInt16,
		UInt32,
		UInt64,
		SInt8,
		SInt16,
		SInt32,
		SInt64
	};

public:	
	/**
	 * \brief Constructs a StateVariableValue of type Empty.
	 */
	StateVariableValue();

	/**
	 * \brief Constructs a StateVariableValue of type String.
	 *
	 * @param strValue		String value.
	 */
	StateVariableValue(const string& strValue);

	/**
	 * \brief Constructs a StateVariableValue of type uint64_t.
	 *
	 * @param value	uint64_t value
	 */
	StateVariableValue(uint64_t value);

	/**
	 * \brief Constructs a StateVariableValue of a given type and value.
	 *
	 * @param strType		The type in string format.
	 * @param escapedStringValue	The value as a C-style escaped string.
	 */
	StateVariableValue(const string& strType,
	    const string& escapedStringValue);

	/**
	 * \brief Gets the type of the %StateVariableValue.
	 *
	 * @return the type of the value
	 */
	enum Type GetType() const;
	
	/**
	 * \brief Gets the type of the %StateVariableValue, as a string.
	 *
	 * @return the type of the value, formatted as a string
	 */
	string GetTypeString() const;
	
	/**
	 * \brief Formats the value as a string.
	 *
	 * @return the value, formatted as a string
	 */
	string ToString() const;


	/********************************************************************/

	static void RunUnitTests(int& nSucceeded, int& nFailures);

private:
	enum Type		m_type;

	string			m_str;
	union {
		uint8_t		uint8;
		uint16_t	uint16;
		uint32_t	uint32;
		uint64_t	uint64;
		int8_t		sint8;
		int16_t		sint16;
		int32_t		sint32;
		int64_t		sint64;
	} m_value;
};


#endif	// STATEVARIABLEVALUE_H

