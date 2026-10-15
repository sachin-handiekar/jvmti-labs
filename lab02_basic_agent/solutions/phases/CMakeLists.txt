cmake_minimum_required(VERSION 3.10)
project(phase_agent C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(phase_agent phase_agent.c)
