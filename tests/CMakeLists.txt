cmake_minimum_required(VERSION 3.20)
project(coletec_protocol_tests C)

add_executable(coletec_protocol_tests
    protocol_test.c
    ../src/protocol.c
    ../src/coletec_sync.c
)

target_include_directories(coletec_protocol_tests PRIVATE ../include)
target_compile_features(coletec_protocol_tests PRIVATE c_std_11)
target_compile_options(coletec_protocol_tests PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wpedantic
)

enable_testing()
add_test(NAME coletec_protocol_tests COMMAND coletec_protocol_tests)
