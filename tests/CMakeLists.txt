add_executable(robustness robustness.cpp)
add_executable(fairness fairness.cpp)
add_executable(cleartext_accuracy cleartext_accuracy.cpp)

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

target_include_directories(robustness PRIVATE ${COMMON_INCLUDES})
target_include_directories(fairness PRIVATE ${COMMON_INCLUDES})
target_include_directories(cleartext_accuracy PRIVATE ${COMMON_INCLUDES})


target_link_libraries(robustness PRIVATE ${COMMON_LIBS})
target_link_libraries(fairness PRIVATE ${COMMON_LIBS})
target_link_libraries(cleartext_accuracy PRIVATE ${COMMON_LIBS})


set_target_properties(robustness fairness cleartext_accuracy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

