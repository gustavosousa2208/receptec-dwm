cmake_minimum_required(VERSION 3.20)
project(receptec_uwb_protocol_tests C)

add_executable(receptec_uwb_protocol_tests
    protocol_test.c
    ../src/uwb_protocol.c
)
target_include_directories(receptec_uwb_protocol_tests PRIVATE ../include)
target_compile_features(receptec_uwb_protocol_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_protocol_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
enable_testing()
add_test(NAME receptec_uwb_protocol_tests COMMAND receptec_uwb_protocol_tests)

add_executable(receptec_uwb_peer_registry_tests
    peer_registry_test.c
    ../src/uwb_peer_registry.c
)
target_include_directories(receptec_uwb_peer_registry_tests PRIVATE
    ../include
    ../src
)
target_compile_features(receptec_uwb_peer_registry_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_peer_registry_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_peer_registry_tests
    COMMAND receptec_uwb_peer_registry_tests)
