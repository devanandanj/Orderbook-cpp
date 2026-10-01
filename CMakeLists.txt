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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(include)

# Self-contained exe outside the build dir (llvm-mingw / MinGW runtime DLLs).
function(link_static target)
    if (NOT MSVC)
        target_link_options(${target} PRIVATE -static)
    endif()
endfunction()

# Tests are assert-based: keep asserts live in Release too.
function(add_unit_test name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -UNDEBUG)
    enable_strict_warnings(${name})
    link_static(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_executable (Orderbook-cpp
        "src/main.cpp"
        "src/orderbook.cpp"
        "src/itchparser.cpp"
        "src/moldudp64.cpp"
        "src/trace.cpp"
        "src/snapshot.cpp"
)
enable_strict_warnings(Orderbook-cpp)
link_static(Orderbook-cpp)
target_compile_definitions(Orderbook-cpp PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

add_executable(gen_test test/gen_test.cpp)
enable_strict_warnings(gen_test)
link_static(gen_test)

enable_testing()
add_unit_test(test_orderbook  test/test_orderbook.cpp src/orderbook.cpp)
add_unit_test(test_itchparser test/test_itchparser.cpp src/itchparser.cpp)
add_unit_test(test_moldudp64  test/test_moldudp64.cpp src/moldudp64.cpp src/itchparser.cpp)
add_unit_test(test_snapshot   test/test_snapshot.cpp src/snapshot.cpp src/trace.cpp
                              src/orderbook.cpp src/itchparser.cpp)

# End-to-end: generate the stress feed, run the model, check its summary.
add_test(NAME gen_stress COMMAND gen_test)
set_tests_properties(gen_stress PROPERTIES FIXTURES_SETUP stress_feed)
add_test(NAME stress COMMAND Orderbook-cpp stress_test.mold ${CMAKE_BINARY_DIR}/snapshot.bin)
set_tests_properties(stress PROPERTIES
    FIXTURES_REQUIRED stress_feed
    PASS_REGULAR_EXPRESSION "bids=1 asks=1[\r\n]+Rejects [(]book full[)]: bid=2 ask=2 unknown_order_ref=3")
