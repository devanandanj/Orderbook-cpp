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

# The model proper: everything a driver (main, the C API, a test) links.
set(GOLDEN_SOURCES
        src/book.cpp
        src/ordertable.cpp
        src/pricebook.cpp
        src/command.cpp
        src/itchparser.cpp
        src/snapshot.cpp
        src/trace.cpp
)

add_executable (Orderbook-cpp src/main.cpp src/moldudp64.cpp ${GOLDEN_SOURCES})
enable_strict_warnings(Orderbook-cpp)
link_static(Orderbook-cpp)
target_compile_definitions(Orderbook-cpp PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# C API for testbenches: DPI-C (UVM, xsim) or ctypes (cocotb). See
# include/golden_api.h.
add_library(golden_api SHARED src/golden_api.cpp ${GOLDEN_SOURCES})
enable_strict_warnings(golden_api)

add_executable(gen_test test/gen_test.cpp)
enable_strict_warnings(gen_test)
link_static(gen_test)

enable_testing()
add_unit_test(test_itchparser test/test_itchparser.cpp src/itchparser.cpp)
add_unit_test(test_ordertable test/test_ordertable.cpp src/ordertable.cpp)
add_unit_test(test_pricebook  test/test_pricebook.cpp src/pricebook.cpp)
add_unit_test(test_book       test/test_book.cpp src/book.cpp src/ordertable.cpp src/pricebook.cpp)
add_unit_test(test_moldudp64  test/test_moldudp64.cpp src/moldudp64.cpp src/itchparser.cpp)
add_unit_test(test_snapshot   test/test_snapshot.cpp ${GOLDEN_SOURCES})
add_unit_test(test_command    test/test_command.cpp ${GOLDEN_SOURCES})
add_unit_test(test_golden_api test/test_golden_api.cpp src/golden_api.cpp ${GOLDEN_SOURCES})

# End-to-end: generate the stress feed, run the model, check its summary.
add_test(NAME gen_stress COMMAND gen_test)
set_tests_properties(gen_stress PROPERTIES FIXTURES_SETUP stress_feed)
# Expected values are derived by hand in test/gen_test.cpp's header.
# Each run writes its own trace/state in the build dir, so tests never
# overwrite each other's (or a developer's) <repo>/trace.txt.
add_test(NAME stress COMMAND Orderbook-cpp stress_test.mold ${CMAKE_BINARY_DIR}/snapshot.bin
                             --base-price 1000000 --symbol STRESS
                             --trace stress_trace.txt --state stress_state.txt)
set_tests_properties(stress PROPERTIES
    FIXTURES_REQUIRED stress_feed
    FIXTURES_SETUP stress_trace
    PASS_REGULAR_EXPRESSION "Processed 63 messages successfully.[\r\n]+Packets: 6 [(]1 heartbeats[)], sequence gaps: 0; applied 55, skipped 8[\r\n]+Symbol: STRESS   [(]locate 1[)][\r\n]+Base price: 1000000[\r\n]+Final BBO: bid=999000 x 25 [(]1 orders[)] ask=1002000 x 8 [(]1 orders[)][\r\n]+Counters: dropped=3 price_rejected=4 unknown_order_ref=5")

# Same feed with one packet's sequence number skipped ahead.
add_test(NAME stress_gap COMMAND Orderbook-cpp stress_gap.mold ${CMAKE_BINARY_DIR}/snapshot_gap.bin
                                 --base-price 1000000 --symbol STRESS
                                 --trace gap_trace.txt --state gap_state.txt)
set_tests_properties(stress_gap PROPERTIES
    FIXTURES_REQUIRED stress_feed
    PASS_REGULAR_EXPRESSION "sequence gaps: 1; applied 55")

# Without --symbol, OTHER's orders get in: proves the filter is what keeps
# them out, and that the model warns about it.
add_test(NAME stress_unfiltered COMMAND Orderbook-cpp stress_test.mold ${CMAKE_BINARY_DIR}/snapshot_all.bin
                                        --base-price 1000000
                                        --trace all_trace.txt --state all_state.txt)
set_tests_properties(stress_unfiltered PROPERTIES
    FIXTURES_REQUIRED stress_feed
    PASS_REGULAR_EXPRESSION "WARNING: book messages from 2 stock locates went into one book.*Final BBO: bid=999900 x 10")

# tools/show_book.py rebuilds the stress run's final ladder from its trace
# (and fails on its own if that disagrees with the trace's BBO).
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME show_book COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/show_book.py
                                    book stress_trace.txt)
    set_tests_properties(show_book PROPERTIES
        FIXTURES_REQUIRED stress_trace
        PASS_REGULAR_EXPRESSION "ASK +[$]100[.]20 +8 sh +1 ord   <- best ask.*BID +[$]99[.]90 +25 sh +1 ord   <- best bid.*BID +[$]90[.]00 +19 sh +19 ord")
endif()
