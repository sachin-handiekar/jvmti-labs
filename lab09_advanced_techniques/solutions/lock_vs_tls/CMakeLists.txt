cmake_minimum_required(VERSION 3.10)
project(counter_bench C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(counter_bench counter_bench.c)
