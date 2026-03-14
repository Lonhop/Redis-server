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

function(add_redis_test test_name)
    add_executable(${test_name}
        ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.cpp
    )

    target_include_directories(${test_name} PRIVATE
        ${PROJECT_ROOT}/include
    )

    target_link_libraries(${test_name} PRIVATE
        redis_components
        GTest::gtest_main
    )

    if(WIN32)
        target_link_libraries(${test_name} PRIVATE ws2_32)
    endif()

    target_compile_definitions(${test_name} PRIVATE
        UNIT_TEST
        $<$<CONFIG:Debug>:DEBUG_MODE>
    )

    gtest_discover_tests(${test_name}
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

add_redis_test(test_avl_tree)
add_redis_test(test_heap)
add_redis_test(test_thread_pool)
add_redis_test(test_hash_map)
add_redis_test(test_commands)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_avl_tree test_heap test_thread_pool test_hash_map test_commands
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
