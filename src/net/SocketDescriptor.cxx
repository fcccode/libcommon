/*
 * Copyright (C) 2012-2017 Max Kellermann <max.kellermann@gmail.com>
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

#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "IPv4Address.hxx"
#include "IPv6Address.hxx"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

int
SocketDescriptor::GetType() const noexcept
{
	assert(IsDefined());

	int type;
	socklen_t size = sizeof(type);
	return getsockopt(fd, SOL_SOCKET, SO_TYPE,
			  (char *)&type, &size) == 0
		? type
		: -1;
}

bool
SocketDescriptor::IsStream() const noexcept
{
	return GetType() == SOCK_STREAM;
}

#ifdef _WIN32

void
SocketDescriptor::Close()
{
	if (IsDefined())
		::closesocket(Steal());
}

#endif

SocketDescriptor
SocketDescriptor::Accept()
{
#if defined(__linux__) && !defined(__BIONIC__) && !defined(KOBO)
	int connection_fd = ::accept4(Get(), nullptr, nullptr, SOCK_CLOEXEC);
#else
	int connection_fd = ::accept(Get(), nullptr, nullptr);
#endif
	return connection_fd >= 0
		? SocketDescriptor(connection_fd)
		: Undefined();
}

SocketDescriptor
SocketDescriptor::AcceptNonBlock(StaticSocketAddress &address) const
{
	address.SetMaxSize();
#if defined(__linux__) && !defined(__BIONIC__) && !defined(KOBO)
	int connection_fd = ::accept4(Get(), address, &address.size,
				      SOCK_CLOEXEC|SOCK_NONBLOCK);
#else
	int connection_fd = ::accept(Get(), address, &address.size);
#endif
	return SocketDescriptor(connection_fd);
}

bool
SocketDescriptor::Connect(SocketAddress address)
{
	assert(address.IsDefined());

	return ::connect(Get(), address.GetAddress(), address.GetSize()) >= 0;
}

bool
SocketDescriptor::Create(int domain, int type, int protocol)
{
#ifdef _WIN32
	static bool initialised = false;
	if (!initialised) {
		WSADATA data;
		WSAStartup(MAKEWORD(2,2), &data);
		initialised = true;
	}
#endif

#ifdef SOCK_CLOEXEC
	/* implemented since Linux 2.6.27 */
	type |= SOCK_CLOEXEC;
#endif

	int new_fd = socket(domain, type, protocol);
	if (new_fd < 0)
		return false;

	Set(new_fd);
	return true;
}

bool
SocketDescriptor::CreateNonBlock(int domain, int type, int protocol)
{
#ifdef __linux__
	type |= SOCK_NONBLOCK;
#endif

	if (!Create(domain, type, protocol))
		return false;

#ifndef __linux__
	SetNonBlocking();
#endif

	return true;
}

#ifndef _WIN32

bool
SocketDescriptor::CreateSocketPair(int domain, int type, int protocol,
				 SocketDescriptor &a, SocketDescriptor &b)
{
#ifdef __linux__
	type |= SOCK_CLOEXEC;
#endif

	int fds[2];
	if (socketpair(domain, type, protocol, fds) < 0)
		return false;

	a = SocketDescriptor(fds[0]);
	b = SocketDescriptor(fds[1]);
	return true;
}

bool
SocketDescriptor::CreateSocketPairNonBlock(int domain, int type, int protocol,
					 SocketDescriptor &a, SocketDescriptor &b)
{
#ifdef __linux__
	type |= SOCK_NONBLOCK;
#endif
	if (!CreateSocketPair(domain, type, protocol, a, b))
		return false;

#ifndef __linux__
	a.SetNonBlocking();
	b.SetNonBlocking();
#endif

	return true;
}

#endif

int
SocketDescriptor::GetError()
{
	assert(IsDefined());

	int s_err = 0;
	socklen_t s_err_size = sizeof(s_err);
	return getsockopt(fd, SOL_SOCKET, SO_ERROR,
			  (char *)&s_err, &s_err_size) == 0
		? s_err
		: errno;
}

size_t
SocketDescriptor::GetOption(int level, int name,
			    void *value, size_t size) const
{
	assert(IsDefined());

	socklen_t size2 = size;
	return getsockopt(fd, level, name, value, &size2) == 0
		? size2
		: 0;
}

struct ucred
SocketDescriptor::GetPeerCredentials() const noexcept
{
	struct ucred cred;
	if (GetOption(SOL_SOCKET, SO_PEERCRED,
		      &cred, sizeof(cred)) < sizeof(cred))
		cred.pid = -1;
	return cred;
}

bool
SocketDescriptor::SetOption(int level, int name,
			    const void *value, size_t size)
{
	assert(IsDefined());

	return setsockopt(fd, level, name, value, size) == 0;
}

#ifdef __linux__

bool
SocketDescriptor::SetReuseAddress(bool value)
{
	return SetBoolOption(SOL_SOCKET, SO_REUSEADDR, value);
}

bool
SocketDescriptor::SetReusePort(bool value)
{
	return SetBoolOption(SOL_SOCKET, SO_REUSEPORT, value);
}

bool
SocketDescriptor::SetFreeBind(bool value)
{
	return SetBoolOption(IPPROTO_IP, IP_FREEBIND, value);
}

bool
SocketDescriptor::SetNoDelay(bool value)
{
	return SetBoolOption(IPPROTO_TCP, TCP_NODELAY, value);
}

bool
SocketDescriptor::SetCork(bool value)
{
	return SetBoolOption(IPPROTO_TCP, TCP_CORK, value);
}

bool
SocketDescriptor::SetTcpDeferAccept(const int &seconds)
{
	return SetOption(IPPROTO_TCP, TCP_DEFER_ACCEPT, &seconds, sizeof(seconds));
}

bool
SocketDescriptor::SetV6Only(bool value)
{
	return SetBoolOption(IPPROTO_IPV6, IPV6_V6ONLY, value);
}

bool
SocketDescriptor::SetBindToDevice(const char *name)
{
	return SetOption(SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name));
}

bool
SocketDescriptor::SetTcpFastOpen(int qlen)
{
	return SetOption(SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
}

bool
SocketDescriptor::AddMembership(const IPv4Address &address)
{
	struct ip_mreq r{address.GetAddress(), IPv4Address(0).GetAddress()};
	return setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			  &r, sizeof(r)) == 0;
}

bool
SocketDescriptor::AddMembership(const IPv6Address &address)
{
	struct ipv6_mreq r{address.GetAddress(), address.GetScopeId()};
	return setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
			  &r, sizeof(r)) == 0;
}

bool
SocketDescriptor::AddMembership(SocketAddress address)
{
	switch (address.GetFamily()) {
	case AF_INET:
		return AddMembership(IPv4Address(address));

	case AF_INET6:
		return AddMembership(IPv6Address(address));

	default:
		errno = EINVAL;
		return false;
	}
}

#endif

bool
SocketDescriptor::Bind(SocketAddress address)
{
	return bind(Get(), address.GetAddress(), address.GetSize()) == 0;
}

#ifdef __linux__

bool
SocketDescriptor::AutoBind()
{
	static constexpr sa_family_t family = AF_LOCAL;
	return Bind(SocketAddress((const struct sockaddr *)&family,
				  sizeof(family)));
}

#endif

bool
SocketDescriptor::Listen(int backlog)
{
	return listen(Get(), backlog) == 0;
}

StaticSocketAddress
SocketDescriptor::GetLocalAddress() const
{
	assert(IsDefined());

	StaticSocketAddress result;
	result.size = result.GetCapacity();
	if (getsockname(fd, result, &result.size) < 0)
		result.Clear();

	return result;
}

StaticSocketAddress
SocketDescriptor::GetPeerAddress() const
{
	assert(IsDefined());

	StaticSocketAddress result;
	result.size = result.GetCapacity();
	if (getpeername(fd, result, &result.size) < 0)
		result.Clear();

	return result;
}

ssize_t
SocketDescriptor::Read(void *buffer, size_t length)
{
	int flags = 0;
#ifndef _WIN32
	flags |= MSG_DONTWAIT;
#endif

	return ::recv(Get(), (char *)buffer, length, flags);
}

ssize_t
SocketDescriptor::Write(const void *buffer, size_t length)
{
	int flags = 0;
#ifdef __linux__
	flags |= MSG_NOSIGNAL;
#endif

	return ::send(Get(), (const char *)buffer, length, flags);
}

#ifdef _WIN32

int
SocketDescriptor::WaitReadable(int timeout_ms) const
{
	assert(IsDefined());

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(Get(), &rfds);

	struct timeval timeout, *timeout_p = nullptr;
	if (timeout_ms >= 0) {
		timeout.tv_sec = unsigned(timeout_ms) / 1000;
		timeout.tv_usec = (unsigned(timeout_ms) % 1000) * 1000;
		timeout_p = &timeout;
	}

	return select(Get() + 1, &rfds, nullptr, nullptr, timeout_p);
}

int
SocketDescriptor::WaitWritable(int timeout_ms) const
{
	assert(IsDefined());

	fd_set wfds;
	FD_ZERO(&wfds);
	FD_SET(Get(), &wfds);

	struct timeval timeout, *timeout_p = nullptr;
	if (timeout_ms >= 0) {
		timeout.tv_sec = unsigned(timeout_ms) / 1000;
		timeout.tv_usec = (unsigned(timeout_ms) % 1000) * 1000;
		timeout_p = &timeout;
	}

	return select(Get() + 1, nullptr, &wfds, nullptr, timeout_p);
}

#endif

ssize_t
SocketDescriptor::Read(void *buffer, size_t length,
		       StaticSocketAddress &address)
{
	int flags = 0;
#ifndef _WIN32
	flags |= MSG_DONTWAIT;
#endif

	socklen_t addrlen = address.GetCapacity();
	ssize_t nbytes = ::recvfrom(Get(), (char *)buffer, length, flags,
				    address, &addrlen);
	if (nbytes > 0)
		address.SetSize(addrlen);

	return nbytes;
}

ssize_t
SocketDescriptor::Write(const void *buffer, size_t length,
			SocketAddress address)
{
	int flags = 0;
#ifndef _WIN32
	flags |= MSG_DONTWAIT;
#endif
#ifdef __linux__
	flags |= MSG_NOSIGNAL;
#endif

	return ::sendto(Get(), (const char *)buffer, length, flags,
			address.GetAddress(), address.GetSize());
}

void
SocketDescriptor::Shutdown()
{
    shutdown(Get(), SHUT_RDWR);
}

void
SocketDescriptor::ShutdownRead()
{
    shutdown(Get(), SHUT_RD);
}

void
SocketDescriptor::ShutdownWrite()
{
    shutdown(Get(), SHUT_WR);
}
