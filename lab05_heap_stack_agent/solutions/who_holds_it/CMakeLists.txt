cmake_minimum_required(VERSION 3.10)
project(leak_path_agent C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(leak_path_agent leak_path_agent.c)
