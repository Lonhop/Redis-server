cmake_minimum_required(VERSION 3.16)

project(RedisServerTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0135 NEW)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_library(avl_tree_lib STATIC
        ${PROJECT_ROOT}/src/data_structures/avl_tree.cpp
)

target_include_directories(avl_tree_lib PUBLIC
        ${PROJECT_ROOT}/include
)

add_executable(test_avl_tree
        ${CMAKE_CURRENT_SOURCE_DIR}/test_avl_tree.cpp
)

target_include_directories(test_avl_tree PRIVATE
        ${PROJECT_ROOT}/include
)

target_link_libraries(test_avl_tree
        avl_tree_lib
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_compile_definitions(test_avl_tree PRIVATE
        UNIT_TEST
        $<$<CONFIG:Debug>:DEBUG_MODE>
)

if(WIN32)
    target_link_libraries(test_avl_tree PRIVATE ws2_32)
endif()

gtest_discover_tests(test_avl_tree
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

message(STATUS "AVL Tree Test Configuration:")
message(STATUS "  Test source: ${CMAKE_CURRENT_SOURCE_DIR}/test_avl_tree.cpp")
message(STATUS "  AVL Tree header: ${PROJECT_ROOT}/include/data_structures/avl_tree.h")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")

add_custom_target(run_avl_tree_tests
        COMMAND test_avl_tree
        DEPENDS test_avl_tree
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running AVL Tree tests..."
)