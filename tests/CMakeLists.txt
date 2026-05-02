add_executable(test_small test_small.cpp)
add_executable(test_fairness test_fairness.cpp)

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

target_link_libraries(test_small PRIVATE ${COMMON_LIBS})
target_link_libraries(test_fairness PRIVATE ${COMMON_LIBS})


set_target_properties(test_small test_fairness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME test_small COMMAND test_small)
add_test(NAME test_fairness COMMAND test_fairness)