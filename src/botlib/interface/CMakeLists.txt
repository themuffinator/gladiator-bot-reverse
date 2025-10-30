add_library(botlib_interface STATIC
    bot_interface.c
)

target_include_directories(botlib_interface
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(botlib_interface
    PUBLIC
        q2bridge
)
