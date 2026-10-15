cmake_minimum_required(VERSION 3.10)
project(lock_dump_agent C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(lock_dump_agent lock_dump_agent.c)
