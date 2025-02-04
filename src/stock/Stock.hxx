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

#include "AbstractStock.hxx"
#include "Item.hxx"
#include "Request.hxx"
#include "Stats.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "io/Logger.hxx"
#include "util/DeleteDisposer.hxx"

#include <boost/intrusive/list.hpp>

#include <cstddef>
#include <string>

class CancellablePointer;
class Stock;
class StockClass;
class StockGetHandler;

class StockHandler {
public:
	/**
	 * The stock has become empty.  It is safe to delete it from
	 * within this method.
	 */
	virtual void OnStockEmpty(Stock &stock) noexcept = 0;
};

/**
 * Objects in stock.  May be used for connection pooling.
 *
 * A #Stock instance holds a number of idle objects.
 */
class Stock : public AbstractStock {
	StockClass &cls;

	const std::string name;

	/**
	 * The maximum number of items in this stock.  If any more items
	 * are requested, they are put into the #waiting list, which gets
	 * checked as soon as Put() is called.
	 */
	const std::size_t limit;

	/**
	 * The maximum number of permanent idle items.  If there are more
	 * than that, a timer will incrementally kill excess items.
	 */
	const std::size_t max_idle;

	const Event::Duration clear_interval;

	StockHandler *const handler;

	const Logger logger;

	/**
	 * This event is used to move the "retry waiting" code out of the
	 * current stack, to invoke the handler method in a safe
	 * environment.
	 */
	DeferEvent retry_event;

	/**
	 * This event is used to move the "empty" check out of the current
	 * stack, to invoke the handler method in a safe environment.
	 */
	DeferEvent empty_event;

	CoarseTimerEvent cleanup_event;
	CoarseTimerEvent clear_event;

	using ItemList =
		boost::intrusive::list<StockItem,
				       boost::intrusive::constant_time_size<true>>;

	ItemList idle;

	ItemList busy;

	std::size_t num_create = 0;

	struct Waiting;
	using WaitingList =
		boost::intrusive::list<Waiting,
				       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
				       boost::intrusive::constant_time_size<false>>;

	WaitingList waiting;

	bool may_clear = false;

public:
	/**
	 * @param name may be something like a hostname:port pair for HTTP
	 * client connections - it is used for logging, and as a key by
	 * the #MapStock class
	 */
	gcc_nonnull(4)
	Stock(EventLoop &event_loop, StockClass &cls,
	      const char *name, std::size_t limit, std::size_t max_idle,
	      Event::Duration _clear_interval,
	      StockHandler *handler=nullptr) noexcept;

	~Stock() noexcept;

	Stock(const Stock &) = delete;
	Stock &operator=(const Stock &) = delete;

	EventLoop &GetEventLoop() const noexcept override {
		return retry_event.GetEventLoop();
	}

	StockClass &GetClass() noexcept {
		return cls;
	}

	const char *GetName() const noexcept override {
		return name.c_str();
	}

	/**
	 * Returns true if there are no items in the stock - neither idle
	 * nor busy.
	 */
	[[gnu::pure]]
	bool IsEmpty() const noexcept {
		return idle.empty() && busy.empty() && num_create == 0;
	}

	/**
	 * @return true if the configured stock limit has been reached
	 * and no more items can be created, false if this stock is
	 * unlimited
	 */
	[[gnu::pure]]
	bool IsFull() const noexcept {
		return limit > 0 && GetActiveCount() >= limit;
	}

	/**
	 * Obtain statistics.
	 */
	void AddStats(StockStats &data) const noexcept {
		data.busy += busy.size();
		data.idle += idle.size();
	}

	/**
	 * Discard all items which are idle and havn't been used in a
	 * while.
	 */
	void DiscardUnused() noexcept;

	/**
	 * Destroy all idle items and don't reuse any of the current busy
	 * items.
	 */
	void FadeAll() noexcept;

	/**
	 * Destroy all matching idle items and don't reuse any of the
	 * matching busy items.
	 */
	template<typename P>
	void FadeIf(P &&predicate) noexcept {
		for (auto &i : busy)
			if (predicate(i))
				i.fade = true;

		ClearIdleIf(std::forward<P>(predicate));

		ScheduleCheckEmpty();
		// TODO: restart the "num_create" list?
	}

	/**
	 * Enable shutdown mode where all returned items will be
	 * destroyed and all events will be deregistered.
	 */
	void Shutdown() noexcept;

private:
	/**
	 * Determine the number of "active" items, i.e. the busy items
	 * and the ones being created (#num_create).  This number is
	 * used to compare with the configured #limit.
	 */
	std::size_t GetActiveCount() const noexcept {
		return busy.size() + num_create;
	}

	/**
	 * Check if the stock has become empty, and invoke the handler.
	 */
	void CheckEmpty() noexcept;
	void ScheduleCheckEmpty() noexcept;

	void ScheduleClear() noexcept {
		if (clear_interval > Event::Duration::zero())
			clear_event.Schedule(clear_interval);
	}

	void ClearIdle() noexcept;

	template<typename P>
	void ClearIdleIf(P &&predicate) noexcept {
		idle.remove_and_dispose_if(std::forward<P>(predicate),
					   DeleteDisposer());

		if (idle.size() <= max_idle)
			UnscheduleCleanup();
	}

public:
	/**
	 * Borrow an idle item.
	 *
	 * @return an item or nullptr if there was no (usable) idle
	 * item
	 */
	StockItem *GetIdle() noexcept;

	/**
	 * @param request a request that shall be destroyed before
	 * invoking the handler (to avoid use-after-free bugs)
	 */
	bool GetIdle(StockRequest &request,
		     StockGetHandler &handler) noexcept;

	void GetCreate(StockRequest request,
		       StockGetHandler &get_handler,
		       CancellablePointer &cancel_ptr) noexcept;

	void Get(StockRequest request,
		 StockGetHandler &get_handler,
		 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Obtains an item from the stock without going through the
	 * callback.  This requires a stock class which finishes the
	 * create() method immediately.
	 *
	 * Throws exception on error.
	 */
	StockItem *GetNow(StockRequest request);

	void Put(StockItem &item, bool destroy) noexcept override;

	void ItemIdleDisconnect(StockItem &item) noexcept override;
	void ItemCreateSuccess(StockItem &item) noexcept override;
	void ItemCreateError(StockGetHandler &get_handler,
			     std::exception_ptr ep) noexcept override;
	void ItemCreateAborted() noexcept override;

	void ItemUncleanFlagCleared() noexcept override {
		ScheduleRetryWaiting();
	}

private:
	/**
	 * Retry the waiting requests.  This is called after the number of
	 * busy items was reduced.
	 */
	void RetryWaiting() noexcept;
	void ScheduleRetryWaiting() noexcept;

	void ScheduleCleanup() noexcept {
		cleanup_event.Schedule(std::chrono::seconds(20));
	}

	void UnscheduleCleanup() noexcept {
		cleanup_event.Cancel();
	}

	void CleanupEventCallback() noexcept;
	void ClearEventCallback() noexcept;
};
