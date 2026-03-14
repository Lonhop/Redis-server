cmake_minimum_required(VERSION 3.16)

project(RedisServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REDIS_BUILD_TESTS "Build tests" ON)
option(REDIS_ENABLE_EXPERIMENTAL_NETWORKING "Build incomplete networking layer" OFF)
option(REDIS_ENABLE_EXPERIMENTAL_ZSET "Build incomplete zset layer" OFF)

add_library(redis_components STATIC
    src/utils/logger.cpp
    src/utils/timer.cpp
    src/utils/raii_helpers.cpp
    src/core/serialization.cpp
    src/core/commands.cpp
    src/data_structures/hash_map.cpp
    src/data_structures/avl_tree.cpp
    src/data_structures/heap.cpp
    src/core/thread_pool.cpp
)

target_include_directories(redis_components PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(REDIS_ENABLE_EXPERIMENTAL_ZSET)
    target_sources(redis_components PRIVATE
        src/data_structures/zset.cpp
    )
    target_compile_definitions(redis_components PUBLIC REDIS_ENABLE_EXPERIMENTAL_ZSET=1)
endif()

if(REDIS_ENABLE_EXPERIMENTAL_NETWORKING)
    target_sources(redis_components PRIVATE
        src/core/event_loop.cpp
        src/core/iocp_poller.cpp
        src/core/connection.cpp
        src/core/tcp_server.cpp
    )
    target_compile_definitions(redis_components PUBLIC REDIS_ENABLE_EXPERIMENTAL_NETWORKING=1)
endif()

if(WIN32)
    target_link_libraries(redis_components PUBLIC ws2_32)
endif()

add_executable(redis_server
    src/main.cpp
)

target_link_libraries(redis_server PRIVATE redis_components)

if(REDIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
