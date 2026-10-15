cmake_minimum_required(VERSION 3.10)
project(int_patcher C)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JvmtiCommon.cmake)

add_jvmti_agent(int_patcher int_patcher.c)
