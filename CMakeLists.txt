# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(RECEPTEC_APP FALSE)
set(ATLETEC_FW_TYPE "ATLETEC UWB firmware")
set(ATLETEC_FW_VARIANT "${BOARD}")
if(BOARD STREQUAL "fake_coletec_0/nrf52833" AND ATLETEC_FIXED_ANCHOR)
    set(RECEPTEC_APP TRUE)
    set(RECEPTEC_QORVO TRUE)
    set(RECEPTEC_FIXED_ANCHOR TRUE)
    set(ATLETEC_FW_TYPE "RECEPTEC fixed UWB anchor")
    set(ATLETEC_FW_VARIANT "Anchor 1 on fake-COLETEC-0 / DWM3001C / nRF52833")
    set(CONF_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/receptec_qorvo/receptec_qorvo.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/coletec/fake_coletec_0_anchor.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/coletec/fake_coletec_uart.conf)
elseif(BOARD STREQUAL "receptec/nrf52840")
    set(RECEPTEC_APP TRUE)
    set(RECEPTEC_QORVO FALSE)
    set(ATLETEC_FW_TYPE "RECEPTEC receiver")
    set(ATLETEC_FW_VARIANT "RECEPTEC Type2AB / nRF52840 / DW3720")
    set(DTC_OVERLAY_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/receptec/receptec.overlay)
    set(CONF_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/receptec/receptec.conf)
elseif(BOARD STREQUAL "receptec_qorvo/nrf52833")
    set(RECEPTEC_APP TRUE)
    set(RECEPTEC_QORVO TRUE)
    set(ATLETEC_FW_TYPE "RECEPTEC receiver")
    set(ATLETEC_FW_VARIANT "RECEPTEC-Qorvo / DWM3001C / nRF52833 / DW3110")
    set(DTC_OVERLAY_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/receptec_qorvo/receptec_qorvo.overlay)
    set(CONF_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/receptec_qorvo/receptec_qorvo.conf)
elseif(BOARD STREQUAL "fake_coletec_0/nrf52833")
    set(ATLETEC_FW_TYPE "fake-COLETEC responder")
    set(ATLETEC_FW_VARIANT "fake-COLETEC-0 / DWM3001C / nRF52833 / DW3110")
    set(CONF_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/coletec/fake_coletec_uart.conf)
elseif(BOARD STREQUAL "fake_coletec_1/nrf52833")
    set(ATLETEC_FW_TYPE "fake-COLETEC responder")
    set(ATLETEC_FW_VARIANT "fake-COLETEC-1 / DWM3001C / nRF52833 / DW3110")
    set(CONF_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/boards/atletec/coletec/fake_coletec_uart.conf)
elseif(BOARD STREQUAL "coletec/nrf52833")
    set(ATLETEC_FW_TYPE "COLETEC-Qorvo UWB responder")
    set(ATLETEC_FW_VARIANT "COLETEC-Qorvo / DWM3001C / nRF52833 / DW3110")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atletec_firmware)

set(ATLETEC_FW_INFO_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/fw_info_build.h)
add_custom_target(atletec_fw_info_metadata
    COMMAND ${CMAKE_COMMAND}
        "-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT=${ATLETEC_FW_INFO_HEADER}"
        "-DFW_TYPE=${ATLETEC_FW_TYPE}"
        "-DFW_VARIANT=${ATLETEC_FW_VARIANT}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_fw_info.cmake
    BYPRODUCTS ${ATLETEC_FW_INFO_HEADER}
    COMMENT "Generating firmware identity"
    VERBATIM
)
add_dependencies(app atletec_fw_info_metadata)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(QORVO_DRIVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/qorvo)
set(QORVO_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/qorvo)

target_include_directories(app PRIVATE
    include
    src
    src/receptec
    ${QORVO_DRIVER_DIR}
    ${QORVO_DRIVER_DIR}/dw3000
    ${QORVO_DRIVER_DIR}/dw3720
    ${QORVO_DRIVER_DIR}/lib/qmath/include
)

if(RECEPTEC_APP)
    target_compile_definitions(app PRIVATE USE_DRV_DW3000)
    if(RECEPTEC_QORVO)
        target_compile_definitions(app PRIVATE RECEPTEC_DW3110_RADIO)
    endif()
    if(RECEPTEC_FIXED_ANCHOR)
        target_compile_definitions(app PRIVATE RECEPTEC_FIXED_ANCHOR)
    endif()
    target_sources(app PRIVATE
        src/receptec/main.c
        src/fw_info_shell.c
        src/led_shell.c
        src/receptec/uwb_protocol.c
        src/receptec/uwb_peer_registry.c
        src/receptec/uwb_peer_storage.c
        src/receptec/uwb_test_result.c
        src/receptec/uwb_test_frames.c
        src/receptec/uwb_test_payload.c
        src/receptec/uwb_radio.c
        src/receptec/uwb_shell.c
        ${QORVO_SOURCE_DIR}/deca_interface.c
        ${QORVO_SOURCE_DIR}/deca_compat.c
        ${QORVO_SOURCE_DIR}/deca_rsl.c
        ${QORVO_SOURCE_DIR}/dw3000/dw3000_device.c
        ${QORVO_SOURCE_DIR}/lib/qmath/src/qmath.c
    )

    if(RECEPTEC_QORVO)
        target_sources(app PRIVATE src/dw3110_port.c)
    else()
        target_compile_definitions(app PRIVATE USE_DRV_DW3720)
        target_sources(app PRIVATE
            src/receptec/dw3720_spi.c
            ${QORVO_SOURCE_DIR}/dw3720/dw3720_device.c
        )
    endif()
else()
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

    target_compile_definitions(app PRIVATE
        USE_DRV_DW3000
        DWM_BENCH_GIT_REV="${DWM_BENCH_GIT_REV}"
        DWM_BENCH_CONTRACTS_REV="${DWM_BENCH_CONTRACTS_REV}"
    )
    target_sources(app PRIVATE
        src/main.c
        src/fw_info_shell.c
        src/protocol.c
        src/coletec_sync.c
        src/dw3110_port.c
        src/dw3110_radio.c
        src/led_shell.c
        src/uwb_shell.c
        ${QORVO_SOURCE_DIR}/deca_compat.c
        ${QORVO_SOURCE_DIR}/deca_interface.c
        ${QORVO_SOURCE_DIR}/deca_rsl.c
        ${QORVO_SOURCE_DIR}/dw3000/dw3000_device.c
        ${QORVO_SOURCE_DIR}/lib/qmath/src/qmath.c
    )
endif()

set_source_files_properties(
    ${QORVO_SOURCE_DIR}/deca_compat.c
    ${QORVO_SOURCE_DIR}/deca_interface.c
    ${QORVO_SOURCE_DIR}/deca_rsl.c
    ${QORVO_SOURCE_DIR}/dw3000/dw3000_device.c
    ${QORVO_SOURCE_DIR}/dw3720/dw3720_device.c
    ${QORVO_SOURCE_DIR}/lib/qmath/src/qmath.c
    PROPERTIES COMPILE_FLAGS
    "-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
)
