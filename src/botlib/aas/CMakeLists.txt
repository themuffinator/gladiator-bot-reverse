add_library(botlib_aas STATIC
    aas_debug.c
)

target_include_directories(botlib_aas
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
)
