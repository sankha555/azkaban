cmake_minimum_required(VERSION 3.10)
project(ZonotopeProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb -fopenmp -O3 -march=native -ffast-math -w -Wfatal-errors")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Wall -g2 -ggdb -fopenmp -O3 -march=native -ffast-math -w -Wfatal-errors")

# System emp config
find_path(CMAKE_FOLDER NAMES cmake/emp-tool-config.cmake)
include(${CMAKE_FOLDER}/cmake/emp-base.cmake)

find_package(emp-ot REQUIRED)
find_package(OpenSSL REQUIRED)

# Include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emp-zk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emp-zk/emp-zk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emp-ot
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emp-tool
    ${CMAKE_CURRENT_SOURCE_DIR}/interval
    ${EMP-OT_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Build the main shared library from local emp-zk + emp-zk-math sources
set(SOURCES
    src/emp-zk/emp-zk/emp-zk-bool/emp-zk-bool.cpp
    src/emp-zk/emp-zk/emp-zk-arith/emp-zk-arith.cpp
    src/emp-zk-math/ZKmath-global.cpp
    src/emp-zk-math/ZKmath-functions.cpp
)

add_library(emp-zono SHARED ${SOURCES})

target_link_libraries(emp-zono
    ${EMP-OT_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    pthread
    dl
)

install(DIRECTORY src/emp-zk DESTINATION include/)
install(FILES cmake/emp-zk-config.cmake DESTINATION cmake/)
install(TARGETS emp-zono DESTINATION lib)

# ------------------------------------------------------------------
# Valgrind / memory-leak checking
# ------------------------------------------------------------------
# Detect valgrind and expose it to the test targets (see tests/CMakeLists.txt).
find_program(VALGRIND_COMMAND valgrind)

if(VALGRIND_COMMAND)
    message(STATUS "Valgrind found: ${VALGRIND_COMMAND}")

    # Default options reused by the custom 'valgrind' targets and by CTest's
    # built-in memcheck (ctest -T memcheck).
    set(VALGRIND_OPTIONS
        "--leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1")

    # Wire up 'ctest -T memcheck'. These variables must be set before
    # include(CTest) so they land in DartConfiguration.tcl.
    set(MEMORYCHECK_COMMAND "${VALGRIND_COMMAND}"
        CACHE FILEPATH "Path to valgrind" FORCE)
    set(MEMORYCHECK_COMMAND_OPTIONS "${VALGRIND_OPTIONS}"
        CACHE STRING "Options passed to valgrind by 'ctest -T memcheck'" FORCE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
        set(MEMORYCHECK_SUPPRESSIONS_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp"
            CACHE FILEPATH "Valgrind suppressions file" FORCE)
    endif()

    # NOTE: the default build enables -march=native and -ffast-math. If valgrind
    # aborts with "unrecognised instruction", rebuild without those flags
    # (or with a lower -march) so valgrind can interpret the binary.
else()
    message(WARNING
        "Valgrind not found - 'make valgrind' targets and 'ctest -T memcheck' "
        "will be unavailable. Install it with e.g. 'apt-get install valgrind'.")
endif()

# include(CTest) enables testing (like enable_testing()) and turns on the
# memcheck dashboard target configured above.
include(CTest)
add_subdirectory(tests)