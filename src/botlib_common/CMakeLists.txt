add_library(botlib_common STATIC
    l_assets.c
    l_crc.c
    l_libvar.c
    l_log.c
    l_memory.c
    l_struct.c
    l_utils.c
)

register_botlib_sources(
    TARGET botlib_common
    SOURCES
        l_assets.c
        l_crc.c
        l_libvar.c
        l_log.c
        l_memory.c
        l_struct.c
        l_utils.c
)

target_include_directories(botlib_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/botlib_precomp
        ${PROJECT_SOURCE_DIR}/src
)
