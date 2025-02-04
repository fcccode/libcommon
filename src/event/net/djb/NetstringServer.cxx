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

#include "NetstringServer.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Compiler.h"
#include "util/ConstBuffer.hxx"

#include <stdexcept>

#include <string.h>

static constexpr auto busy_timeout = std::chrono::seconds(5);

NetstringServer::NetstringServer(EventLoop &event_loop,
				 UniqueSocketDescriptor fd,
				 size_t max_size) noexcept
	:event(event_loop, BIND_THIS_METHOD(OnEvent), fd.Release()),
	 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
	 input(max_size)
{
	event.ScheduleRead();
	timeout_event.Schedule(busy_timeout);
}

NetstringServer::~NetstringServer() noexcept
{
	event.Close();
}

bool
NetstringServer::SendResponse(const void *data, size_t size) noexcept
try {
	std::list<ConstBuffer<void>> list{{data, size}};
	generator(list);
	for (const auto &i : list)
		write.Push(i.data, i.size);

	switch (write.Write(GetSocket().ToFileDescriptor())) {
	case MultiWriteBuffer::Result::MORE:
		throw std::runtime_error("short write");

	case MultiWriteBuffer::Result::FINISHED:
		return true;
	}

	assert(false);
	gcc_unreachable();
} catch (...) {
	OnError(std::current_exception());
	return false;
}

bool
NetstringServer::SendResponse(const char *data) noexcept
{
	return SendResponse((const void *)data, strlen(data));
}

void
NetstringServer::OnEvent(unsigned flags) noexcept
try {
	if (flags & SocketEvent::ERROR)
		throw MakeErrno(GetSocket().GetError(), "Socket error");

	if (flags & SocketEvent::HANGUP) {
		OnDisconnect();
		return;
	}

	if (input.IsFinished()) {
		/* TODO: was garbage received or did the peer just
		   close the socket?  Maybe use EPOLLRDHUP? */
		OnDisconnect();
		return;
	}

	switch (input.Receive(GetSocket().ToFileDescriptor())) {
	case NetstringInput::Result::MORE:
		timeout_event.Schedule(busy_timeout);
		break;

	case NetstringInput::Result::CLOSED:
		OnDisconnect();
		break;

	case NetstringInput::Result::FINISHED:
		timeout_event.Cancel();
		OnRequest(std::move(input.GetValue()));
		break;
	}
} catch (...) {
	OnError(std::current_exception());
}

void
NetstringServer::OnTimeout() noexcept
{
	OnDisconnect();
}
