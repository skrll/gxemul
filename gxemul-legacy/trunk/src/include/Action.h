#ifndef ACTION_H
#define	ACTION_H

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
 *  $Id: Action.h,v 1.3 2007-12-18 14:39:29 debug Exp $
 */

#include "misc.h"


/**
 * \brief Actions are undoable/redoable wrappers around function calls.
 *
 * Actions that are replayable and/or undoable are implemented using the
 * Action class.
 *
 * The main purpose of this class, together with the ActionStack class, is 
 * to enable undo/redo functionality for the end user via the GUI, but the 
 * functionality is also available from the text-only interface.
 */
class Action
	: public ReferenceCountable
{
public:
	/**
	 * Base constructor for an Action.
	 *
	 * @param strClassName		The name of the action class.
	 *				It should be a short, descriptive name,
	 *				e.g. "add component".
	 * @param strDescription	A short description describing the
	 *				action. For e.g. the "add component"
	 *				action, it can be "Adds a component to
	 *				the current configuration tree."
	 */
	Action(const string& strClassName,
		const string& strDescription);

	virtual ~Action();

	virtual void Execute() = 0;

	virtual void Redo() = 0;

	virtual void Undo() = 0;

private:
	string	m_strClassName;
	string	m_strDescription;
};


#endif	// ACTION_H
