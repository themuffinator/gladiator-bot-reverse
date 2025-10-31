add_library(botlib_interface STATIC
    bot_interface.c
)

target_sources(botlib_interface
    PRIVATE
        $<TARGET_OBJECTS:botlib_ai>
)

target_include_directories(botlib_interface
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(botlib_interface
    PUBLIC
        botlib_common
        botlib_precomp
        q2bridge
)
