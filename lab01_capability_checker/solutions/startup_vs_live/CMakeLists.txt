cmake_minimum_required(VERSION 3.10)
project(capabilities_by_phase C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(capabilities_by_phase capabilities_by_phase.c)
