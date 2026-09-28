# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/receptec.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(receptec)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
    src/main.c
    src/uwb_protocol.c
    src/uwb_peer_registry.c
    src/uwb_peer_storage.c
    src/dw3720_spi.c
    src/uwb_radio.c
    src/uwb_shell.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_interface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_compat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_rsl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3000/dw3000_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3720/dw3720_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/lib/qmath/src/qmath.c
)

target_compile_definitions(app PRIVATE USE_DRV_DW3000 USE_DRV_DW3720)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3000
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3720
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/lib/qmath/include
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_interface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_compat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/deca_rsl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3000/dw3000_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/dw3720/dw3720_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/qorvo/lib/qmath/src/qmath.c
    PROPERTIES COMPILE_FLAGS
    "-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
)
