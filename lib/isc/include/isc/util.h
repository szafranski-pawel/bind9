/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#pragma once

#include <inttypes.h>

/*! \file isc/util.h
 * NOTE:
 *
 * This file is not to be included from any <isc/???.h> (or other) library
 * files.
 *
 * \brief
 * Including this file puts several macros in your name space that are
 * not protected (as all the other ISC functions/macros do) by prepending
 * ISC_ or isc_ to the name.
 */

/***
 *** General Macros.
 ***/

/*%
 * Use this to hide unused function arguments.
 * \code
 * int
 * foo(char *bar)
 * {
 *	UNUSED(bar);
 * }
 * \endcode
 */
#define UNUSED(x) (void)(x)

#if __GNUC__ >= 8 && !defined(__clang__)
#define ISC_NONSTRING __attribute__((nonstring))
#else /* if __GNUC__ >= 8 && !defined(__clang__) */
#define ISC_NONSTRING
#endif /* __GNUC__ */

#if HAVE_FUNC_ATTRIBUTE_CONSTRUCTOR && HAVE_FUNC_ATTRIBUTE_DESTRUCTOR
#define ISC_CONSTRUCTOR __attribute__((constructor))
#define ISC_DESTRUCTOR	__attribute__((destructor))
#else
#define ISC_CONSTRUCTOR
#define ISC_DESTRUCTOR
#endif

/*%
 * The opposite: silent warnings about stored values which are never read.
 */
#define POST(x) (void)(x)

#define ISC_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ISC_MIN(a, b) ((a) < (b) ? (a) : (b))

#define ISC_CLAMP(v, x, y) ((v) < (x) ? (x) : ((v) > (y) ? (y) : (v)))

/*%
 * Use this to remove the const qualifier of a variable to assign it to
 * a non-const variable or pass it as a non-const function argument ...
 * but only when you are sure it won't then be changed!
 * This is necessary to sometimes shut up some compilers
 * (as with gcc -Wcast-qual) when there is just no other good way to avoid the
 * situation.
 */
#define DE_CONST(konst, var)           \
	do {                           \
		union {                \
			const void *k; \
			void	     *v; \
		} _u;                  \
		_u.k = konst;          \
		var = _u.v;            \
	} while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/*%
 * Use this in translation units that would otherwise be empty, to
 * suppress compiler warnings.
 */
#define EMPTY_TRANSLATION_UNIT extern int isc__empty;

/*%
 * We use macros instead of calling the routines directly because
 * the capital letters make the locking stand out.
 * We RUNTIME_CHECK for success since in general there's no way
 * for us to continue if they fail.
 */

#ifdef ISC_UTIL_TRACEON
#define ISC_UTIL_TRACE(a) a
#include <stdio.h> /* Required for fprintf/stderr when tracing. */
#else		   /* ifdef ISC_UTIL_TRACEON */
#define ISC_UTIL_TRACE(a)
#endif /* ifdef ISC_UTIL_TRACEON */

#include <isc/result.h> /* Contractual promise. */

#define LOCK(lp)                                                           \
	do {                                                               \
		ISC_UTIL_TRACE(fprintf(stderr, "LOCKING %p %s %d\n", (lp), \
				       __FILE__, __LINE__));               \
		RUNTIME_CHECK(isc_mutex_lock((lp)) == ISC_R_SUCCESS);      \
		ISC_UTIL_TRACE(fprintf(stderr, "LOCKED %p %s %d\n", (lp),  \
				       __FILE__, __LINE__));               \
	} while (0)
#define UNLOCK(lp)                                                          \
	do {                                                                \
		RUNTIME_CHECK(isc_mutex_unlock((lp)) == ISC_R_SUCCESS);     \
		ISC_UTIL_TRACE(fprintf(stderr, "UNLOCKED %p %s %d\n", (lp), \
				       __FILE__, __LINE__));                \
	} while (0)

#define BROADCAST(cvp)                                                        \
	do {                                                                  \
		ISC_UTIL_TRACE(fprintf(stderr, "BROADCAST %p %s %d\n", (cvp), \
				       __FILE__, __LINE__));                  \
		RUNTIME_CHECK(isc_condition_broadcast((cvp)) ==               \
			      ISC_R_SUCCESS);                                 \
	} while (0)
#define SIGNAL(cvp)                                                          \
	do {                                                                 \
		ISC_UTIL_TRACE(fprintf(stderr, "SIGNAL %p %s %d\n", (cvp),   \
				       __FILE__, __LINE__));                 \
		RUNTIME_CHECK(isc_condition_signal((cvp)) == ISC_R_SUCCESS); \
	} while (0)
#define WAIT(cvp, lp)                                                         \
	do {                                                                  \
		ISC_UTIL_TRACE(fprintf(stderr, "WAIT %p LOCK %p %s %d\n",     \
				       (cvp), (lp), __FILE__, __LINE__));     \
		RUNTIME_CHECK(isc_condition_wait((cvp), (lp)) ==              \
			      ISC_R_SUCCESS);                                 \
		ISC_UTIL_TRACE(fprintf(stderr, "WAITED %p LOCKED %p %s %d\n", \
				       (cvp), (lp), __FILE__, __LINE__));     \
	} while (0)

/*
 * isc_condition_waituntil can return ISC_R_TIMEDOUT, so we
 * don't RUNTIME_CHECK the result.
 *
 *  XXX Also, can't really debug this then...
 */

#define WAITUNTIL(cvp, lp, tp) isc_condition_waituntil((cvp), (lp), (tp))

#define RWLOCK(lp, t)                                                         \
	do {                                                                  \
		ISC_UTIL_TRACE(fprintf(stderr, "RWLOCK %p, %d %s %d\n", (lp), \
				       (t), __FILE__, __LINE__));             \
		RUNTIME_CHECK(isc_rwlock_lock((lp), (t)) == ISC_R_SUCCESS);   \
		ISC_UTIL_TRACE(fprintf(stderr, "RWLOCKED %p, %d %s %d\n",     \
				       (lp), (t), __FILE__, __LINE__));       \
	} while (0)
#define RWUNLOCK(lp, t)                                                       \
	do {                                                                  \
		ISC_UTIL_TRACE(fprintf(stderr, "RWUNLOCK %p, %d %s %d\n",     \
				       (lp), (t), __FILE__, __LINE__));       \
		RUNTIME_CHECK(isc_rwlock_unlock((lp), (t)) == ISC_R_SUCCESS); \
	} while (0)

/*
 * List Macros.
 */
#include <isc/list.h> /* Contractual promise. */

#define LIST(type)		       ISC_LIST(type)
#define INIT_LIST(type)		       ISC_LIST_INIT(type)
#define LINK(type)		       ISC_LINK(type)
#define INIT_LINK(elt, link)	       ISC_LINK_INIT(elt, link)
#define HEAD(list)		       ISC_LIST_HEAD(list)
#define TAIL(list)		       ISC_LIST_TAIL(list)
#define EMPTY(list)		       ISC_LIST_EMPTY(list)
#define PREV(elt, link)		       ISC_LIST_PREV(elt, link)
#define NEXT(elt, link)		       ISC_LIST_NEXT(elt, link)
#define APPEND(list, elt, link)	       ISC_LIST_APPEND(list, elt, link)
#define PREPEND(list, elt, link)       ISC_LIST_PREPEND(list, elt, link)
#define UNLINK(list, elt, link)	       ISC_LIST_UNLINK(list, elt, link)
#define ENQUEUE(list, elt, link)       ISC_LIST_APPEND(list, elt, link)
#define DEQUEUE(list, elt, link)       ISC_LIST_UNLINK(list, elt, link)
#define INSERTBEFORE(li, b, e, ln)     ISC_LIST_INSERTBEFORE(li, b, e, ln)
#define INSERTAFTER(li, a, e, ln)      ISC_LIST_INSERTAFTER(li, a, e, ln)
#define APPENDLIST(list1, list2, link) ISC_LIST_APPENDLIST(list1, list2, link)

/*%
 * Performance
 */

#ifdef HAVE_BUILTIN_UNREACHABLE
#define ISC_UNREACHABLE() __builtin_unreachable();
#else /* ifdef HAVE_BUILTIN_UNREACHABLE */
#define ISC_UNREACHABLE()
#endif /* ifdef HAVE_BUILTIN_UNREACHABLE */

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif /* if !defined(__has_feature) */

/* GCC defines __SANITIZE_ADDRESS__, so reuse the macro for clang */
#if __has_feature(address_sanitizer)
#define __SANITIZE_ADDRESS__ 1
#endif /* if __has_feature(address_sanitizer) */

#if __has_feature(thread_sanitizer)
#define __SANITIZE_THREAD__ 1
#endif /* if __has_feature(thread_sanitizer) */

#if __SANITIZE_THREAD__
#define ISC_NO_SANITIZE_THREAD __attribute__((no_sanitize("thread")))
#else /* if __SANITIZE_THREAD__ */
#define ISC_NO_SANITIZE_THREAD
#endif /* if __SANITIZE_THREAD__ */

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR >= 6)
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#elif __has_feature(c_static_assert)
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else /* if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR >= 6) */

/* Courtesy of Joseph Quinsey: https://godbolt.org/z/K9RvWS */
#define TOKENPASTE(a, b)	a##b /* "##" is the "Token Pasting Operator" */
#define EXPAND_THEN_PASTE(a, b) TOKENPASTE(a, b) /* expand then paste */
#define STATIC_ASSERT(x, msg) \
	enum { EXPAND_THEN_PASTE(ASSERT_line_, __LINE__) = 1 / ((msg) && (x)) }
#endif /* if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR >= 6) */

#ifdef UNIT_TESTING
extern void
mock_assert(const int result, const char *const expression,
	    const char *const file, const int line);
/*
 *	Allow clang to determine that the following code is not reached
 *	by calling abort() if the condition fails.  The abort() will
 *	never be executed as mock_assert() and _assert_true() longjmp
 *	or exit if the condition is false.
 */
#define REQUIRE(expression)                                                   \
	((!(expression))                                                      \
		 ? (mock_assert(0, #expression, __FILE__, __LINE__), abort()) \
		 : (void)0)
#define ENSURE(expression)                                                    \
	((!(int)(expression))                                                 \
		 ? (mock_assert(0, #expression, __FILE__, __LINE__), abort()) \
		 : (void)0)
#define INSIST(expression)                                                    \
	((!(expression))                                                      \
		 ? (mock_assert(0, #expression, __FILE__, __LINE__), abort()) \
		 : (void)0)
#define INVARIANT(expression)                                                 \
	((!(expression))                                                      \
		 ? (mock_assert(0, #expression, __FILE__, __LINE__), abort()) \
		 : (void)0)
#define _assert_true(c, e, f, l) \
	((c) ? (void)0 : (_assert_true(0, e, f, l), abort()))
#define _assert_int_equal(a, b, f, l) \
	(((a) == (b)) ? (void)0 : (_assert_int_equal(a, b, f, l), abort()))
#define _assert_int_not_equal(a, b, f, l) \
	(((a) != (b)) ? (void)0 : (_assert_int_not_equal(a, b, f, l), abort()))
#else /* UNIT_TESTING */

#ifndef CPPCHECK

/*
 * Assertions
 */
#include <isc/assertions.h> /* Contractual promise. */

/*% Require Assertion */
#define REQUIRE(e)   ISC_REQUIRE(e)
/*% Ensure Assertion */
#define ENSURE(e)    ISC_ENSURE(e)
/*% Insist Assertion */
#define INSIST(e)    ISC_INSIST(e)
/*% Invariant Assertion */
#define INVARIANT(e) ISC_INVARIANT(e)

#else /* CPPCHECK */

/*% Require Assertion */
#define REQUIRE(e) \
	if (!(e))  \
	abort()
/*% Ensure Assertion */
#define ENSURE(e) \
	if (!(e)) \
	abort()
/*% Insist Assertion */
#define INSIST(e) \
	if (!(e)) \
	abort()
/*% Invariant Assertion */
#define INVARIANT(e) \
	if (!(e))    \
	abort()

#endif /* CPPCHECK */

#endif /* UNIT_TESTING */

/*
 * Errors
 */
#include <isc/error.h> /* Contractual promise. */

/*% Unexpected Error */
#define UNEXPECTED_ERROR isc_error_unexpected
/*% Fatal Error */
#define FATAL_ERROR isc_error_fatal

#ifdef UNIT_TESTING

#define RUNTIME_CHECK(expression)                                             \
	((!(expression))                                                      \
		 ? (mock_assert(0, #expression, __FILE__, __LINE__), abort()) \
		 : (void)0)

#else /* UNIT_TESTING */

#ifndef CPPCHECK
/*% Runtime Check */
#define RUNTIME_CHECK(cond) ISC_ERROR_RUNTIMECHECK(cond)
#else /* ifndef CPPCHECK */
#define RUNTIME_CHECK(e) \
	if (!(e))        \
	abort()
#endif /* ifndef CPPCHECK */

#endif /* UNIT_TESTING */

/*%
 * Time
 */
#define TIME_NOW(tp) RUNTIME_CHECK(isc_time_now((tp)) == ISC_R_SUCCESS)
#define TIME_NOW_HIRES(tp) \
	RUNTIME_CHECK(isc_time_now_hires((tp)) == ISC_R_SUCCESS)

/*%
 * Alignment
 */
#ifdef __GNUC__
#define ISC_ALIGN(x, a) (((x) + (a)-1) & ~((typeof(x))(a)-1))
#else /* ifdef __GNUC__ */
#define ISC_ALIGN(x, a) (((x) + (a)-1) & ~((uintmax_t)(a)-1))
#endif /* ifdef __GNUC__ */

/*%
 * Misc
 */
#include <isc/deprecated.h>
