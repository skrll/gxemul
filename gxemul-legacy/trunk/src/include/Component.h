#ifndef COMPONENT_H
#define	COMPONENT_H

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
 *  $Id: Component.h,v 1.8 2008-01-05 13:13:49 debug Exp $
 */

#include "misc.h"

#include "Checksum.h"
#include "SerializationContext.h"
#include "StateVariable.h"


class Component;
typedef vector< refcount_ptr<Component> > Components;


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
	 * \brief Base constructor for a %Component.
	 *
	 * @param className	The name of the component class.
	 *			It should be a short, descriptive name.
	 *			For e.g. a PCI bus class, it can be "pcibus".
	 */
	Component(const string& className);

	virtual ~Component() = 0;

	/**
	 * \brief Creates a component given a short component class name.
	 *
	 * @param className	The component class name, e.g. "pcibus".
	 * @return A reference counted Component pointer. This is set to the
	 *	newly created component on success. On failure it is set to
	 *	NULL.
	 */
	static refcount_ptr<Component> CreateComponent(const string& className);

	/**
	 * \brief Gets the class name of the component.
	 *
	 * @return the class name of the component, e.g. "pcibus" for a PCI
	 *	bus component class
	 */
	string GetClassName() const;

	/**
	 * \brief Clones the component and all its children.
	 *
	 * The new copy is a complete copy; modifying either the copy or the
	 * original will not affect the other.
	 *
	 * @return A reference counted pointer to the clone.
	 */
	refcount_ptr<Component> Clone() const;

	/**
	 * \brief Resets the state of this component and all its children.
	 */
	void Reset();

	/**
	 * \brief Resets the state variables of this component.
	 *
	 * Note 1: This function is not recursive, so children should not be
	 * traversed.
	 *
	 * Note 2: After a component's state variables have been reset, the
	 * parent's ResetVariables should be called.
	 *
	 * The implementation of this function ususally takes the form of a
	 * list of calls to Component::SetVariable.
	 */
	virtual void ResetVariables();

	/**
	 * \brief Sets the parent component of this component.
	 *
	 * Note: Not reference counted.
	 *
	 * @param parentComponent	a pointer to the parent component.
	 */
	void SetParent(Component* parentComponent);

	/**
	 * \brief Gets this component's parent component, if any.
	 *
	 * Note: Not reference counted.
	 *
	 * Returns NULL if there is no parent component.
	 *
	 * @return the pointer to the parent component, or NULL
	 */
	Component* GetParent();
	
	/**
	 * \brief Adds a reference to a child component.
	 *
	 * @param childComponent  A reference counted pointer to the child
	 *	component to add.
	 * @param insertPosition  If specified, this is the position in the
	 *	vector of child components where the child will be inserted.
	 *	If not specified (or -1), then the child will be added to
	 *	the end of the vector.
	 */
	void AddChild(refcount_ptr<Component> childComponent,
		size_t insertPosition = (size_t) -1);

	/**
	 * \brief Removes a reference to a child component.
	 *
	 * @param childToRemove  A pointer to the child to remove.
	 * @return  A zero-based index, which is the position in the vector
	 *	of children where the child was. (Needed for e.g. Undo
	 *	functionality.)
	 */
	size_t RemoveChild(Component* childToRemove);

	/**
	 * \brief Gets pointers to child components.
	 *
	 * @return reference counted pointers to child components
	 */
	Components& GetChildren();

	/**
	 * \brief Gets the value of a state variable.
	 *
	 * @param name the variable name
	 * @param valuePtr a pointer to a StateVariableValue which will be
	 *		filled with the value of the variable, or NULL
	 * @return true if the variable was available, false otherwise
	 */
	bool GetVariable(const string& name, StateVariableValue* valuePtr);

	/**
	 * \brief Sets a state variable.
	 *
	 * @param name the variable name
	 * @param newValue the new value for the state variable
	 */
	void SetVariable(const string& name,
		const StateVariableValue& newValue);

	/**
	 * \brief Serializes the component into a string.
	 *
	 * @return a string, representing the current state of the component,
	 *	including all its child components
	 */
	string Serialize(SerializationContext& context) const;

	/**
	 * \brief Deserializes a string into a component tree.
	 *
	 * @param str The string to deserialize.
	 * @param pos Initial deserialization position in the string; should
	 *	be 0 when invoked manually. (Used for recursion, to avoid
	 *	copying.)
	 * @return If deserialization was successful, the
	 *	reference counted pointer will point to a component tree;
	 *	on error, it will be set to NULL
	 */
	static refcount_ptr<Component> Deserialize(
	    const string& str, size_t& pos);

	/**
	 * \brief Checks consistency by serializing and deserializing the
	 *	component (including all its child components), and comparing
	 *	the checksum of the original tree with the deserialized tree.
	 *
	 * @return true if the serialization/deserialization was correct,
	 *	false if there was some inconsistency
	 */
	bool CheckConsistency() const;

	/**
	 * \brief Adds this component's state, including children, to
	 *	a checksum.
	 *
	 * @param checksum The checksum to add to.
	 */
	void AddChecksum(Checksum& checksum) const;
	
private:
	/**
	 * \brief Disallow creation of %Component objects using the
	 * default constructor.
	 */
	Component();

private:
	Component*		m_parentComponent;
	Components		m_childComponents;
	string			m_className;
	StateVariableMap	m_stateVariables;
};


#endif	// COMPONENT_H
