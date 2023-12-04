file (GLOB_RECURSE SOURCE_FILES *.h *.cpp)

set (TARGET_NAME Plugin.Core.EntityManager)
add_library(${TARGET_NAME} SHARED ${SOURCE_FILES})
define_static_plugin (${TARGET_NAME} "${TARGET_NAME}")
target_link_libraries (${TARGET_NAME} PRIVATE Urho3D)
