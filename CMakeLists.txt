cmake_minimum_required (VERSION 3.10)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Orderbook-cpp")

function(enable_strict_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

# Add source to this project's executable.
add_executable (Orderbook-cpp
    "src/main.cpp"
    "src/Orderbook.cpp"
    "src/itchparser.cpp"
    "src/moldudp64.cpp"
    "src/trace.cpp"
)
enable_strict_warnings(Orderbook-cpp)

# test orderbook
add_executable(test_orderbook test/test_orderbook.cpp src/orderbook.cpp)
enable_strict_warnings(test_orderbook)

add_executable(test_itchparser test/test_itchparser.cpp src/itchparser.cpp)
target_compile_features(test_itchparser PRIVATE cxx_std_20)
enable_strict_warnings(test_itchparser)

# mold udp
add_executable(test_moldudp64
    test/test_moldudp64.cpp
    src/moldudp64.cpp
    src/itchparser.cpp   # moldudp64.cpp depends on read_u16_be from here
)
target_include_directories(test_moldudp64 PRIVATE include)
target_compile_features(test_moldudp64 PRIVATE cxx_std_20)
enable_strict_warnings(test_moldudp64)

# Scoreboard Self-Checker Test
add_executable(scoreboard_test
    test/scoreboard_test.cpp
    src/Orderbook.cpp
    src/itchparser.cpp
    src/moldudp64.cpp
)
target_include_directories(scoreboard_test PRIVATE include)
target_compile_features(scoreboard_test PRIVATE cxx_std_20)
enable_strict_warnings(scoreboard_test)

# Generator Test
add_executable(gen_test
    test/gen_test.cpp
)
target_include_directories(gen_test PRIVATE include)
target_compile_features(gen_test PRIVATE cxx_std_20)
enable_strict_warnings(gen_test)

target_include_directories(Orderbook-cpp PRIVATE include)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Orderbook-cpp PROPERTY CXX_STANDARD 20)
endif()