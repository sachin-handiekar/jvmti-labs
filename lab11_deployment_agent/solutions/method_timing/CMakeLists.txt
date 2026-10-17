cmake_minimum_required(VERSION 3.10)
project(method_timer C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(method_timer method_timer.c)
