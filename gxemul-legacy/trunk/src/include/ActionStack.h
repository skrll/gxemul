#ifndef ACTIONSTACK_H
#define	ACTIONSTACK_H

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
 *  $Id: ActionStack.h,v 1.1 2007-12-17 23:19:04 debug Exp $
 */

#include "misc.h"

#include "Action.h"


/**
 * The ActionStack contains zero or more reference counted pointers 
 * to Action objects.
 *
 * The main purpose of this class, together with the Action class, is to 
 * enable undo/redo functionality for the end user via the GUI, but the
 * functionality is also available from the text-only interface.
 */
class ActionStack
{
public:
	/**
	 * Clears the stack.
	 */
	void Clear();

	/**
	 * Checks whether the stack is empty. The main purpose of
	 * this is for indication in the GUI. If the stack is empty, the
	 * undo button (or undo menu entry) can be greyed out.
	 *
	 * @return true if the stack is empty, false otherwise.
	 */
	bool IsEmpty() const;

	/**
	 * Pushes an Action onto the stack. This increases the reference
	 * count of the action, so the caller can release its reference.
	 *
	 * @param pAction a pointer to the Action to push
	 */
	void PushAction(Action* pAction);

	/**
	 * Pops an Action from the stack, if there is any left. The
	 * return value is a reference counted pointer; ownership of the
	 * Action is transfered to the caller.
	 *
	 * @return a reference counter pointer to an Action
	 */
	refcount_ptr<Action> PopAction();

private:
	vector< refcount_ptr<Action> >	m_vecActions;
};


#endif	// ACTIONSTACK_H
