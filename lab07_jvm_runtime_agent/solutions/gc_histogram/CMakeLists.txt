cmake_minimum_required(VERSION 3.10)
project(gc_histogram_agent C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(gc_histogram_agent gc_histogram_agent.c)
