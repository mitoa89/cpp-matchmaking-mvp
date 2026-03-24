cmake_minimum_required(VERSION 3.20)

project(mm_submit_pkg LANGUAGES CXX)

include(CTest)
option(MM_FETCH_GTEST "Allow CMake to download GoogleTest when it is not installed locally." OFF)
if(BUILD_TESTING)
    find_package(GTest QUIET CONFIG)
    set(MM_GTEST_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/googletest-src")
    set(MM_GTEST_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/googletest-build")

    # Reuse a previously fetched googletest checkout so moving/reconfiguring the
    # project does not silently drop all tests when no system package is present.
    if(NOT GTest_FOUND AND EXISTS "${MM_GTEST_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${MM_GTEST_SOURCE_DIR}" "${MM_GTEST_BINARY_DIR}" EXCLUDE_FROM_ALL)
    elseif(NOT GTest_FOUND AND MM_FETCH_GTEST)
        include(FetchContent)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            DOWNLOAD_EXTRACT_TIMESTAMP ON
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    if(NOT GTest_FOUND AND NOT TARGET GTest::gtest_main)
        message(WARNING "GoogleTest was not found. Tests will be skipped. Install GTest locally or configure with -DMM_FETCH_GTEST=ON to download it.")
        set(BUILD_TESTING OFF)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

if(APPLE)
    find_library(ICONV_LIBRARY iconv REQUIRED)
endif()

add_executable(mm_demo
    MM/Main.cpp
    MM/DemoRunner.cpp
    MM/stdafx.cpp
        MM/tests/MatchTestHelper.cpp
        MM/matching/core/MatchmakingRule.cpp
        MM/matching/core/MatchSession.cpp
        MM/matching/core/MatchmakingTicket.cpp
    MM/integration/IntegrationZoneInstance.cpp
        MM/integration/IntegrationZoneInstanceManager.cpp
        MM/matching/SoloMatchmakingManager.cpp
        MM/matching/MatchPenaltyManager.cpp
        MM/matching/MatchSessionManager.cpp
        MM/matching/PartyMatchmakingManager.cpp
    MM/workers/AsyncJobWorker.cpp
        MM/matching/workers/PartyMatchmakingWorker.cpp
        MM/matching/workers/SoloMatchmakingWorker.cpp
    MM/workers/WorkerJobRequest.cpp
        MM/common/TSafeMap.h
        MM/common/TSingleton.h
)

target_include_directories(mm_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MM
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/common
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/matching/core
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/integration
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/matching
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/workers
    ${CMAKE_CURRENT_SOURCE_DIR}/MM/sqlite
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/MM PREFIX "MM" FILES
    MM/Main.cpp
    MM/DemoRunner.h
    MM/DemoRunner.cpp
    MM/stdafx.cpp
    MM/stdafx.h
    MM/common/SStringFormat.h
    MM/common/SStringUtil.h
        MM/matching/core/MatchmakingRule.cpp
        MM/matching/core/MatchmakingRule.h
        MM/matching/core/MatchSession.cpp
        MM/matching/core/MatchSession.h
        MM/matching/core/MatchmakingTicket.cpp
        MM/matching/core/MatchmakingTicket.h
    MM/integration/IntegrationZoneInstance.cpp
    MM/integration/IntegrationZoneInstance.h
        MM/integration/IntegrationZoneInstanceManager.cpp
        MM/integration/IntegrationZoneInstanceManager.h
        MM/matching/SoloMatchmakingManager.h
        MM/matching/SoloMatchmakingManager.cpp
        MM/matching/MatchPenaltyManager.h
        MM/matching/MatchPenaltyManager.cpp
        MM/matching/MatchSessionManager.cpp
        MM/matching/MatchSessionManager.h
        MM/matching/PartyMatchmakingManager.cpp
        MM/matching/PartyMatchmakingManager.h
    MM/workers/AsyncJobWorker.cpp
    MM/workers/AsyncJobWorker.h
        MM/matching/workers/PartyMatchmakingWorker.cpp
        MM/matching/workers/PartyMatchmakingWorker.h
        MM/matching/workers/SoloMatchmakingWorker.cpp
        MM/matching/workers/SoloMatchmakingWorker.h
    MM/workers/WorkerGroup.h
    MM/workers/WorkerJobRequest.cpp
    MM/workers/WorkerJobRequest.h
    MM/tests/MMUnitTest.cpp
    MM/tests/MatchTestHelper.h
    MM/tests/MatchTestHelper.cpp
)

target_link_libraries(mm_demo PRIVATE SQLite::SQLite3)

if(APPLE)
    target_link_libraries(mm_demo PRIVATE ${ICONV_LIBRARY})
    target_compile_options(mm_demo PRIVATE -pthread)
    target_link_options(mm_demo PRIVATE -pthread)
endif()

if(BUILD_TESTING)
    add_executable(mm_tests
        MM/stdafx.cpp
        MM/tests/MMUnitTest.cpp
        MM/tests/MatchTestHelper.cpp
            MM/matching/core/MatchmakingRule.cpp
            MM/matching/core/MatchSession.cpp
            MM/matching/core/MatchmakingTicket.cpp
        MM/integration/IntegrationZoneInstance.cpp
            MM/integration/IntegrationZoneInstanceManager.cpp
            MM/matching/SoloMatchmakingManager.cpp
            MM/matching/MatchPenaltyManager.cpp
            MM/matching/MatchSessionManager.cpp
            MM/matching/PartyMatchmakingManager.cpp
        MM/workers/AsyncJobWorker.cpp
            MM/matching/workers/PartyMatchmakingWorker.cpp
            MM/matching/workers/SoloMatchmakingWorker.cpp
        MM/workers/WorkerJobRequest.cpp
            MM/common/TSafeMap.h
            MM/common/TSingleton.h
    )

    target_include_directories(mm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MM
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/common
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/matching/core
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/integration
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/matching
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/workers
        ${CMAKE_CURRENT_SOURCE_DIR}/MM/sqlite
    )

    target_link_libraries(mm_tests PRIVATE SQLite::SQLite3 GTest::gtest_main)

    if(APPLE)
        target_link_libraries(mm_tests PRIVATE ${ICONV_LIBRARY})
        target_compile_options(mm_tests PRIVATE -pthread)
        target_link_options(mm_tests PRIVATE -pthread)
    endif()

    include(GoogleTest)
    gtest_discover_tests(mm_tests)
endif()
