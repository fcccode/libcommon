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

#include "RBindSocket.hxx"
#include "AddressInfo.hxx"
#include "Resolver.hxx"
#include "SocketError.hxx"
#include "UniqueSocketDescriptor.hxx"

UniqueSocketDescriptor
ResolveBindSocket(const char *host_and_port, int default_port,
		  const struct addrinfo &hints)
{
	const auto ail = Resolve(host_and_port, default_port, &hints);
	const auto &ai = ail.GetBest();

	UniqueSocketDescriptor s;
	if (!s.CreateNonBlock(ai.GetFamily(), ai.GetType(), ai.GetProtocol()))
		throw MakeSocketError("Failed to create socket");

	if (!s.Bind(ai))
		throw MakeSocketError("Failed to bind");

	return s;
}

static UniqueSocketDescriptor
ResolveBindSocket(const char *host_and_port, int default_port, int socktype)
{
	return ResolveBindSocket(host_and_port, default_port,
				 MakeAddrInfo(AI_ADDRCONFIG|AI_PASSIVE,
					      AF_UNSPEC, socktype));
}

UniqueSocketDescriptor
ResolveBindStreamSocket(const char *host_and_port, int default_port)
{
	return ResolveBindSocket(host_and_port, default_port, SOCK_STREAM);
}

UniqueSocketDescriptor
ResolveBindDatagramSocket(const char *host_and_port, int default_port)
{
	return ResolveBindSocket(host_and_port, default_port, SOCK_DGRAM);
}
