//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Constructor decays
//    Handler is lvalue ref
//    Handler is const lvalue ref
//    Handler is rvalue
// Copy constructor works, no recursion
// Move constructor works, no recursion
// Non-error, then error
// Error, then non-error
// Error, then other error (first error wins)
// Errors are intercepted, even if they don't originate in error packets
// The output error is cleared on setup
