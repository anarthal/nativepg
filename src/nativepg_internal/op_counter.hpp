//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_OP_COUNTER_HPP
#define NATIVEPG_OP_COUNTER_HPP

#include <boost/assert.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/io_task.hpp>

#include <cstddef>
#include <memory>

namespace nativepg::detail {

// A pending event counter. It allows waiting for outstanding
// operations to finish
class op_counter
{
    std::size_t count_{};
    boost::capy::async_event done_{};

    struct deleter
    {
        void operator()(op_counter* self) const { self->notify_finish(); }
    };

public:
    op_counter()
    {
        done_.set();  // counted starts at zero
    }

    // Notifies that a new operation is now outstanding
    void notify_start()
    {
        ++count_;
        done_.clear();
    }

    using guard = std::unique_ptr<op_counter, deleter>;

    // RAII version
    guard scoped_notify_start()
    {
        notify_start();
        return guard{this};
    }

    // Notifies that a new operation is no longer outstanding
    void notify_finish()
    {
        BOOST_ASSERT(count_ > 0u);
        if (--count_ == 0u)
            done_.set();
    }

    // Waits until all operations have finished.
    boost::capy::io_task<> wait()
    {
        // async_event::set() posts, so the count might change
        // while the event loop schedules us. This loop guards against this
        while (count_ > 0u)
            if (auto [ec] = co_await done_.wait(); ec)
                co_return {ec};
        co_return {};
    }
};

}  // namespace nativepg::detail

#endif
