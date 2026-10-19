# Shared test registration for Noch.
#
# Usage:
#   noch_add_unit_test(tst_name
#       SOURCES tests/tst_name.cpp src/module/Thing.cpp ...
#       LIBS    Qt6::Core Qt6::Network
#   )
#
# Every test gets the source include paths from the noch_includes interface
# target, the Qt Test framework, and a headless-friendly environment.

function(noch_add_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;DEFS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "noch_add_unit_test(${name}): SOURCES is required")
    endif()

    # The tests live in a subdirectory, but source paths in tests/CMakeLists.txt
    # are written relative to the repository root for readability.
    set(sources)
    foreach(source IN LISTS ARG_SOURCES)
        if(IS_ABSOLUTE "${source}")
            list(APPEND sources "${source}")
        else()
            list(APPEND sources "${PROJECT_SOURCE_DIR}/${source}")
        endif()
    endforeach()

    qt_add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE Qt6::Test noch_includes ${ARG_LIBS})
    if(ARG_DEFS)
        target_compile_definitions(${name} PRIVATE ${ARG_DEFS})
    endif()

    add_test(NAME ${name} COMMAND ${name})

    # The headless "offscreen" platform plugin is not part of every Qt install
    # (the Windows installers ship only "windows"), so it is requested only
    # where it exists. Tests never play sound.
    set(env "NOCH_NO_AUDIO=1")
    if(NOT WIN32)
        list(APPEND env "QT_QPA_PLATFORM=offscreen")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()
