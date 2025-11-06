add_library(botlib_aas STATIC
    aas_debug.c
    aas_debug_commands.c
    aas_main.c
    aas_map.c
    aas_sound.c
    aas_reach.c
    aas_route.c
)

register_botlib_sources(
    TARGET botlib_aas
    SOURCES
        aas_debug.c
        aas_debug_commands.c
        aas_main.c
        aas_map.c
        aas_sound.c
        aas_reach.c
        aas_route.c
)

target_include_directories(botlib_aas
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../ai
        ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
)
