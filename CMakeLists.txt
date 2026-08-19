cmake_minimum_required(VERSION 3.16)

# Q2 Gladiator Bot Botlib Reconstruction -- an open reconstruction of the
# Gladiator Bot botlib that Mr Elusive wrote for Quake II in 1999.
#
# The version carried here is the *reconstruction* version and is independent
# of the legacy 0.96 botlib version the module reports to the host.  See
# cmake/ReconstructionVersion.cmake for the distinction and the policy.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(Q2GladiatorBotlibReconstruction LANGUAGES C CXX)

include(BotlibSources)
include(ReconstructionVersion)

message(STATUS
    "${GLADIATOR_RECON_PRODUCT_NAME} ${GLADIATOR_RECON_VERSION_FULL} "
    "(reproducing botlib v${GLADIATOR_LEGACY_BOTLIB_VERSION})")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# Shared headers export Quake conventions (id386, idaxp, etc.) and the
# generated reconstruction version header.
add_subdirectory(src/shared)

add_subdirectory(src/botlib/common)
add_subdirectory(src/botlib/precomp)
add_subdirectory(src/botlib/aas)
add_subdirectory(src/botlib/ea)
add_subdirectory(src/botlib/ai)
add_subdirectory(src/q2bridge)
add_subdirectory(src/botlib/interface)

set(_botlib_source_directories
    ${PROJECT_SOURCE_DIR}/src/botlib/common
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp
    ${PROJECT_SOURCE_DIR}/src/botlib/aas
    ${PROJECT_SOURCE_DIR}/src/botlib/ea
    ${PROJECT_SOURCE_DIR}/src/botlib/ai
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_chat
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_character
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_goal
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_move
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_weapon
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_weight
    ${PROJECT_SOURCE_DIR}/src/botlib/interface
)

verify_all_botlib_sources(${_botlib_source_directories})

add_library(gladiator SHARED)

get_property(_gladiator_botlib_sources GLOBAL PROPERTY BOTLIB_REGISTERED_SOURCES)
if(_gladiator_botlib_sources)
    target_sources(gladiator PRIVATE ${_gladiator_botlib_sources})
endif()

# File metadata only, and only where a resource compiler exists; the module's
# runtime behaviour is identical with or without it.
gladiator_generate_version_resource(_gladiator_version_resource)
if(_gladiator_version_resource)
    target_sources(gladiator PRIVATE "${_gladiator_version_resource}")
endif()
target_link_libraries(gladiator
    PRIVATE
        botlib_interface
        botlib_common
        botlib_precomp
        botlib_aas
        botlib_ai
        botlib_ea
        q2bridge
)
set_target_properties(gladiator
    PROPERTIES
        OUTPUT_NAME gladiator
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

if(UNIX)
    set_target_properties(gladiator
        PROPERTIES
            C_VISIBILITY_PRESET hidden
    )
endif()

if(APPLE)
    set(gladiator_export_list
        "${CMAKE_CURRENT_BINARY_DIR}/gladiator.exports"
    )
    file(GENERATE
        OUTPUT "${gladiator_export_list}"
        CONTENT "_GetBotAPI\n"
    )
    target_link_options(gladiator
        PRIVATE
            "LINKER:-exported_symbols_list,${gladiator_export_list}"
    )
elseif(UNIX)
    target_link_options(gladiator
        PRIVATE
            "LINKER:--exclude-libs,ALL"
    )
endif()

audit_gladiator_sources(
    SOURCE_ROOT "${PROJECT_SOURCE_DIR}/src"
    TARGETS
        gladiator
        q2bridge
)

install(
    TARGETS gladiator
    EXPORT GladiatorTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    EXPORT GladiatorTargets
    NAMESPACE gladiator::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gladiator
)

add_subdirectory(tools/bspc)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
