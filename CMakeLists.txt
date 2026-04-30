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

enable_testing()
add_subdirectory(tests)