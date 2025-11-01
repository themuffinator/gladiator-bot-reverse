add_library(q2bridge STATIC
    bridge.c
    bridge_config.c
    update_translator.c
    aas_translation.c
)

target_include_directories(q2bridge
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
