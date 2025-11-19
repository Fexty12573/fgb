find_program(WLA_GB wla-gb REQUIRED)
find_program(WLA_LINK wlalink REQUIRED)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.s)
set(BDIR ${CMAKE_CURRENT_BINARY_DIR})

set(INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/rom.inc ${CMAKE_CURRENT_SOURCE_DIR}/util.inc)

function(fgb_add_rom NAME)
    add_custom_command(
        OUTPUT ${BDIR}/${NAME}.o
        COMMAND ${WLA_GB} -o ${BDIR}/${NAME}.o -I${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.s
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.s ${INCLUDES}
        COMMENT "Assembling ${NAME}.s to ${NAME}.o"
        VERBATIM
    )

    add_custom_target(${NAME}_rom
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.link ${BDIR}/${NAME}.link
        COMMAND ${WLA_LINK} -d -S ${NAME}.link ${BDIR}/${NAME}.gb
        WORKING_DIRECTORY ${BDIR}
        DEPENDS ${BDIR}/${NAME}.o ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.link
        BYPRODUCTS ${BDIR}/${NAME}.sym
        COMMENT "Linking ${NAME}.o to ${NAME}.gb"
        VERBATIM
    )
endfunction()

fgb_add_rom(main)
fgb_add_rom(obj)

add_custom_target(rom ALL DEPENDS main_rom obj_rom)
