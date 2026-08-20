# ---------------------------------------------------------------------------
# Gladiator Bot game module for Quake II
#
# Mr Elusive's v0.96 game source, as released in gladq2096gamesrc.zip, built
# unmodified apart from the portability fixes marked "PORT(...)" in the source.
# See docs/game_source_integration.md for what was changed and why.
#
# This is a self-contained module: it carries its own q_shared.{c,h}, game.h
# and botlib.h and shares no headers with the botlib reconstruction, exactly
# as the two DLLs shipped in 1999.  It talks to the botlib only across the
# GetBotAPI boundary, resolved at runtime by bl_main.c.
# ---------------------------------------------------------------------------

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(SORT GAME_SOURCES)

if(NOT GAME_SOURCES)
    message(FATAL_ERROR "No game sources found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_library(game SHARED ${GAME_SOURCES})

target_include_directories(game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# The module name the engine looks for differs by platform and word size.
# Retail Quake II loads gamex86.dll / gamei386.so; 64-bit source ports
# (Q2PRO, yquake2) load gamex86_64.dll / gamex86_64.so.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_game_output_name "gamex86_64")
elseif(WIN32)
    set(_game_output_name "gamex86")
else()
    set(_game_output_name "gamei386")
endif()

set_target_properties(game
    PROPERTIES
        OUTPUT_NAME "${_game_output_name}"
        PREFIX ""
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        C_STANDARD 99
        # ON (the default) matters here: the 1999 source calls strdup,
        # strcasecmp and access, which strict -std=c99 hides.  Stated
        # explicitly so a future tightening does not silently break it.
        C_EXTENSIONS ON
)

# C_ONLY is the source's own switch for "portable C, no hand-written assembly".
# It forces q_shared.h's id386 to 0, which selects the C BoxOnPlaneSide over the
# MSVC __declspec(naked) version, and drops the inline-asm Q_ftol (which nothing
# in this tree ever calls).  Modern compilers reject MSVC naked functions
# containing inline asm, and the 64-bit builds already take the C path because
# id386 is 0 there -- defining it everywhere keeps all three release builds
# behaviourally identical rather than only the 64-bit ones.
target_compile_definitions(game PRIVATE C_ONLY)

if(WIN32)
    # q_shared.c tests bare `WIN32` (not `_WIN32`) to pick _stricmp over
    # strcasecmp.  MSVC6 got that from its project settings; today it arrives
    # only via CMake's default flags, which a build that overrides
    # CMAKE_C_FLAGS -- as the 32-bit release build does -- silently drops.
    # Define it here so the choice never depends on flag ordering.
    target_compile_definitions(game PRIVATE WIN32)
else()
    # The 1999 source calls the Win32 spellings of the case-insensitive compares.
    target_compile_definitions(game PRIVATE stricmp=strcasecmp)
endif()

# GetGameAPI is the module's entire public surface.
#
# g_main.c declares it bare -- no __declspec(dllexport) -- exactly as it did in
# 1999, so the export has to come from the linker.  There is no fallback: if
# none of the branches below fires, the module builds cleanly and exports
# nothing, and the engine's GetProcAddress/dlsym lookup fails at load time.
# That is why the final else() is fatal rather than silent.
#
# On Windows the branch cannot key off MSVC: that is true for cl and clang-cl
# but false for the GNU-driver clang.exe, which still targets the MSVC ABI and
# links through lld-link.  Key off the frontend variant instead.
set(_game_def "${CMAKE_CURRENT_SOURCE_DIR}/game.def")
set(_game_version_script "${CMAKE_CURRENT_SOURCE_DIR}/game.exports")

if(WIN32)
    if(MINGW)
        # MinGW's ld consumes a .def listed as an ordinary input.
        target_sources(game PRIVATE "${_game_def}")
    elseif(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Same game.def the original MSVC6 project used.
        target_link_options(game PRIVATE "/DEF:${_game_def}")
    else()
        # clang.exe (GNU driver, MSVC ABI): the flag still has to reach
        # lld-link, but only -Wl, gets it past the GNU-style driver.
        target_link_options(game PRIVATE "-Wl,/DEF:${_game_def}")
    endif()
    set_property(TARGET game APPEND PROPERTY LINK_DEPENDS "${_game_def}")
elseif(APPLE)
    set(_game_export_list "${CMAKE_CURRENT_BINARY_DIR}/game.symbols")
    file(GENERATE OUTPUT "${_game_export_list}" CONTENT "_GetGameAPI\n")
    target_link_options(game PRIVATE "LINKER:-exported_symbols_list,${_game_export_list}")
elseif(UNIX)
    target_link_options(game PRIVATE
        "LINKER:--version-script=${_game_version_script}")
    set_property(TARGET game APPEND PROPERTY LINK_DEPENDS "${_game_version_script}")
else()
    message(FATAL_ERROR
        "No way to export GetGameAPI for this toolchain. Building anyway would "
        "produce a game module the engine cannot load, with no diagnostic. Add "
        "a branch here for it.")
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(game PRIVATE m ${CMAKE_DL_LIBS})
endif()

# 1999 C, compiled by a 2020s compiler.  These categories fire in the hundreds
# across the tree and none of them indicate a defect worth chasing in code that
# is being preserved rather than rewritten; everything else stays enabled so a
# genuine problem still surfaces.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game PRIVATE
        -Wno-unused-but-set-variable
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-parentheses
        -Wno-misleading-indentation
        -Wno-implicit-fallthrough
        -Wno-unknown-pragmas
        -Wno-format-truncation
    )
endif()

# -Wstringop-* are GCC-only; passing the -Wno- forms to Clang earns
# unknown-warning-option noise, which is exactly the kind of spurious
# diagnostic this block exists to remove.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(game PRIVATE
        -Wno-stringop-truncation
        -Wno-stringop-overflow
    )
endif()
if(MSVC)
    target_compile_definitions(game PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(
    TARGETS game
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
