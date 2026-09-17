cmake_minimum_required(VERSION 3.20)

project(exchange_simulator LANGUAGES CXX)

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

add_executable(exchange
    	src/main.cpp
)

add_executable(exchange_tests
	tests/order_book_tests.cpp
)

add_executable(exchange_bench
	benchmarks/benchmark.cpp
)



target_include_directories(exchange_core PUBLIC include)

target_link_libraries(exchange_bench PRIVATE exchange_core)
target_link_libraries(exchange PRIVATE exchange_core)
target_link_libraries(exchange_tests PRIVATE 
	exchange_core
	Catch2::Catch2WithMain
)

catch_discover_tests(exchange_tests)
