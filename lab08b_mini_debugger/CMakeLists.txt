cmake_minimum_required(VERSION 3.10)
project(mini_debugger C)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/JvmtiCommon.cmake)

add_jvmti_agent(mini_debugger mini_debugger.c)
