if(NOT DEFINED INPUT_FILE OR NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Generated binary does not exist: ${INPUT_FILE}")
endif()

if(NOT DEFINED MAX_SIZE)
    message(FATAL_ERROR "MAX_SIZE is required")
endif()

file(SIZE "${INPUT_FILE}" ACTUAL_SIZE)
if(ACTUAL_SIZE EQUAL 0)
    message(FATAL_ERROR "Generated binary is empty: ${INPUT_FILE}")
endif()

if(ACTUAL_SIZE GREATER MAX_SIZE)
    message(FATAL_ERROR
        "Generated binary is ${ACTUAL_SIZE} bytes; maximum is ${MAX_SIZE}: ${INPUT_FILE}"
    )
endif()

message(STATUS "Validated ${INPUT_FILE}: ${ACTUAL_SIZE}/${MAX_SIZE} bytes")
