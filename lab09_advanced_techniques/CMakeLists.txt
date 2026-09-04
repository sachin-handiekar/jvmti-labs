cmake_minimum_required(VERSION 3.10)
project(advanced_techniques C)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/JvmtiCommon.cmake)

add_jvmti_agent(advanced_agent advanced_agent.c)
