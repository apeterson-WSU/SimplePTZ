# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\SimplePTZ_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\SimplePTZ_autogen.dir\\ParseCache.txt"
  "SimplePTZ_autogen"
  )
endif()
