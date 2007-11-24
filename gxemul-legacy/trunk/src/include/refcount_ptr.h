#ifndef	REFCOUNT_PTR_H
#define	REFCOUNT_PTR_H

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
 *  $Id: refcount_ptr.h,v 1.1 2007-11-24 10:05:22 debug Exp $
 *
 *  Reference counting wrapper template.
 *
 *  Usage:
 *		refcount_ptr<MyClass> myPtr;
 *
 *  where MyClass should have increase_refcount and
 *  decrease_refcount, e.g.
 *
 *		class MyClass : public ReferenceCountable
 *		{
 *			...
 *		}
 */


class ReferenceCountable
{
public:
	ReferenceCountable()
		: m_refCount(0)
	{
	}

	~ReferenceCountable()
	{
	}

	int increase_refcount()
	{
		return (++ m_refCount);
	}

	int decrease_refcount()
	{
		return (-- m_refCount);
	}

private:
	int	m_refCount;
};


template <class T>
class refcount_ptr
{
public:
	refcount_ptr(T* p = NULL)
		: m_p(p)
	{
		if (m_p != NULL)
			m_p->increase_refcount();
	}

	~refcount_ptr()
	{
		release();
	}

	refcount_ptr(const refcount_ptr& other)
		: m_p(other.m_p)
	{
		if (m_p != NULL)
			m_p->increase_refcount();
	}

	refcount_ptr& operator = (const refcount_ptr& other)
	{
		if (this != &other) {
			release();
			if ((m_p = other.m_p) != NULL)
				m_p->increase_refcount();
		}
		return *this;
	}

	void release()
	{
		if (m_p != NULL) {
			if (m_p->decrease_refcount() <= 0)
				delete m_p;
			m_p = NULL;
		}
	}

	operator T* ()
	{
		return m_p;
	}

	T& operator *()
	{
		return *m_p;
	}

	T* operator ->()
	{
		return m_p;
	}

	const T& operator *() const
	{
		return *m_p;
	}

	const T* operator ->() const
	{
		return m_p;
	}

	bool IsNULL() const
	{
		return m_p == NULL;
	}

	bool operator < (const refcount_ptr& other) const
	{
		return (size_t)m_p < (size_t)other.m_p;
	}

private:
	T*	m_p;
};

#endif	// REFCOUNT_PTR_H
