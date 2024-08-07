
#pragma once

#include <boost/system/error_category.hpp>

namespace nativepg {

const boost::system::error_category& get_client_category();

enum class client_errc : int
{
    /// An incomplete message was received from the server (indicates a deserialization error or
    /// packet mismatch).
    incomplete_message = 1,

    /// An unexpected value was found in a server-received message (indicates a deserialization
    /// error or packet mismatch).
    protocol_value_error,

    /// Unexpected extra bytes at the end of a message were received (indicates a deserialization
    /// error or packet mismatch).
    extra_bytes,
};

/// Creates an \ref error_code from a \ref client_errc.
inline boost::system::error_code make_error_code(client_errc error)
{
    return boost::system::error_code(static_cast<int>(error), get_client_category());
}

}  // namespace nativepg

namespace boost {
namespace system {

template <>
struct is_error_code_enum<::nativepg::client_errc>
{
    static constexpr bool value = true;
};

}  // namespace system
}  // namespace boost