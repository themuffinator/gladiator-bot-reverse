add_library(q2bridge STATIC
    bridge.c
    bridge_config.c
    update_translator.cpp
    aas_translation.cpp
)

target_include_directories(q2bridge
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
