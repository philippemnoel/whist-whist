# Install script for directory: C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/json-c.lib")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/json-c.dll")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c/json-c-targets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c/json-c-targets.cmake"
         "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/CMakeFiles/Export/lib/cmake/json-c/json-c-targets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c/json-c-targets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c/json-c-targets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c" TYPE FILE FILES "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/CMakeFiles/Export/lib/cmake/json-c/json-c-targets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c" TYPE FILE FILES "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/CMakeFiles/Export/lib/cmake/json-c/json-c-targets-debug.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/json-c" TYPE FILE FILES "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/json-c-config.cmake")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/config.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_config.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/arraylist.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/debug.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_c_version.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_inttypes.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_object.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_object_iterator.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_pointer.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_tokener.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_util.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/json_visit.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/linkhash.h;C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c/printbuf.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/install/x64-Debug/include/json-c" TYPE FILE FILES
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/config.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/json_config.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/arraylist.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/debug.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_c_version.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_inttypes.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_object.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_object_iterator.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_pointer.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_tokener.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_util.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/json_visit.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/linkhash.h"
    "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/printbuf.h"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/tests/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "C:/Users/vm1/Downloads/fractal-protocol/desktop/include/json-c/out/build/x64-Debug/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
