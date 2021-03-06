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
 */

#ifdef WITH_GTKMM

#include <assert.h>
#include <gtkmm.h>

#include "ui/gtkmm/GtkmmUI.h"
#include "ui/gtkmm/GXemulWindow.h"


GtkmmUI::GtkmmUI(GXemul *gxemul)
	: UI(gxemul)
{
}


GtkmmUI::~GtkmmUI()
{
}


void GtkmmUI::Initialize()
{
	// No specific initialization necessary.
}


void GtkmmUI::ShowStartupBanner()
{
	// No startup banner.
}


void GtkmmUI::ShowDebugMessage(const string& msg)
{
	if (m_window != NULL)
		m_window->InsertText(msg);
}


void GtkmmUI::ShowCommandMessage(const string& command)
{
	stringstream ss;
	ss << "> " << command << "\n";

	if (m_window != NULL)
		m_window->InsertText(ss.str());
}


void GtkmmUI::FatalError(const string& msg)
{
	// Print to stderr in case the GUI crashes too.
	std::cerr << msg;
	std::cerr.flush();

	Gtk::MessageDialog dialog(*m_window, msg,
	    false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK);
        dialog.run();
}


void GtkmmUI::InputLineDone()
{
	std::cerr << "GtkmmUI::InputLineDone: TODO\n";
}


void GtkmmUI::RedisplayInputLine(const string& inputline,
    size_t cursorPosition)
{
	std::cerr << "GtkmmUI::RedisplayInputLine: TODO\n";
}


void GtkmmUI::Shutdown()
{
	if (m_window != NULL)
		m_window->ShutdownUI();
}


int GtkmmUI::MainLoop()
{
	if (m_window != NULL) {
		assert(false);
		delete m_window;
	}

	m_window = new GXemulWindow(m_gxemul);

	Gtk::Main::run(*m_window);

	delete m_window;
	m_window = NULL;

	return 0;
}


#endif	// WITH_GTKMM
