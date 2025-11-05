find_program(WLA_GB wla-gb REQUIRED)
find_program(WLA_LINK wlalink REQUIRED)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.s)
set(BDIR ${CMAKE_CURRENT_BINARY_DIR})

set(OBJ ${BDIR}/main.o)
set(GB ${BDIR}/main.gb)

add_custom_command(
    OUTPUT ${OBJ}
    COMMAND ${WLA_GB} -o ${OBJ} -I${CMAKE_CURRENT_SOURCE_DIR} ${SRC}
    DEPENDS ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/rom.inc
    COMMENT "Assembling ${SRC} to ${OBJ}"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GB}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/main.link ${BDIR}/main.link
    COMMAND ${WLA_LINK} -d -S main.link ${GB}
    WORKING_DIRECTORY ${BDIR}
    DEPENDS ${OBJ} ${CMAKE_CURRENT_SOURCE_DIR}/main.link
    BYPRODUCTS ${BDIR}/main.sym
    COMMENT "Linking ${OBJ} to ${GB}"
    VERBATIM
)

add_custom_target(rom ALL DEPENDS ${GB})
