cmake_minimum_required(VERSION 3.10)
project(return_watch C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(return_watch return_watch.c)
