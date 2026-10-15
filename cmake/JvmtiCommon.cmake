# JvmtiCommon.cmake — Shared CMake configuration for all JVMTI agent labs
#
# Usage (in each lab's CMakeLists.txt):
#   cmake_minimum_required(VERSION 3.10)
#   project(<agent_name> C)
#   include(${CMAKE_CURRENT_LIST_DIR}/../cmake/JvmtiCommon.cmake)
#   add_jvmti_agent(<agent_name> <source_file.c>)

cmake_minimum_required(VERSION 3.10)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Portable JNI/JVMTI header discovery — works on Linux, macOS, and Windows
find_package(JNI REQUIRED)

# The labs' shared headers, found relative to THIS file, so agents at any
# depth (lab folders, labNN/solutions/...) can include jvmti_utils.h
set(JVMTI_LABS_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../common")

# Convenience function: creates a shared library target with correct includes and naming
function(add_jvmti_agent TARGET_NAME)
    # All remaining arguments are source files
    set(SOURCE_FILES ${ARGN})

    add_library(${TARGET_NAME} SHARED ${SOURCE_FILES})

    # The JDK's headers are included as SYSTEM headers: compilers don't
    # warn about code in them. Without this, Clang on macOS rejects a
    # declaration in jvmti.h itself (-Wstrict-prototypes) once warnings
    # are errors — a problem in the JDK, not in the agent.
    target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${JNI_INCLUDE_DIRS})
    target_include_directories(${TARGET_NAME} PRIVATE ${JVMTI_LABS_COMMON_DIR})

    # Keep the platform's conventional naming (§2.7):
    # libagent.so (Linux), libagent.dylib (macOS), agent.dll (Windows)
    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "${TARGET_NAME}"
    )

    # Enable warnings for better code quality. JVMTI callback signatures
    # are fixed by the spec, so unused-parameter warnings are suppressed.
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /wd4100)
        # The labs use portable standard C (strncpy, fopen, strtok...).
        # MSVC flags those as "unsafe" (C4996) in favour of its own _s
        # variants; accept the standard ones.
        target_compile_definitions(${TARGET_NAME} PRIVATE
            _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    endif()
endfunction()
