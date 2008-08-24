#ifndef	GXMVCF_H
#define	GXMVCF_H

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
 */

/*! \mainpage Source code documentation
 *  
 * This is the automatically generated Doxygen documentation for GXmvcf,
 * built from comments throughout the source code.
 *      
 * \section intro_sec Introduction - What is GXmvcf?
 *
 * GXmvcf is a lightweight (.h files only) <a href=
 * "http://en.wikipedia.org/wiki/Model-view-controller">Model-View-Controller
 * framework</a>, written in and for C++.
 * 
 * \section concepts_sec Core concepts
 * 
 * \subsection components_subsec Model, View, and Controller
 *
 * The Model contains the plain data.
 *
 * Views are parts of the UI, which render the Model in a form appropriate for
 * the end-user. For example, a text UI renders the data in text form, and
 * a GUI renders the data using various graphical components.
 *
 * The Controller is the glue binding it all together, i.e. it controls how
 * Model classes and View classes talk to each other. Whenever the user (via
 * the UI) wishes to modify the data in the model, the corresponding View
 * asks the Controller to do the modification.
 *
 * TODO: Longer description.
 */

#include <string>
using std::string;

#include <sstream>
using std::stringstream;

// Reference counting is needed in lots of places, so it is best to
// include it from this file.
#include "refcount_ptr.h"  


#endif	/*  GXMVCF_H  */
