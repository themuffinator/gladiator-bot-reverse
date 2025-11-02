add_executable(bspc
    src/main.cpp
    src/bsp_builder.cpp
    src/bsp_to_map.cpp
    src/bsp_serializers.cpp
    src/aas_tmp.cpp
    src/brush_bsp.cpp
    src/csg.cpp
    src/filesystem_helper.cpp
    src/leakfile.cpp
    src/legacy_common.cpp
    src/logging.cpp
    src/map_parser.cpp
    src/memory.cpp
    src/pipelines.cpp
    src/portals.cpp
    src/prtfile.cpp
    src/threads.cpp
    src/tree.cpp
)

target_include_directories(bspc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/shared
)

target_compile_features(bspc PRIVATE cxx_std_17)
