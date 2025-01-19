
cmake_minimum_required(VERSION 3.8.0)
project(nativepg)

find_package(Boost REQUIRED COMPONENTS headers)
find_package(OpenSSL REQUIRED)


add_library(nativepg
    src/error.cpp
    src/messages.cpp
)
target_link_libraries(nativepg PUBLIC Boost::headers OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(nativepg PUBLIC include)

add_executable(main test/main.cpp)
target_link_libraries(main PRIVATE nativepg)
