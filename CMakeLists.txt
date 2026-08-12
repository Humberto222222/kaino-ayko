cmake_minimum_required(VERSION 3.16)
project(ayko_backend VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DASIO_STANDALONE -DCROW_STATIC)

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.cpp
)

add_executable(ayko_server ${SOURCES})

target_link_libraries(ayko_server
    Threads::Threads
    sqlite3
)

if(WIN32)
    target_link_libraries(ayko_server ws2_32 mswsock)
endif()

file(COPY ${CMAKE_SOURCE_DIR}/public DESTINATION ${CMAKE_BINARY_DIR})
