#ifndef COMPONENT_H
#define	COMPONENT_H

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
 *  $Id: Component.h,v 1.5 2007-12-21 18:16:57 debug Exp $
 */

#include "misc.h"


/**
 * \brief A %Component is a node in the configuration tree that
 *	makes up an emulation setup.
 *
 * The %Component is the core concept in %GXemul. All devices,
 * CPUs, networks, and so on are components.
 */
class Component
	: public ReferenceCountable
{
public:
	/**
	 * Base constructor for a %Component.
	 *
	 * @param strClassName		The name of the component class.
	 *				It should be a short, descriptive name.
	 *				For e.g. a PCI bus class, it can be "pcibus".
	 * @param strDescription	A short description describing the
	 *				component class. For e.g. a PCI bus class,
	 *				it can be "PCI bus".
	 */
	Component(const string& strClassName,
		  const string& strDescription);

	virtual ~Component();

	/**
	 * Calling Reset on a %Component should reset its state
	 * to the same state that a newly created Component of that
	 * type has.
	 */
	virtual void Reset() = 0;

private:
	/**
	 * Disallow creating %Component objects using the default constructor.
	 */
	Component() { }

private:
	string		m_strClassName;
	string		m_strDescription;
};


#endif	// COMPONENT_H
