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

#include "net/IPv4Address.hxx"

#include <gtest/gtest.h>

TEST(IPv4AddressTest, Basic)
{
	IPv4Address dummy;
	EXPECT_EQ(dummy.GetSize(), sizeof(struct sockaddr_in));
}

TEST(IPv4AddressTest, Port)
{
	IPv4Address a(12345);
	EXPECT_EQ(a.GetPort(), 12345u);

	a.SetPort(42);
	EXPECT_EQ(a.GetPort(), 42u);
}

TEST(IPv4AddressTest, NumericAddress)
{
	IPv4Address a(12345);
	EXPECT_EQ(a.GetNumericAddress(), 0u);
	EXPECT_EQ(a.GetNumericAddressBE(), 0u);

	a = IPv4Address(192, 168, 1, 2, 42);
	EXPECT_EQ(a.GetNumericAddress(), 0xc0a80102);
	EXPECT_EQ(a.GetNumericAddressBE(), ToBE32(0xc0a80102));
}

TEST(IPv4AddressTest, Mask)
{
	EXPECT_EQ(IPv4Address::MaskFromPrefix(0).GetNumericAddress(),
		  IPv4Address(0, 0, 0, 0, 0).GetNumericAddress());
	EXPECT_EQ(IPv4Address::MaskFromPrefix(1).GetNumericAddress(),
		  IPv4Address(128, 0, 0, 0, 0).GetNumericAddress());
	EXPECT_EQ(IPv4Address::MaskFromPrefix(23).GetNumericAddress(),
		  IPv4Address(255, 255, 254, 0, 0).GetNumericAddress());
	EXPECT_EQ(IPv4Address::MaskFromPrefix(24).GetNumericAddress(),
		  IPv4Address(255, 255, 255, 0, 0).GetNumericAddress());
	EXPECT_EQ(IPv4Address::MaskFromPrefix(32).GetNumericAddress(),
		  IPv4Address(255, 255, 255, 255, 0).GetNumericAddress());
}

TEST(IPv4AddressTest, And)
{
	EXPECT_EQ((IPv4Address::MaskFromPrefix(32) &
		   IPv4Address(192, 168, 1, 2, 0)).GetNumericAddress(),
		  IPv4Address(192, 168, 1, 2, 0).GetNumericAddress());
	EXPECT_EQ((IPv4Address::MaskFromPrefix(24) &
		   IPv4Address(192, 168, 1, 2, 0)).GetNumericAddress(),
		  IPv4Address(192, 168, 1, 0, 0).GetNumericAddress());
	EXPECT_EQ((IPv4Address::MaskFromPrefix(16) &
		   IPv4Address(192, 168, 1, 2, 0)).GetNumericAddress(),
		  IPv4Address(192, 168, 0, 0, 0).GetNumericAddress());
	EXPECT_EQ((IPv4Address::MaskFromPrefix(8) &
		   IPv4Address(192, 168, 1, 2, 0)).GetNumericAddress(),
		  IPv4Address(192, 0, 0, 0, 0).GetNumericAddress());
	EXPECT_EQ((IPv4Address::MaskFromPrefix(0) &
		   IPv4Address(192, 168, 1, 2, 0)).GetNumericAddress(),
		  IPv4Address(0, 0, 0, 0, 0).GetNumericAddress());
}
