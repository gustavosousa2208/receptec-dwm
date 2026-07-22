cmake_minimum_required(VERSION 3.20)
project(receptec_protocol_tests C)

add_executable(receptec_protocol_tests
    protocol_test.c
    ../src/protocol.c
)

target_include_directories(receptec_protocol_tests PRIVATE ../include)
target_compile_features(receptec_protocol_tests PRIVATE c_std_11)
target_compile_options(receptec_protocol_tests PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wpedantic
)

enable_testing()
add_test(NAME receptec_protocol_tests COMMAND receptec_protocol_tests)
