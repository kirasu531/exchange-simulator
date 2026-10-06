cmake_minimum_required(VERSION 3.20)

project(exchange_simulator LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
)

FetchContent_MakeAvailable(Catch2)

include(Catch)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(exchange_core
	src/order_book.cpp
	src/matching_engine.cpp
)
add_library(network_utils
    src/network_utils.cpp
)

add_executable(exchange
    src/main.cpp
)
add_executable(exchange_tests
	tests/order_book_tests.cpp
)
add_executable(exchange_bench
	benchmarks/benchmark.cpp
)
add_executable(exchange_server
	src/server_main.cpp
)
add_executable(exchange_client
	src/client_main.cpp
)
add_executable(exchange_network
	benchmarks/network_bench.cpp
)
add_executable(exchange_durability
	benchmarks/durability_benchmark.cpp
)
add_executable(logsync_bench
    benchmarks/log_sync_benchmark.cpp    
)

target_include_directories(exchange_core PUBLIC include)
target_include_directories(network_utils PUBLIC include)

target_link_libraries(exchange_bench PRIVATE exchange_core)
target_link_libraries(exchange PRIVATE exchange_core)
target_link_libraries(exchange_tests PRIVATE 
	exchange_core
	Catch2::Catch2WithMain
)
target_link_libraries(exchange_server PRIVATE exchange_core network_utils)
target_link_libraries(exchange_client PRIVATE network_utils)
target_link_libraries(exchange_network PRIVATE exchange_core network_utils)
target_link_libraries(exchange_durability PRIVATE exchange_core network_utils)
target_link_libraries(logsync_bench PRIVATE network_utils)

catch_discover_tests(exchange_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME network_integration
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/network_integration.py
        $<TARGET_FILE:exchange_server>
)

set_tests_properties(
    network_integration
    PROPERTIES TIMEOUT 5
)

add_test(
    NAME recovery_integration
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/recovery_integration.py
        $<TARGET_FILE:exchange_server>
)

set_tests_properties(
    recovery_integration
    PROPERTIES TIMEOUT 2
)

