cmake_minimum_required(VERSION 3.20)
project(dwm_uwb_protocol_tests C)

add_executable(dwm_uwb_protocol_tests
    protocol_test.c
    ../src/protocol.c
    ../src/coletec_sync.c
)

target_include_directories(dwm_uwb_protocol_tests PRIVATE ../include)
target_compile_features(dwm_uwb_protocol_tests PRIVATE c_std_11)
target_compile_options(dwm_uwb_protocol_tests PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wpedantic
)

enable_testing()
add_test(NAME dwm_uwb_protocol_tests COMMAND dwm_uwb_protocol_tests)

add_subdirectory(receptec)
