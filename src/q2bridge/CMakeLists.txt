add_library(q2bridge STATIC
    bridge.c
)

target_include_directories(q2bridge
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
