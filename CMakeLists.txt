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

# Mr Elusive's v0.96 game module.  Independent of the botlib above: it is a
# separate DLL that finds the botlib at runtime through GetBotAPI.
option(BUILD_GAME_MODULE
    "Build the Gladiator game module (gamex86.dll / gamex86_64.dll / gamex86_64.so)" ON)
if(BUILD_GAME_MODULE)
    add_subdirectory(src/game)
endif()

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
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

# The game module loads the botlib by a hard-coded filename (BOTLIB_DEFAULT_LIBRARY
# in src/game/bl_spawn.c) with no fallback and no search, so the name emitted here
# has to be the name it asks for. Keep these two in step.
#
#   Windows 32-bit  gladiator.dll       retail's name, kept exactly
#   Windows 64-bit  gladiator_x64.dll
#   Linux   32-bit  gladi386.so         retail's name, kept exactly
#   Linux   64-bit  gladiator_x64.so
#
# The 64-bit builds need distinct names for the same reason the game module has
# gamex86.dll and gamex86_64.dll: without them the two word sizes collide on one
# filename, cannot sit in the same mod directory, and a mismatched pair fails at
# load time with little to show for it.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_gladiator_output_name "gladiator_x64")
elseif(WIN32)
    set(_gladiator_output_name "gladiator")
else()
    set(_gladiator_output_name "gladi386")
endif()

set_target_properties(gladiator PROPERTIES OUTPUT_NAME "${_gladiator_output_name}")

if(UNIX AND NOT APPLE)
    # Retail's .so names carry no `lib` prefix, and the game module asks for the
    # bare name; CMake's default would produce libgladiator_x64.so.
    set_target_properties(gladiator PROPERTIES PREFIX "")
endif()

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

# Every .c under src/ must belong to exactly one shipped module.
set(_gladiator_audit_targets gladiator q2bridge)
set(_gladiator_audit_excludes)
if(BUILD_GAME_MODULE)
    list(APPEND _gladiator_audit_targets game)
else()
    list(APPEND _gladiator_audit_excludes "${PROJECT_SOURCE_DIR}/src/game")
endif()

audit_gladiator_sources(
    SOURCE_ROOT "${PROJECT_SOURCE_DIR}/src"
    TARGETS ${_gladiator_audit_targets}
    EXCLUDE_DIRS ${_gladiator_audit_excludes}
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
