add_library(botlib_ea STATIC
    ea_main.c
)

register_botlib_sources(
    TARGET botlib_ea
    SOURCES
        ea_main.c
)

target_include_directories(botlib_ea
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(botlib_ea
    PUBLIC
        botlib_common
        q2bridge
)
