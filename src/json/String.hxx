/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
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

#pragma once

#include <boost/json.hpp>

#include <string_view>

namespace Json {

[[gnu::pure]]
static inline std::string_view
GetString(const boost::json::value &json) noexcept
{
	const auto *s = json.if_string();
	return s != nullptr
		? (std::string_view)*s
		: std::string_view{};
}

[[gnu::pure]]
static inline std::string_view
GetString(const boost::json::value *json) noexcept
{
	return json != nullptr
		? GetString(*json)
		: std::string_view{};
}

[[gnu::pure]]
static inline std::string_view
GetString(const boost::json::object &parent, std::string_view key) noexcept
{
	return GetString(parent.if_contains(key));
}

[[gnu::pure]]
static inline const char *
GetCString(const boost::json::value &json) noexcept
{
	const auto *s = json.if_string();
	return s != nullptr
		? s->c_str()
		: nullptr;
}

[[gnu::pure]]
static inline const char *
GetCString(const boost::json::value *json) noexcept
{
	return json != nullptr
		? GetCString(*json)
		: nullptr;
}

[[gnu::pure]]
static inline const char *
GetCString(const boost::json::object &parent, std::string_view key) noexcept
{
	return GetCString(parent.if_contains(key));
}

} // namespace Json
