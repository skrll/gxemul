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
 *  $Id: ConfigNode.cc,v 1.3 2007-11-26 14:20:31 debug Exp $
 *
 *  A class describing a Configuration Node.
 */

#include "ConfigNode.h"
#include <iostream>


ConfigNode::ConfigNode(const string& strName)
	: m_strName(strName)
	, m_parentNode(NULL)
{
}


ConfigNode::~ConfigNode()
{
	if (!m_parentNode.IsNULL())
		m_parentNode->RemoveChild(this);

	std::cout << "Removing ConfigNode " << m_strName << "\n";
}


void ConfigNode::AddChild(ConfigNode* pNode)
{
	m_childNodes.insert(pNode);
}


void ConfigNode::RemoveChild(ConfigNode* pNode)
{
	ConfigNodes::iterator it = m_childNodes.find(pNode);
	if (it != m_childNodes.end())
		m_childNodes.erase(it);
}


string ConfigNode::ToString(int indentation) const
{
	string str;
	int i;

	for (i=0; i<indentation; i++)
		str += "\t";

	str += "\"" + m_strName + "\"\n";

	for (i=0; i<indentation; i++)
		str += "\t";

	str += "{\n";

	for (ConfigNodes::iterator it = m_childNodes.begin();
	    it != m_childNodes.end(); ++it) {
		if (it != m_childNodes.begin())
			str += "\n";

		str += (*it)->ToString(indentation + 1);
	}

	for (i=0; i<indentation; i++)
		str += "\t";

	str += "}\n";

	return str;
}


#ifdef TEST

int main(int argc, char *argv[])
{
	refcount_ptr<ConfigNode> rootNode = new ConfigNode("emulation");
	refcount_ptr<ConfigNode> machineB = new ConfigNode("machine");

	{
		refcount_ptr<ConfigNode> machineA = new ConfigNode("machine");
		refcount_ptr<ConfigNode> pcibus(new ConfigNode("pcibus"));

		rootNode->AddChild(machineA);
		rootNode->AddChild(machineB);

		machineA->AddChild(pcibus);

		std::cout << rootNode->ToString();

		rootNode->RemoveChild(machineA);
	}

	std::cout << rootNode->ToString();

	std::cout << "TEST OK: ConfigNode\n";
	return 0;
}

#endif

