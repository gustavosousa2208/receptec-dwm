# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(coletec_dwm)

set(QORVO_DRIVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/qorvo)

target_compile_definitions(app PRIVATE USE_DRV_DW3000)

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
    src/coletec_radio.c
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
