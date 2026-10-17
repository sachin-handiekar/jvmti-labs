cmake_minimum_required(VERSION 3.10)
project(self_time_profiler C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(self_time_profiler self_time_profiler.c)
