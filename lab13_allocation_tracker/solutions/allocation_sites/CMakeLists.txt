cmake_minimum_required(VERSION 3.10)
project(alloc_sites C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(alloc_sites alloc_sites.c)
