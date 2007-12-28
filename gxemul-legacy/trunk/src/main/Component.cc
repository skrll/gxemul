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
 *  $Id: Component.cc,v 1.4 2007-12-28 19:08:44 debug Exp $
 */

#include "assert.h"

#include "Component.h"

// For CreateComponent:
#include "components/DummyComponent.h"


Component::Component(const string& className)
	: m_parentComponent(NULL)
	, m_className(className)
{
}


Component::~Component()
{
}


bool Component::CreateComponent(const string& className,
	refcount_ptr<Component>& pComponent)
{
	pComponent = NULL;

	if (className == "dummy") {
		pComponent = new DummyComponent;
		return true;
	}
	
	return false;
}


string Component::GetClassName() const
{
	return m_className;
}


void Component::Reset()
{
	ResetVariables();

	// Recurse:
	for (size_t i = 0; i < m_childComponents.size(); ++ i)
		m_childComponents[i]->Reset();
}


void Component::ResetVariables()
{
	// Base implementation: Do nothing.
}


void Component::SetParent(Component* parentComponent)
{
	m_parentComponent = parentComponent;
}


Component* Component::GetParent()
{
	return m_parentComponent;
}


void Component::AddChild(refcount_ptr<Component> childComponent)
{
	m_childComponents.push_back(childComponent);

	// A component cannot have two parents.	
	assert(childComponent->GetParent() == NULL);

	childComponent->SetParent(this);
}


void Component::RemoveChild(Component* childToRemove)
{
	const size_t n = m_childComponents.size();
	
	for (size_t i = 0; i < n; ++ i) {
		if (childToRemove == m_childComponents[i]) {
			childToRemove->SetParent(NULL);
			
			// Unless the item we are removing is the last in the
			// vector...
			if (i != n - 1) {
				// ... remove item i by moving every item after
				// it back one step:
				for (size_t j = i; j < n - 1; ++j)
					m_childComponents[j] =
					    m_childComponents[j+1];
			}
			
			m_childComponents.pop_back();

			return;
		}
	}

	// Child not found? Should not happen.
	assert(false);
}


Components& Component::GetChildren()
{
	return m_childComponents;
}


bool Component::GetVariable(const string& name, StateVariableValue* valuePtr)
{
	StateVariableMap::const_iterator it = m_stateVariables.find(name);
	if (it == m_stateVariables.end())
		return false;
	
	if (valuePtr != NULL)
		*valuePtr = (it->second).GetValue();

	return true;
}


void Component::SetVariable(const string& name,
	const StateVariableValue& newValue)
{
	m_stateVariables[name] = StateVariable(name, newValue);
}


string Component::Serialize(SerializationContext& context) const
{
	SerializationContext subContext = context.Indented();

	string variables;
	for (StateVariableMap::const_iterator it = m_stateVariables.begin();
	    it != m_stateVariables.end();
	    ++ it)
		variables += (it->second).Serialize(subContext);

	string children;
	for (size_t i = 0; i < m_childComponents.size(); ++ i)
		children += "\n" + m_childComponents[i]->Serialize(subContext);
	
	return
	    context.Tabs() + "component " + m_className + "\n" +
	    context.Tabs() + "{\n" +
	    variables +
	    children +
	    context.Tabs() + "}\n";
}


static bool GetNextToken(const string& str, size_t& pos, string& token)
{
	token = "";

	size_t len = str.length();
	
	// Skip initial whitespace:
	while (pos < len &&
	    (str[pos] == ' '  || str[pos] == '\t' ||
	     str[pos] == '\r' || str[pos] == '\n'))
		++ pos;

	if (pos >= len)
		return false;

	// Get the token, until end-of-string or until whitespace is found:
	bool quoted = false;
	do {
		char ch = str[pos];

		if (!quoted) {
			if (ch == '"')
				quoted = true;
			if (ch == ' '  || ch == '\t' ||
			    ch == '\r' || ch == '\n')
				break;
			token += ch;
		} else {
			if (ch == '"')
				quoted = false;
			token += ch;
			if (ch == '\\' && pos < len-1)
				token += str[++pos];
		}

		++ pos;
	} while (pos < len);

	return true;
}


bool Component::Deserialize(const string& str, size_t& pos,
	refcount_ptr<Component>& deserializedTree)
{
	deserializedTree = NULL;
	string token;

	if (!GetNextToken(str, pos, token) || token != "component")
		return false;

	if (!GetNextToken(str, pos, token))
		return false;

	string className = token;

	if (!GetNextToken(str, pos, token) || token != "{")
		return false;

	if (!Component::CreateComponent(className, deserializedTree))
		return false;

	while (pos < str.length()) {
		size_t savedPos = pos;

		// Either 1)   variableType variableName "value"
		// or     2)   component classname { ...
		// or     3)   }     (end of current component)

		if (!GetNextToken(str, pos, token))
			return true;

		// Case 3:
		if (token == "}")
			return true;

		string name;
		if (!GetNextToken(str, pos, name))
			return false;

		if (token == "component") {
			// Case 2:
			refcount_ptr<Component> child;
			if (!Component::Deserialize(str, savedPos, child))
				return false;
			deserializedTree->AddChild(child);
			pos = savedPos;
		} else {
			// Case 1:
			string varType = token;
			string varValue;
			if (!GetNextToken(str, pos, varValue))
				return false;
			deserializedTree->SetVariable(name,
			    StateVariableValue(varType, varValue));
		}
	}

	return true;
}


void Component::AddChecksum(Checksum& checksum) const
{
	// Some random stuff is added between normal fields to the checksum.
	// This is to make it harder to get the same checksum for two different
	// objects, that just have some fields swapped, or such.

	checksum.Add(0x12491725abcef011LL);
	checksum.Add(m_className);
	
	SerializationContext dummyContext;

	// Add all state variables.
	for (StateVariableMap::const_iterator it = m_stateVariables.begin();
	    it != m_stateVariables.end();
	    ++ it) {
		checksum.Add(0x019fb87925addae1LL);
		checksum.Add((it->second).Serialize(dummyContext));
	}
	
	// Add all child components.
	for (size_t i = 0; i < m_childComponents.size(); ++ i) {
		checksum.Add(0xf98a7c7c109f0000LL + i);
		m_childComponents[i]->AddChecksum(checksum);
	}

	checksum.Add(0x90a1022497defa7aLL);
}


