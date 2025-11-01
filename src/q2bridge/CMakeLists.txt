add_library(q2bridge STATIC
    aas_translation.c
    bridge.c
    bridge_config.c
    update_translator.c
)

target_include_directories(q2bridge
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
