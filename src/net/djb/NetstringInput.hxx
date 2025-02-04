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

#ifndef NETSTRING_INPUT_HXX
#define NETSTRING_INPUT_HXX

#include "util/AllocatedArray.hxx"

#include <cstddef>
#include <cassert>
#include <cstdint>

class FileDescriptor;

/**
 * A netstring input buffer.
 */
class NetstringInput {
	enum class State {
		HEADER,
		VALUE,
		FINISHED,
	};

	State state = State::HEADER;

	char header_buffer[32];
	size_t header_position = 0;

	AllocatedArray<uint8_t> value;
	size_t value_position;

	const size_t max_size;

public:
	explicit NetstringInput(size_t _max_size) noexcept
		:max_size(_max_size) {}

	enum class Result {
		MORE,
		CLOSED,
		FINISHED,
	};

	/**
	 * Throws std::runtime_error on error.
	 */
	Result Receive(FileDescriptor fd);

	bool IsFinished() const noexcept {
		return state == State::FINISHED;
	}

	AllocatedArray<uint8_t> &GetValue() noexcept {
		assert(IsFinished());

		return value;
	}

private:
	Result ReceiveHeader(FileDescriptor fd);
	Result ValueData(size_t nbytes);
	Result ReceiveValue(FileDescriptor fd);
};

#endif
