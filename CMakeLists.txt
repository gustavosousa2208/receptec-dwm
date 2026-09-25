# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atletec_uwb_dwm_bench)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE DWM_BENCH_GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/contracts
    OUTPUT_VARIABLE DWM_BENCH_CONTRACTS_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT DWM_BENCH_GIT_REV)
    set(DWM_BENCH_GIT_REV "unknown")
endif()
if(NOT DWM_BENCH_CONTRACTS_REV)
    set(DWM_BENCH_CONTRACTS_REV "unknown")
endif()

set(QORVO_DRIVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/qorvo)

target_compile_definitions(app PRIVATE
    USE_DRV_DW3000
    DWM_BENCH_GIT_REV="${DWM_BENCH_GIT_REV}"
    DWM_BENCH_CONTRACTS_REV="${DWM_BENCH_CONTRACTS_REV}"
)

target_include_directories(app PRIVATE
    include
    ${QORVO_DRIVER_DIR}
    ${QORVO_DRIVER_DIR}/dw3000
    ${QORVO_DRIVER_DIR}/lib/qmath/include
)

target_sources(app PRIVATE
    src/main.c
    src/protocol.c
    src/coletec_sync.c
    src/dw3110_port.c
    src/dw3110_radio.c
    src/led_shell.c
    src/uwb_shell.c
    ${QORVO_DRIVER_DIR}/deca_compat.c
    ${QORVO_DRIVER_DIR}/deca_interface.c
    ${QORVO_DRIVER_DIR}/deca_rsl.c
    ${QORVO_DRIVER_DIR}/dw3000/dw3000_device.c
    ${QORVO_DRIVER_DIR}/lib/qmath/src/qmath.c
)

set_source_files_properties(
    ${QORVO_DRIVER_DIR}/deca_compat.c
    ${QORVO_DRIVER_DIR}/deca_interface.c
    ${QORVO_DRIVER_DIR}/deca_rsl.c
    ${QORVO_DRIVER_DIR}/dw3000/dw3000_device.c
    ${QORVO_DRIVER_DIR}/lib/qmath/src/qmath.c
    PROPERTIES COMPILE_FLAGS
    "-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
)
