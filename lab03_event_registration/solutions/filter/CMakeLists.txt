cmake_minimum_required(VERSION 3.10)
project(filtered_counter C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(filtered_counter filtered_counter.c)
