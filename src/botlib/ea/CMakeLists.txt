add_library(botlib_ea STATIC
    ea_main.c
)

target_include_directories(botlib_ea
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(botlib_ea
    PUBLIC
        botlib_common
        q2bridge
)
