#ifndef UI_H
#define	UI_H

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
 *  $Id: UI.h,v 1.3 2008-01-02 10:56:41 debug Exp $
 */

#include "misc.h"

class GXemul;


/**
 * \brief Base class for a User Interface.
 */
class UI
	: public ReferenceCountable
{
public:
	/**
	 * \brief Constructs a User Interface.
	 *
	 * @param gxemul A pointer to the GXemul owner instance.
	 */
	UI(GXemul* gxemul)
	    : m_gxemul(gxemul)
	{
	}

	virtual ~UI()
	{
	}

	/**
	 * \brief Initializes the UI.
	 */
	virtual void Initialize() = 0;

	/**
	 * \brief Shows a startup banner.
	 */
	virtual void ShowStartupBanner() = 0;

	/**
	 * \brief Shows a debug message.
	 *
	 * @param msg The message to show.
	 */
	virtual void ShowDebugMessage(const string& msg) = 0;

	/**
	 * \brief Echoes a character to the command input line.
	 *
	 * @param ch The character to output.
	 */
	virtual void ShowInputLineCharacter(stringchar ch) = 0;

	/**
	 * \brief Runs the UI's main loop.
	 *
	 * The return code should be the exit code that the gxemul binary
	 * should return.
 	 *
	 * @return 0 on success, non-zero on failure.
	 */
	virtual int MainLoop() = 0;

protected:
	GXemul*		m_gxemul;
};


#endif	// UI_H
