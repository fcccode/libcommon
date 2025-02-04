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

#include "event/net/UdpListener.hxx"
#include "event/net/UdpHandler.hxx"

struct WasSocket;
class UniqueSocketDescriptor;

namespace Was {

class SimpleMultiServer;

class SimpleMultiServerHandler {
public:
	virtual void OnMultiWasNew(SimpleMultiServer &server,
				   WasSocket &&socket) noexcept = 0;
	virtual void OnMultiWasError(SimpleMultiServer &server,
				     std::exception_ptr error) noexcept = 0;
	virtual void OnMultiWasClosed(SimpleMultiServer &server) noexcept = 0;
};

/**
 * A "simple" WAS server connection.
 */
class SimpleMultiServer final
	: UdpHandler
{
	UdpListener socket;

	SimpleMultiServerHandler &handler;

public:
	SimpleMultiServer(EventLoop &event_loop,
			  UniqueSocketDescriptor _socket,
			  SimpleMultiServerHandler &_handler) noexcept;

	auto &GetEventLoop() const noexcept {
		return socket.GetEventLoop();
	}

private:
	/* virtual methods from class UdpHandler */
	bool OnUdpDatagram(ConstBuffer<void> payload,
			   WritableBuffer<UniqueFileDescriptor> fds,
			   SocketAddress address, int uid) override;

	bool OnUdpHangup() override {
		handler.OnMultiWasClosed(*this);
		return false;
	}

	void OnUdpError(std::exception_ptr e) noexcept override {
		handler.OnMultiWasError(*this, std::move(e));
	}
};

} // namespace Was
