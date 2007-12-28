#ifndef	REFCOUNT_PTR_H
#define	REFCOUNT_PTR_H

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
 *  $Id: refcount_ptr.h,v 1.4 2007-12-28 19:08:44 debug Exp $
 */

/**
 * \brief Base class for reference countable objects.
 *
 * Usage:
 *	refcount_ptr<MyClass> myPtr = new MyClass(...);
 *
 * where MyClass should have increase_refcount and
 * decrease_refcount, e.g.
 *
 * class MyClass : public ReferenceCountable
 * {
 *	...
 * }
 */
class ReferenceCountable
{
public:
	/**
	 * Default constructor, which initializes the reference
	 * count to zero.
 	 */
	ReferenceCountable()
		: m_refCount(0)
	{
	}

	~ReferenceCountable()
	{
	}

	/**
	 * Increases the reference count of the object.
	 * @return The reference count after increasing it.
	 */
	int increase_refcount()
	{
		return (++ m_refCount);
	}

	/**
	 * Decreases the reference count of the object.
	 * @return The reference count after decreasing it. If the
	 *	value is zero, the caller should delete the object.
	 */
	int decrease_refcount()
	{
		return (-- m_refCount);
	}

private:
	int	m_refCount;
};


/**
 * \brief A template class representing a reference counted pointer.
 *
 * Basically, when a pointer assigned to the reference counted pointer,
 * it increases the reference count of the pointed-to object.
 * When the reference counted pointer is destroyed (or NULL is
 * assigned to it), it decreases the reference count of the pointed-to
 * object. If the reference count reaches zero, the object is deleted.
 */
template <class T>
class refcount_ptr
{
public:
	/**
	 * Constructor for a reference counted pointer.
	 *
	 * @param p Pointer to an object; default is NULL.
	 */
	refcount_ptr(T* p = NULL)
		: m_p(p)
	{
		if (m_p != NULL)
			m_p->increase_refcount();
	}

	/**
	 * The destructor causes the reference count to be decreased
	 * by one. If the reference count of the object reaches zero,
	 * it is deleted (freed).
	 */
	~refcount_ptr()
	{
		release();
	}

	/**
	 * Copy constructor, which causes the reference count of the
	 * pointed-to object to be increased.
	 *
	 * @param other The reference counted pointer to copy from.
	 */
	refcount_ptr(const refcount_ptr& other)
		: m_p(other.m_p)
	{
		if (m_p != NULL)
			m_p->increase_refcount();
	}

	/**
	 * Assignment operator. If an object is already referenced,
	 * it is released (i.e. its reference is decreased, and if it
	 * is zero, it is freed). The object referenced to by the
	 * other reference counted pointer then gets its reference
	 * count increased.
	 *
	 * @param other The reference counted pointer to assign from.
	 */
	refcount_ptr& operator = (const refcount_ptr& other)
	{
		if (this != &other) {
			release();
			if ((m_p = other.m_p) != NULL)
				m_p->increase_refcount();
		}
		return *this;
	}

	/**
	 * Releases the currently references object, if any, by
	 * decreasing its reference count. If the count reaches zero,
	 * that means that no others have pointers to the object,
	 * and it is freed.
	 */
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

	/**
	 * \brief Checks whether or not an object is referenced
	 * by the reference counted pointer.
	 *
	 * @return true if the reference counted pointer is not
	 *	referencing any object, false otherwise.
	 */
	bool IsNULL() const
	{
		return m_p == NULL;
	}

	/**
	 * \brief Less-than operator, e.g. for sorting.
	 *
	 * @param other The reference counted pointer to
	 *	compare this object to.
	 * @return true if the plain pointer of this object is
	 *	less than the plain pointer of the other object.
	 */
	bool operator < (const refcount_ptr& other) const
	{
		return (size_t)m_p < (size_t)other.m_p;
	}

	/**
	 * \brief Equals operator.
	 *
	 * @param other The reference counted pointer to
	 *	compare this object to.
	 * @return true if the pointed to objects have the same
	 *	address, false otherwise.
	 */
	bool operator == (const refcount_ptr& other) const
	{
		return m_p == other.m_p;
	}

	/**
	 * \brief Not-Equals operator.
	 *
	 * @param other The reference counted pointer to
	 *	compare this object to.
	 * @return true if the pointed to objects have
	 *	different address, false otherwise.
	 */
	bool operator != (const refcount_ptr& other) const
	{
		return m_p != other.m_p;
	}

private:
	T*	m_p;
};

#endif	// REFCOUNT_PTR_H
