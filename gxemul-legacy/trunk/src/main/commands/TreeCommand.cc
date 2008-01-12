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
 *
 *
 *  $Id: TreeCommand.cc,v 1.2 2008-01-12 08:29:56 debug Exp $
 */

#include "components/DummyComponent.h"
#include "commands/TreeCommand.h"
#include "GXemul.h"


TreeCommand::TreeCommand()
	: Command("tree", "")
{
}


TreeCommand::~TreeCommand()
{
}


static void ShowMsg(GXemul& gxemul, const string& msg)
{
	gxemul.GetUI()->ShowDebugMessage(msg);
}


static void ShowTree(GXemul& gxemul, refcount_ptr<Component> node,
	const string& branchTemplate)
{
	// Basically, this shows a tree which looks like:
	//
	//	root
	//	|-- child1
	//	|   |-- child1's child1
	//	|   \-- child1's child2
	//	\-- child2
	//	    \-- child2's child
	//
	// TODO: Comment this better.

	string branch;
	for (size_t pos=0; pos<branchTemplate.length(); pos++) {
		stringchar ch = branchTemplate[pos];
		if (ch == '\\') {
			if (pos < branchTemplate.length() - 4)
				branch += ' ';
			else
				branch += ch;
		} else {
			if (pos == branchTemplate.length() - 3 ||
			    pos == branchTemplate.length() - 2)
				ch = '-';
			branch += ch;
		}
	}

	// Fallback to showing the component's class name in parentheses...
	string name = "(unnamed " + node->GetClassName() + ")";

	// ... but prefer the state variable "name" if it is available:
	StateVariableValue value;
	if (node->GetVariable("name", &value))
		name = value.ToString();

	string str = branch + name;

	// If this component was created by a template, then show the template
	// type in [ ].
	StateVariableValue templateName;
	if (node->GetVariable("template", &templateName))
		str += "  [" + templateName.ToString() + "]";

	// Show the branch of the tree...
	ShowMsg(gxemul, "  " + str + "\n");

	// ... and recurse to show children, if there are any:
	const Components& children = node->GetChildren();
	for (size_t i=0, n=children.size(); i<n; ++i) {
		string subBranch = branchTemplate;
		if (i == n-1)
			subBranch += "\\   ";
		else
			subBranch += "|   ";

		ShowTree(gxemul, children[i], subBranch);
	}
}


void TreeCommand::Execute(GXemul& gxemul, const vector<string>& arguments)
{
	if (gxemul.GetRootComponent()->GetChildren().size() == 0)
		ShowMsg(gxemul, "The emulation is empty; no components have"
		    " been added yet.\n");
	else
		ShowTree(gxemul, gxemul.GetRootComponent(), "");
}


string TreeCommand::GetShortDescription() const
{
	return "Shows the component configuration tree.";
}


string TreeCommand::GetLongDescription() const
{
	return "Shows the component configuration tree.";
}

