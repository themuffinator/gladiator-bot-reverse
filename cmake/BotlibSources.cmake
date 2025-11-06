include_guard(GLOBAL)

function(register_botlib_sources)
    set(options)
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(RBS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT RBS_TARGET)
        message(FATAL_ERROR "register_botlib_sources requires a TARGET argument")
    endif()

    if(NOT RBS_SOURCES)
        return()
    endif()

    set(_absolute_sources)
    foreach(_source IN LISTS RBS_SOURCES)
        if(IS_ABSOLUTE "${_source}")
            list(APPEND _absolute_sources "${_source}")
        else()
            list(APPEND _absolute_sources "${CMAKE_CURRENT_SOURCE_DIR}/${_source}")
        endif()
    endforeach()

    set_property(GLOBAL APPEND PROPERTY BOTLIB_REGISTERED_SOURCES "${_absolute_sources}")
    set_property(GLOBAL APPEND PROPERTY BOTLIB_REGISTERED_TARGETS "${RBS_TARGET}")
endfunction()

function(_botlib_relativize _input_list _output_var)
    set(_result)
    foreach(_path IN LISTS ${_input_list})
        file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}" "${_path}")
        list(APPEND _result "${_relative}")
    endforeach()
    list(REMOVE_DUPLICATES _result)
    set(${_output_var} "${_result}" PARENT_SCOPE)
endfunction()

function(_botlib_collect_target_sources _target _output_var)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "_botlib_collect_target_sources: '${_target}' is not a valid target")
    endif()

    get_target_property(_sources "${_target}" SOURCES)

    if(NOT _sources)
        set(${_output_var} "" PARENT_SCOPE)
        return()
    endif()

    get_target_property(_source_dir "${_target}" SOURCE_DIR)

    set(_absolute_sources)
    foreach(_source IN LISTS _sources)
        if(IS_ABSOLUTE "${_source}")
            list(APPEND _absolute_sources "${_source}")
        else()
            if(NOT _source_dir)
                message(FATAL_ERROR "Target '${_target}' reported a relative source but has no SOURCE_DIR")
            endif()
            get_filename_component(_resolved "${_source_dir}/${_source}" ABSOLUTE)
            list(APPEND _absolute_sources "${_resolved}")
        endif()
    endforeach()

    set(${_output_var} "${_absolute_sources}" PARENT_SCOPE)
endfunction()

function(audit_gladiator_sources)
    set(options)
    set(oneValueArgs SOURCE_ROOT)
    set(multiValueArgs TARGETS)
    cmake_parse_arguments(AGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT AGS_SOURCE_ROOT)
        message(FATAL_ERROR "audit_gladiator_sources requires SOURCE_ROOT to be specified")
    endif()

    if(NOT AGS_TARGETS)
        message(FATAL_ERROR "audit_gladiator_sources requires at least one target")
    endif()

    file(GLOB_RECURSE _tree_sources
        LIST_DIRECTORIES FALSE
        CONFIGURE_DEPENDS
        "${AGS_SOURCE_ROOT}/*.c"
    )

    set(_registered_sources)
    foreach(_target IN LISTS AGS_TARGETS)
        _botlib_collect_target_sources("${_target}" _target_sources)
        list(APPEND _registered_sources ${_target_sources})
    endforeach()

    list(REMOVE_DUPLICATES _registered_sources)

    _botlib_relativize(_tree_sources _tree_sources_rel)
    _botlib_relativize(_registered_sources _registered_sources_rel)

    set(_missing)
    foreach(_source IN LISTS _tree_sources_rel)
        list(FIND _registered_sources_rel "${_source}" _index)
        if(_index EQUAL -1)
            list(APPEND _missing "${_source}")
        endif()
    endforeach()

    set(_untracked)
    foreach(_source IN LISTS _registered_sources_rel)
        list(FIND _tree_sources_rel "${_source}" _index)
        if(_index EQUAL -1)
            list(APPEND _untracked "${_source}")
        endif()
    endforeach()

    if(_missing)
        list(SORT _missing)
        string(REPLACE ";" "\n  " _missing_message "${_missing}")
        message(FATAL_ERROR "Gladiator source audit detected unreferenced sources:\n  ${_missing_message}")
    endif()

    if(_untracked)
        list(SORT _untracked)
        string(REPLACE ";" "\n  " _untracked_message "${_untracked}")
        message(FATAL_ERROR "Gladiator source audit detected unexpected registered sources:\n  ${_untracked_message}")
    endif()
endfunction()

function(verify_all_botlib_sources)
    if(NOT ARGN)
        message(FATAL_ERROR "verify_all_botlib_sources requires at least one directory")
    endif()

    set(BOTLIB_DISCOVERED_SOURCES)

    foreach(BOTLIB_ROOT IN LISTS ARGN)
        if(NOT IS_DIRECTORY "${BOTLIB_ROOT}")
            message(FATAL_ERROR "verify_all_botlib_sources: '${BOTLIB_ROOT}' is not a directory")
        endif()

        file(GLOB_RECURSE _botlib_root_sources
            LIST_DIRECTORIES FALSE
            CONFIGURE_DEPENDS
            "${BOTLIB_ROOT}/*.c"
        )

        list(APPEND BOTLIB_DISCOVERED_SOURCES ${_botlib_root_sources})
    endforeach()

    get_property(BOTLIB_REGISTERED_SOURCES GLOBAL PROPERTY BOTLIB_REGISTERED_SOURCES)
    if(NOT BOTLIB_REGISTERED_SOURCES)
        set(BOTLIB_REGISTERED_SOURCES)
    endif()

    _botlib_relativize(BOTLIB_DISCOVERED_SOURCES BOTLIB_DISCOVERED_REL)
    _botlib_relativize(BOTLIB_REGISTERED_SOURCES BOTLIB_REGISTERED_REL)

    set(BOTLIB_MISSING)
    foreach(_source IN LISTS BOTLIB_DISCOVERED_REL)
        list(FIND BOTLIB_REGISTERED_REL "${_source}" _index)
        if(_index EQUAL -1)
            list(APPEND BOTLIB_MISSING "${_source}")
        endif()
    endforeach()

    set(BOTLIB_ORPHANED)
    foreach(_source IN LISTS BOTLIB_REGISTERED_REL)
        list(FIND BOTLIB_DISCOVERED_REL "${_source}" _index)
        if(_index EQUAL -1)
            list(APPEND BOTLIB_ORPHANED "${_source}")
        endif()
    endforeach()

    if(BOTLIB_MISSING)
        list(SORT BOTLIB_MISSING)
        string(REPLACE ";" "\n  " BOTLIB_MISSING_MESSAGE "${BOTLIB_MISSING}")
        message(FATAL_ERROR "Untracked botlib sources detected:\n  ${BOTLIB_MISSING_MESSAGE}")
    endif()

    if(BOTLIB_ORPHANED)
        list(SORT BOTLIB_ORPHANED)
        string(REPLACE ";" "\n  " BOTLIB_ORPHANED_MESSAGE "${BOTLIB_ORPHANED}")
        message(FATAL_ERROR "Botlib sources registered but not present on disk:\n  ${BOTLIB_ORPHANED_MESSAGE}")
    endif()
endfunction()
