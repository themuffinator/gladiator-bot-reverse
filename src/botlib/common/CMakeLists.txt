add_library(botlib_common STATIC
    l_crc.c
    l_libvar.c
    l_log.c
    l_memory.c
    l_precomp.c
    l_script.c
    l_struct.c
    l_utils.c
)

target_include_directories(botlib_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
