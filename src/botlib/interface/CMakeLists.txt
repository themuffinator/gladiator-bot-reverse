add_library(botlib_interface STATIC
    bot_interface.c
    bot_state.c
    botlib_interface.c
)

target_include_directories(botlib_interface
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(botlib_interface
    PUBLIC
        botlib_common
        botlib_precomp
        botlib_aas
        botlib_ai
        botlib_ea
        q2bridge
)
