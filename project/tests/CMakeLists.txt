cmake_minimum_required(VERSION 3.16)

project(RedisServerTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# avl libr
add_library(avl_tree_lib STATIC
        ${PROJECT_ROOT}/src/data_structures/avl_tree.cpp
)
target_include_directories(avl_tree_lib PUBLIC
        ${PROJECT_ROOT}/include
)
# heap libr
add_library(heap_lib STATIC
        ${PROJECT_ROOT}/src/data_structures/heap.cpp
)

target_include_directories(heap_lib PUBLIC
        ${PROJECT_ROOT}/include
)
# thread libr
add_library(thread_pool_lib STATIC
        ${PROJECT_ROOT}/src/core/thread_pool.cpp
)

target_include_directories(thread_pool_lib PUBLIC
        ${PROJECT_ROOT}/include
)
#hash_map libr
add_library(hash_map_lib STATIC
        ${PROJECT_ROOT}/src/data_structures/hash_map.cpp
)

target_include_directories(hash_map_lib PUBLIC
        ${PROJECT_ROOT}/include
)

# avl tests
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

gtest_discover_tests(test_avl_tree
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# heap tests
add_executable(test_heap
        ${CMAKE_CURRENT_SOURCE_DIR}/test_heap.cpp
)

target_include_directories(test_heap PRIVATE
        ${PROJECT_ROOT}/include
)

target_link_libraries(test_heap
        heap_lib
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_compile_definitions(test_heap PRIVATE
        UNIT_TEST
        $<$<CONFIG:Debug>:DEBUG_MODE>
)

gtest_discover_tests(test_heap
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# thread tests
add_executable(test_thread_pool
        ${CMAKE_CURRENT_SOURCE_DIR}/test_thread_pool.cpp
)

target_include_directories(test_thread_pool PRIVATE
        ${PROJECT_ROOT}/include
)

target_link_libraries(test_thread_pool
        thread_pool_lib
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_compile_definitions(test_thread_pool PRIVATE
        UNIT_TEST
        $<$<CONFIG:Debug>:DEBUG_MODE>
)

gtest_discover_tests(test_thread_pool
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# hash test
add_executable(test_hash_map
        ${CMAKE_CURRENT_SOURCE_DIR}/test_hash_map.cpp
)

target_include_directories(test_hash_map PRIVATE
        ${PROJECT_ROOT}/include
)

target_link_libraries(test_hash_map
        hash_map_lib
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_compile_definitions(test_hash_map PRIVATE
        UNIT_TEST
        $<$<CONFIG:Debug>:DEBUG_MODE>
)

gtest_discover_tests(test_hash_map
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# win libr
if(WIN32)
    target_link_libraries(test_avl_tree PRIVATE ws2_32)
    target_link_libraries(test_heap PRIVATE ws2_32)
    target_link_libraries(test_thread_pool PRIVATE ws2_32)
    target_link_libraries(test_hash_map PRIVATE ws2_32)
endif()

# Ручной запуск каждой функции
add_custom_target(run_avl_tree_tests
        COMMAND test_avl_tree
        DEPENDS test_avl_tree
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(run_heap_tests
        COMMAND test_heap
        DEPENDS test_heap
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(run_thread_pool_tests
        COMMAND test_thread_pool
        DEPENDS test_thread_pool
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_custom_target(run_hash_map_tests
        COMMAND test_hash_map
        DEPENDS test_hash_map
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(run_all_tests
        COMMAND test_avl_tree
        COMMAND test_heap
        COMMAND test_thread_pool
        COMMAND test_hash_map
        DEPENDS test_avl_tree test_heap test_thread_pool test_hash_map
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)