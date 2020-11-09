# CMakeList.txt : CMake project for UdimUnpack, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("UdimUnpack")

# Use relative paths
# This is mostly to reduce path size for command-line limits on windows
if(WIN32)
  set(CMAKE_USE_RELATIVE_PATHS true)
  set(CMAKE_SUPPRESS_REGENERATION true)
endif()

# find the FBX SDK
include("CMake/FindFBX.cmake")
if (NOT FBXSDK_FOUND)
    message(FATAL_ERROR "FBX SDK not found!")
endif()



set (SRC_FILES
	UdimUnpack.cpp
)



add_executable (${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${FBXSDK_INCLUDE_DIR}
)
target_link_libraries(${PROJECT_NAME} ${FRAMEWORKS} optimized ${FBXSDK_LIBS} debug ${FBXSDK_LIBS_DEBUG}) 

# Suppress LNK4099 (pdbs missing, FBX SDK doesn't include them)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")

