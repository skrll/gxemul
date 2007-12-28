#ifndef SERIALIZATIONCONTEXT_H
#define	SERIALIZATIONCONTEXT_H

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
 *  $Id: SerializationContext.h,v 1.1 2007-12-28 19:08:44 debug Exp $
 */

#include "misc.h"


/**
 * \brief A context used during serialization of objects.
 */
class SerializationContext
{
public:
	/**
	 * \brief Constructs a default SerializationContext.
	 */
	SerializationContext()
		: m_indentation(0)
	{
	}

	int GetIndentation() const
	{
		return m_indentation;
	}

	void SetIndentation(int indentation)
	{
		m_indentation = indentation;
	}

	SerializationContext Indented()
	{
		SerializationContext newContext;
		newContext.SetIndentation(GetIndentation() + 1);
		return newContext;
	}

	string Tabs() const
	{
		string retValue;
		for (int i=0; i<m_indentation; i++)
			retValue += "\t";
		return retValue;
	}

private:
	int	m_indentation;
};


#endif	// SERIALIZATIONCONTEXT_H
