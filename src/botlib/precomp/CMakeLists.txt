add_library(botlib_precomp STATIC
    l_precomp.c
    l_script.c
)

target_include_directories(botlib_precomp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
