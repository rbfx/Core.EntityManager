cmake_minimum_required(VERSION 3.21)
project (Plugin.Core.EntityManager)

file (GLOB_RECURSE SOURCE_FILES *.h *.cpp)
add_plugin (${PROJECT_NAME} "${SOURCE_FILES}")
