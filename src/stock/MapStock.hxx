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

#include "Stock.hxx"
#include "event/Chrono.hxx"
#include "io/Logger.hxx"
#include "util/Cast.hxx"

#include <boost/intrusive/unordered_set.hpp>

#include <cassert>
#include <cstddef>

/**
 * A hash table of any number of Stock objects, each with a different
 * URI.
 */
class StockMap : StockHandler {
	struct Item
		: boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
		Stock stock;

		bool sticky = false;

		template<typename... Args>
		explicit Item(Args&&... args) noexcept:stock(std::forward<Args>(args)...) {}

		static constexpr Item &Cast(Stock &s) noexcept {
			return ContainerCast(s, &Item::stock);
		}

		const char *GetKey() const noexcept {
			return stock.GetName();
		}

		[[gnu::pure]]
		static size_t KeyHasher(const char *key) noexcept;

		[[gnu::pure]]
		static size_t ValueHasher(const Item &value) noexcept {
			return KeyHasher(value.GetKey());
		}

		[[gnu::pure]]
		static bool KeyValueEqual(const char *a, const Item &b) noexcept {
			assert(a != nullptr);

			return strcmp(a, b.GetKey()) == 0;
		}

		struct Hash {
			[[gnu::pure]]
			size_t operator()(const Item &value) const noexcept {
				return ValueHasher(value);
			}
		};

		struct Equal {
			[[gnu::pure]]
			bool operator()(const Item &a, const Item &b) const noexcept {
				return KeyValueEqual(a.GetKey(), b);
			}
		};
	};

	using Map =
		boost::intrusive::unordered_set<Item,
						boost::intrusive::hash<Item::Hash>,
						boost::intrusive::equal<Item::Equal>,
						boost::intrusive::constant_time_size<false>>;

	const Logger logger;

	EventLoop &event_loop;

	StockClass &cls;

	/**
	 * The maximum number of items in each stock.
	 */
	const std::size_t limit;

	/**
	 * The maximum number of permanent idle items in each stock.
	 */
	const std::size_t max_idle;

	const Event::Duration clear_interval;

	Map map;

	static constexpr size_t N_BUCKETS = 251;
	Map::bucket_type buckets[N_BUCKETS];

public:
	StockMap(EventLoop &_event_loop, StockClass &_cls,
		 std::size_t _limit, std::size_t _max_idle,
		 Event::Duration _clear_interval) noexcept;

	~StockMap() noexcept;

	EventLoop &GetEventLoop() const noexcept {
		return event_loop;
	}

	StockClass &GetClass() noexcept {
		return cls;
	}

	void Erase(Item &item) noexcept;

	/**
	 * Discard all items which are idle and havn't been used in a
	 * while.
	 */
	void DiscardUnused() noexcept {
		for (auto &i : map)
			i.stock.DiscardUnused();
	}

	/**
	 * @see Stock::FadeAll()
	 */
	void FadeAll() noexcept {
		for (auto &i : map)
			i.stock.FadeAll();
	}

	/**
	 * @see Stock::FadeIf()
	 */
	template<typename P>
	void FadeIf(P &&predicate) noexcept {
		for (auto &i : map)
			i.stock.FadeIf(predicate);
	}

	/**
	 * Obtain statistics.
	 */
	void AddStats(StockStats &data) const noexcept {
		for (const auto &i : map)
			i.stock.AddStats(data);
	}

	[[gnu::pure]]
	Stock &GetStock(const char *uri, void *request) noexcept;

	/**
	 * Set the "sticky" flag.  Sticky stocks will not be deleted
	 * when they become empty.
	 */
	void SetSticky(Stock &stock, bool sticky) noexcept;

	void Get(const char *uri, StockRequest &&request,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept {
		Stock &stock = GetStock(uri, request.get());
		stock.Get(std::move(request), handler, cancel_ptr);
	}

	/**
	 * Obtains an item from the stock without going through the
	 * callback.  This requires a stock class which finishes the
	 * create() method immediately.
	 *
	 * Throws exception on error.
	 */
	StockItem *GetNow(const char *uri, StockRequest &&request) {
		Stock &stock = GetStock(uri, request.get());
		return stock.GetNow(std::move(request));
	}

protected:
	/**
	 * Derived classes can override this method to choose a
	 * per-#Stock clear_interval.
	 */
	virtual Event::Duration GetClearInterval(void *) const noexcept {
		return clear_interval;
	}

private:
	/* virtual methods from class StockHandler */
	void OnStockEmpty(Stock &stock) noexcept override;
};
