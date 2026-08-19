include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Reconstruction version
#
# This is the version of *this reconstruction project*, and it is deliberately
# independent of the legacy Gladiator botlib version.
#
#   Legacy botlib version : 0.96  -- what the module reports to the host.
#                                    BotVersion() returns the literal string
#                                    "BotLib v0.96" and BotSetupLibrary prints
#                                    it in the startup banner.  Both are part
#                                    of the ABI the original Gladiator mod was
#                                    built against and both are pinned by
#                                    address in the parity contract
#                                    (tests/reference/botlib_contract.json,
#                                    0x10037bef).  It must never be bumped to
#                                    describe reconstruction progress.
#
#   Reconstruction version: below  -- what *we* ship.  It versions the accuracy
#                                    of the reconstruction, the build system,
#                                    the tooling and the documentation.  It is
#                                    what release archives are named after and
#                                    what the Windows VERSIONINFO resource
#                                    reports.  It never reaches the host at
#                                    runtime, so bumping it cannot perturb
#                                    parity.
#
# Bump this on every release.  See CHANGELOG.md for the recorded history and
# docs/reconstruction_versioning.md for the policy.
# ---------------------------------------------------------------------------

set(GLADIATOR_RECON_VERSION "1.0.0"
    CACHE STRING "Version of the Gladiator botlib reconstruction (independent of the legacy 0.96 botlib version)")

# The legacy botlib version reproduced by this module.  Frozen: it describes
# the retail binary being reconstructed, not the reconstruction.
set(GLADIATOR_LEGACY_BOTLIB_VERSION "0.96")

set(GLADIATOR_RECON_PRODUCT_NAME "Q2 Gladiator Bot Botlib Reconstruction")

if(NOT GLADIATOR_RECON_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR
        "GLADIATOR_RECON_VERSION must be MAJOR.MINOR.PATCH with an optional "
        "pre-release suffix (e.g. 1.0.0 or 1.1.0-rc1); got '${GLADIATOR_RECON_VERSION}'")
endif()

set(GLADIATOR_RECON_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(GLADIATOR_RECON_VERSION_MINOR "${CMAKE_MATCH_2}")
set(GLADIATOR_RECON_VERSION_PATCH "${CMAKE_MATCH_3}")
set(GLADIATOR_RECON_VERSION_PRERELEASE "${CMAKE_MATCH_4}")

# MAJOR.MINOR.PATCH with the pre-release suffix stripped, for consumers that
# cannot express one (project(), VERSIONINFO's binary FILEVERSION field).
set(GLADIATOR_RECON_VERSION_NUMERIC
    "${GLADIATOR_RECON_VERSION_MAJOR}.${GLADIATOR_RECON_VERSION_MINOR}.${GLADIATOR_RECON_VERSION_PATCH}")

# ---------------------------------------------------------------------------
# Build provenance
#
# Both fields degrade to a fixed placeholder rather than failing: a source
# archive with no .git directory must still configure and build.
# ---------------------------------------------------------------------------

function(_gladiator_resolve_git_commit _output_var)
    find_package(Git QUIET)
    if(NOT GIT_FOUND OR NOT IS_DIRECTORY "${PROJECT_SOURCE_DIR}/.git")
        set(${_output_var} "unknown" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE _commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _status
    )

    if(NOT _status EQUAL 0 OR _commit STREQUAL "")
        set(${_output_var} "unknown" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE _dirty
        ERROR_QUIET
    )
    if(NOT _dirty EQUAL 0)
        string(APPEND _commit "-dirty")
    endif()

    set(${_output_var} "${_commit}" PARENT_SCOPE)
endfunction()

_gladiator_resolve_git_commit(GLADIATOR_RECON_GIT_COMMIT)

string(TIMESTAMP GLADIATOR_RECON_BUILD_DATE "%Y-%m-%d" UTC)

# Human-readable one-liner used by the banner string baked into the module and
# by the release notes.  Example:
#   Q2 Gladiator Bot Botlib Reconstruction 1.0.0 (a1b2c3d4e5f6, 2026-08-20)
set(GLADIATOR_RECON_VERSION_FULL
    "${GLADIATOR_RECON_VERSION} (${GLADIATOR_RECON_GIT_COMMIT}, ${GLADIATOR_RECON_BUILD_DATE})")

# ---------------------------------------------------------------------------
# Generated artefacts
# ---------------------------------------------------------------------------

# Emits shared/gladiator_version.h into the build tree and returns the include
# root that must be added to the compiler search path.
function(gladiator_generate_version_header _include_root_var)
    set(_root "${PROJECT_BINARY_DIR}/generated")
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/shared/gladiator_version.h.in"
        "${_root}/shared/gladiator_version.h"
        @ONLY
    )
    set(${_include_root_var} "${_root}" PARENT_SCOPE)
endfunction()

# Emits the Windows VERSIONINFO resource and returns its path, or an empty
# string when the platform or toolchain cannot compile one.  Callers must
# tolerate the empty result.
function(gladiator_generate_version_resource _resource_var)
    set(${_resource_var} "" PARENT_SCOPE)

    if(NOT WIN32)
        return()
    endif()

    include(CheckLanguage)
    check_language(RC)
    if(NOT CMAKE_RC_COMPILER)
        message(STATUS
            "No resource compiler found; gladiator.dll will be built without a "
            "VERSIONINFO record.  The reconstruction version is still readable "
            "from the module's embedded version marker string.")
        return()
    endif()

    enable_language(RC)

    set(_resource "${PROJECT_BINARY_DIR}/generated/gladiator_version.rc")
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/shared/gladiator_version.rc.in"
        "${_resource}"
        @ONLY
    )
    set(${_resource_var} "${_resource}" PARENT_SCOPE)
endfunction()
