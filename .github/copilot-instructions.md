# nativepg Copilot instructions

## Project overview

- `nativepg` is a C++20 library implementing the PostgreSQL wire protocol. It provides protocol serialization/parsing, Sans-I/O state machines, request/response handling, and optional Corosio-based connection APIs.
- Public API headers are in `include/nativepg/`. Keep stable, consumer-facing declarations there.
- Protocol message types and state machines are in `include/nativepg/protocol/`; implementation details that must be exposed to other library components belong in its `detail/` subtree.
- Compiled implementation files are in `src/`. Private helpers belong in `src/nativepg_internal/` and must not be added to the public API.
- Corosio support is optional. Code and examples that depend on it must remain guarded by the `NATIVEPG_COROSIO_API` CMake option.

## Implementation rules

- Preserve the PostgreSQL protocol's framing, message ordering, error-recovery, and text/binary format semantics. Add focused tests for each valid and invalid protocol path.
- Keep network-independent behavior in protocol state machines; do not introduce I/O into the Sans-I/O layer.
- Prefer the established Boost facilities (`boost::system::error_code`, `boost::span`, `boost::compat::function_ref`, and Boost assertions) over introducing competing abstractions.
- Public headers must be self-contained, use include guards named `NATIVEPG_<PATH>_HPP`, and avoid exposing private `src/` headers.
- Match existing ownership and error conventions: return `boost::system::error_code` for recoverable protocol errors, and use exceptions only where the existing API does.
- Add the existing Boost Software License copyright header to every new C++ source or header file.
- Do not allocate unnecessary memory (heap allocations, dynamic containers, exceptions on hot/valid paths, etc.). Only allocate when there is no reasonable alternative; prefer stack storage, fixed-size types, and non-throwing/`noexcept` parsing APIs (e.g. `from_chars`) over throwing constructors.
- Do not use double underscores (`__`) anywhere in identifiers (class, function, method, variable, macro, or namespace names) — such names are reserved for the implementation.

## Style

- Follow `.clang-format`: four spaces, no tabs, 110-column limit, left-aligned pointers, and Allman braces for classes, functions, and control statements.
- Keep include groups ordered as Boost headers, standard-library headers, then project headers. Use quoted includes for project headers.
- Use `snake_case` for functions, variables, files, and namespaces; use `lowercase` names for types and `enum class` values.
- Follow the existing namespace-closing form: `}  // namespace nativepg` (or the fully qualified namespace).
- Use C++20 features already used by the project, such as `std::span`, designated initializers, and `std::source_location`, but do not require a newer standard.

## Tests

- Unit tests live in `test/unit/`, mirroring the source area: protocol tests in `test/unit/protocol/`, internal tests in `test/unit/nativepg_internal/`, and type tests in `test/unit/types/`.
- Tests are standalone executables using `boost::core::lightweight_test`; use `test_range_eq` from `test_utils/test_range_eq.hpp` for comparing ranges, and finish `main` with `boost::report_errors()`.
- Register each new unit-test source in the `BUILD_TESTING` block in `test/CMakeLists.txt` using `nativepg_add_test`. Do not add an unregistered test file.
- For serialization changes, assert exact payload bytes and the expected protocol-message sequence, following the existing request and protocol tests.

## Build and validation

- Required dependencies are Boost headers and OpenSSL. Enabling `NATIVEPG_COROSIO_API` additionally requires Boost.Corosio.
- Configure a normal debug build with:
  ```sh
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DBUILD_TESTING=ON
  ```
- Build and run tests with:
  ```sh
  cmake --build build
  ctest --test-dir build --output-on-failure
  ```
- Format modified C++ sources and headers with `clang-format -i` before submitting. The CI matrix builds C++20 with GCC and Clang in Debug and Release; Debug jobs use AddressSanitizer and UndefinedBehaviorSanitizer.
- Before submitting, review and analyse modified C++ sources and headers with the clang static analyzer (`clang++ --analyze`) and format them with `clang-format -i`; resolve any warnings raised.
- Use `cppcheck` as an additional static analyzer when it is available on the system, and fix any errors and warnings it reports accordingly.
