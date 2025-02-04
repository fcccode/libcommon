/*
 * Copyright 2015-2021 Max Kellermann <max.kellermann@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LUA_PUSH_LAMBDA_HXX
#define LUA_PUSH_LAMBDA_HXX

#include "Assert.hxx"
#include "util/Compiler.h"

extern "C" {
#include <lua.h>
}

#include <utility>

namespace Lua {

/**
 * Internal helper type generated by Lambda().  It is designed to be
 * optimized away.
 */
template<typename T>
struct _Lambda : T {
	template<typename U>
	_Lambda(U &&u):T(std::forward<U>(u)) {}
};

/**
 * Instantiate a #_Lambda instance for the according Push() overload.
 */
template<typename T>
static inline _Lambda<T>
Lambda(T &&t)
{
	return _Lambda<T>(std::forward<T>(t));
}

/**
 * Push a value on the Lua stack by invoking a C++ lambda.  With
 * C++17, we could use std::is_callable, but with C++14, we need the
 * detour with struct _Lambda and Lambda().
 */
template<typename T>
gcc_nonnull_all
static inline void
Push(lua_State *L, _Lambda<T> l)
{
	const ScopeCheckStack check_stack(L, 1);

	l();
}

} // namespace Lua

#endif
