set(_genx_intrinsic_description "GenXIntrinsicDesctiption.gen")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_genx_intrinsic_description}
    COMMAND ${PYTHON_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/Intrinsics.py
            ${CMAKE_CURRENT_SOURCE_DIR}/Intrinsic_definitions.py
            ${CMAKE_CURRENT_BINARY_DIR}/${_genx_intrinsic_description}
    DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/Intrinsics.py
         ${CMAKE_CURRENT_SOURCE_DIR}/Intrinsic_definitions.py
    COMMENT "Building ${_genx_intrinsic_description}..."
)

add_custom_target(GenXIntrinsicDescriptionGen
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_genx_intrinsic_description}
)
add_custom_target(GenXIntrinsicsGen)
add_dependencies(GenXIntrinsicsGen GenXIntrinsicDescriptionGen)
