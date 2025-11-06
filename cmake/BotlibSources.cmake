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

function(verify_all_botlib_sources BOTLIB_ROOT)
    if(NOT IS_DIRECTORY "${BOTLIB_ROOT}")
        message(FATAL_ERROR "verify_all_botlib_sources: '${BOTLIB_ROOT}' is not a directory")
    endif()

    file(GLOB_RECURSE BOTLIB_DISCOVERED_SOURCES
        LIST_DIRECTORIES FALSE
        CONFIGURE_DEPENDS
        "${BOTLIB_ROOT}/*.c"
    )

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
