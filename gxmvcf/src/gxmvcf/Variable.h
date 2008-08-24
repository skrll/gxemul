#ifndef	GXMVCF_VARIABLE_H
#define	GXMVCF_VARIABLE_H

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

#include "gxmvcf.h"  

namespace GXmvcf
{

/**
 * \brief A model variable.
 *
 * A variable holds a value of a specific type. A model node has zero
 * or more variables.
 */
template<class T>
class Variable
{
public:
	/**
	 * \brief Constructs a variable of type T with a default initial value.
	 *
	 * (For POD types, the value will be 0.)
	 */
	Variable()
		: m_value()	// Note: POD types will be initialized
				// to zeroes!
	{
	}

	/**
	 * \brief Constructs a variable of type T with a specific initial value.
	 *
	 * @param value The initial value.
	 */
	Variable(T value)
		: m_value(value)
	{
	}
	
	/**
	 * \brief Gets a const reference to the variable value.
	 *
	 * @return A const reference to the variable value.
	 */
	const T& GetValueRef() const
	{
		return m_value;
	}

	/**
	 * \brief Gets a copy of the variable value.
	 *
	 * @return A copy of the variable value.
	 */
	T GetValue() const
	{
		return m_value;
	}

private:
	T	m_value;
};

}

#endif	/*  GXMVCF_VARIABLE_H  */
