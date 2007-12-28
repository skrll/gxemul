#ifndef STATEVARIABLE_H
#define	STATEVARIABLE_H

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
 *  $Id: StateVariable.h,v 1.1 2007-12-28 19:08:44 debug Exp $
 */

#include "misc.h"

#include "SerializationContext.h"
#include "StateVariableValue.h"
#include "UnitTest.h"


class StateVariable;
typedef map<string,StateVariable> StateVariableMap;


/**
 * \brief StateVariables make up the persistent state of Component objects.
 *
 * A %StateVariable has a name, and a value.
 */
class StateVariable
	: public UnitTestable
{
public:
	/**
	 * \brief Default constructor for a StateVariable.
	 */
	StateVariable() {}

	/**
	 * \brief Base constructor for a StateVariable.
	 *
	 * @param name		The name of the variable.
	 * @param value		The variable's initial value.
	 */
	StateVariable(const string& name, const StateVariableValue& value);

	/**
	 * \brief Gets the name of the variable.
	 *
	 * @return the name of the variable
	 */
	const string& GetName() const;
	
	/**
	 * \brief Gets the value of the variable.
	 *
	 * @return the value of the variable
	 */
	const StateVariableValue& GetValue() const;

	/**
	 * \brief Sets the value of the variable.
	 *
	 * @param newValue	The value to assign to the variable.
	 */
	 void SetValue(const StateVariableValue& newValue);

	/**
	 * \brief Serializes the variable into a string.
	 *
	 * @return a string, representing the variable
	 */
	string Serialize(SerializationContext& context) const;


	/********************************************************************/

	static int RunUnitTests();

private:
	string			m_name;
	StateVariableValue	m_value;
};


#endif	// STATEVARIABLE_H

