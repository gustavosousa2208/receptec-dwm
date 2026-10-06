cmake_minimum_required(VERSION 3.20)
project(receptec_uwb_protocol_tests C)

add_executable(receptec_uwb_protocol_tests
    protocol_test.c
    ../../src/receptec/uwb_protocol.c
)
target_include_directories(receptec_uwb_protocol_tests PRIVATE ../../include)
target_compile_features(receptec_uwb_protocol_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_protocol_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
enable_testing()
add_executable(receptec_uwb_round_guard_tests round_guard_test.c)
target_include_directories(receptec_uwb_round_guard_tests PRIVATE ../../src/receptec)
target_compile_features(receptec_uwb_round_guard_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_round_guard_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_round_guard_tests COMMAND receptec_uwb_round_guard_tests)
add_test(NAME receptec_uwb_protocol_tests COMMAND receptec_uwb_protocol_tests)

add_executable(receptec_uwb_peer_registry_tests
    peer_registry_test.c
    ../../src/receptec/uwb_peer_registry.c
)
target_include_directories(receptec_uwb_peer_registry_tests PRIVATE
    ../../include
    ../../src/receptec
)
target_compile_features(receptec_uwb_peer_registry_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_peer_registry_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_peer_registry_tests
    COMMAND receptec_uwb_peer_registry_tests)

add_executable(receptec_uwb_peer_storage_tests
    peer_storage_test.c
    ../../src/receptec/uwb_peer_storage.c
)
target_include_directories(receptec_uwb_peer_storage_tests PRIVATE
    mocks
    ../../include
    ../../src/receptec
)
target_compile_features(receptec_uwb_peer_storage_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_peer_storage_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_peer_storage_tests
    COMMAND receptec_uwb_peer_storage_tests)

add_executable(receptec_uwb_test_helpers_tests
    uwb_test_helpers_test.c
    ../../src/receptec/uwb_protocol.c
    ../../src/receptec/uwb_test_result.c
    ../../src/receptec/uwb_test_frames.c
    ../../src/receptec/uwb_test_payload.c
)
target_include_directories(receptec_uwb_test_helpers_tests PRIVATE ../../include)
target_compile_features(receptec_uwb_test_helpers_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_test_helpers_tests PRIVATE
    -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_test_helpers_tests
    COMMAND receptec_uwb_test_helpers_tests)

add_executable(receptec_uwb_cycle_policy_tests cycle_policy_test.c)
target_include_directories(receptec_uwb_cycle_policy_tests PRIVATE ../../src/receptec)
target_compile_features(receptec_uwb_cycle_policy_tests PRIVATE c_std_11)
target_compile_options(receptec_uwb_cycle_policy_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_uwb_cycle_policy_tests COMMAND receptec_uwb_cycle_policy_tests)

add_executable(receptec_ble_range_tests ble_range_test.c)
target_include_directories(receptec_ble_range_tests PRIVATE ble_mocks ../../include ../../src/receptec)
target_compile_features(receptec_ble_range_tests PRIVATE c_std_11)
target_compile_options(receptec_ble_range_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME receptec_ble_range_tests COMMAND receptec_ble_range_tests)
