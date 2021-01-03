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

#include "util/Compiler.h"

#include <cstdint>
#include <vector>

/**
 * This class helps with building a new VFS (virtual file system).  It
 * remembers which paths have a writable "tmpfs" and creates mount
 * points inside it.
 */
class VfsBuilder {
	struct Item;

	std::vector<Item> items;

	int old_umask = -1;

public:
	const uint_least32_t uid, gid;

	VfsBuilder(uint_least32_t _uid, uint_least32_t _gid) noexcept;
	~VfsBuilder() noexcept;

	void AddWritableRoot(const char *path);

	/**
	 * Throws if the mount point could not be created.
	 */
	void Add(const char *path);

	/**
	 * Throws if the mount point could not be opened.
	 */
	void MakeWritable();

	/**
	 * Schedule a remount of the most recently added mount point.
	 */
	void ScheduleRemount(int flags) noexcept;

	void Finish();

private:
	struct FindWritableResult;

	FindWritableResult FindWritable(const char *path) const;
};
