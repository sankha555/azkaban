add_executable(test_small test_small.cpp)
add_executable(test_fairness test_fairness.cpp)
add_executable(test_small_fp test_small_fp.cpp)
add_executable(robustness robustness.cpp)
add_executable(fairness fairness.cpp)
add_executable(cleartext_accuracy cleartext_accuracy.cpp)
add_executable(inference_fp inference_fp.cpp)

set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emp-zk
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emp-zk/emp-zk
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emp-ot
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/emp-tool
    ${CMAKE_CURRENT_SOURCE_DIR}/../interval
    ${EMP-OT_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

set(COMMON_LIBS
    emp-zono
    ${EMP-OT_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    pthread
    dl
)

target_include_directories(test_small PRIVATE ${COMMON_INCLUDES})
target_include_directories(test_fairness PRIVATE ${COMMON_INCLUDES})
target_include_directories(test_small_fp PRIVATE ${COMMON_INCLUDES})
target_include_directories(robustness PRIVATE ${COMMON_INCLUDES})
target_include_directories(fairness PRIVATE ${COMMON_INCLUDES})
target_include_directories(cleartext_accuracy PRIVATE ${COMMON_INCLUDES})
target_include_directories(inference_fp PRIVATE ${COMMON_INCLUDES})

target_link_libraries(test_small PRIVATE ${COMMON_LIBS})
target_link_libraries(test_fairness PRIVATE ${COMMON_LIBS})
target_link_libraries(test_small_fp PRIVATE ${COMMON_LIBS})
target_link_libraries(robustness PRIVATE ${COMMON_LIBS})
target_link_libraries(fairness PRIVATE ${COMMON_LIBS})
target_link_libraries(cleartext_accuracy PRIVATE ${COMMON_LIBS})
target_link_libraries(inference_fp PRIVATE ${COMMON_LIBS})


set_target_properties(test_small test_fairness test_small_fp robustness fairness cleartext_accuracy inference_fp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME test_small COMMAND test_small)
add_test(NAME test_fairness COMMAND test_fairness)

# ------------------------------------------------------------------
# Run the tests under valgrind to look for memory leaks.
#
# These are two-party emp-zk programs, so each target launches BOTH parties
# (ALICE=1 and BOB=2) on 127.0.0.1 via run_valgrind.sh. Per-party log files are
# written to the build directory, e.g.:
#   valgrind_test_small_alice.log / valgrind_test_small_bob.log
#
# The working directory is the project root so the tests can find their
# data/inputs and data/params files.
#
# Usage:
#   make valgrind_test_small
#   make valgrind_test_fairness
#   make valgrind                 # runs all of the above
#
# Override options with:  VALGRIND_OPTS="--leak-check=full ..." make valgrind
# ------------------------------------------------------------------
if(VALGRIND_COMMAND)
    set(VALGRIND_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_valgrind.sh)

    add_custom_target(valgrind_test_small
        COMMAND bash ${VALGRIND_RUNNER} ${CMAKE_BINARY_DIR} $<TARGET_FILE:test_small>
        DEPENDS test_small
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running test_small under valgrind (both parties)"
        USES_TERMINAL
        VERBATIM
    )

    # test_fairness also takes example_from/example_to args (here: 0 1).
    add_custom_target(valgrind_test_fairness
        COMMAND bash ${VALGRIND_RUNNER} ${CMAKE_BINARY_DIR} $<TARGET_FILE:test_fairness> 0 1
        DEPENDS test_fairness
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running test_fairness under valgrind (both parties)"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_target(valgrind
        DEPENDS valgrind_test_small valgrind_test_fairness
        COMMENT "Running all tests under valgrind"
    )
endif()
