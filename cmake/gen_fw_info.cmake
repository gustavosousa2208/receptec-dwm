execute_process(
    COMMAND git -C "${SRC_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE FW_INFO_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FW_INFO_COMMIT)
    set(FW_INFO_COMMIT "unknown")
endif()

execute_process(
    COMMAND git -C "${SRC_DIR}/contracts" rev-parse HEAD
    OUTPUT_VARIABLE FW_INFO_CONTRACTS_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FW_INFO_CONTRACTS_COMMIT)
    set(FW_INFO_CONTRACTS_COMMIT "unknown")
endif()

execute_process(
    COMMAND git -C "${SRC_DIR}" status --porcelain --untracked-files=normal
    OUTPUT_VARIABLE FW_INFO_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(FW_INFO_STATUS)
    set(FW_INFO_DIRTY 1)
else()
    set(FW_INFO_DIRTY 0)
endif()

get_filename_component(FW_INFO_OUTPUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${FW_INFO_OUTPUT_DIR}")
set(FW_INFO_CONTENT "#ifndef FW_INFO_BUILD_H\n#define FW_INFO_BUILD_H\n\n#define FW_INFO_COMMIT \"${FW_INFO_COMMIT}\"\n#define FW_INFO_CONTRACTS_COMMIT \"${FW_INFO_CONTRACTS_COMMIT}\"\n#define FW_INFO_DIRTY ${FW_INFO_DIRTY}\n#define FW_INFO_TYPE \"${FW_TYPE}\"\n#define FW_INFO_VARIANT \"${FW_VARIANT}\"\n\n#endif\n")
if(NOT EXISTS "${OUTPUT}")
    file(WRITE "${OUTPUT}" "${FW_INFO_CONTENT}")
else()
    file(READ "${OUTPUT}" FW_INFO_CURRENT)
    if(NOT "${FW_INFO_CURRENT}" STREQUAL "${FW_INFO_CONTENT}")
        file(WRITE "${OUTPUT}" "${FW_INFO_CONTENT}")
    endif()
endif()
