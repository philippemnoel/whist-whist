find_program(
  CLANG_FORMAT_EXE
  NAMES "clang-format"
  DOC "Path to clang-format executable")
if(NOT CLANG_FORMAT_EXE)
  message(STATUS "Detecting clang-format - not found")
else()
  message(STATUS "Detecting clang-format - found: ${CLANG_FORMAT_EXE}")
endif()

# Get all project files
file(
  GLOB_RECURSE
  ALL_SOURCE_FILES
  CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/client/*.c
  ${PROJECT_SOURCE_DIR}/client/*.cpp
  ${PROJECT_SOURCE_DIR}/client/*.h
  ${PROJECT_SOURCE_DIR}/client/*.hpp
  ${PROJECT_SOURCE_DIR}/client/*.m
  ${PROJECT_SOURCE_DIR}/server/*.c
  ${PROJECT_SOURCE_DIR}/server/*.h
  ${PROJECT_SOURCE_DIR}/server/*.m
  ${PROJECT_SOURCE_DIR}/whist/*.c
  ${PROJECT_SOURCE_DIR}/whist/*.cpp
  ${PROJECT_SOURCE_DIR}/whist/*.h
  ${PROJECT_SOURCE_DIR}/whist/*.m
  ${PROJECT_SOURCE_DIR}/test/*.c
  ${PROJECT_SOURCE_DIR}/test/*.cpp
  ${PROJECT_SOURCE_DIR}/test/*.hpp
)

file(GLOB_RECURSE CLIENT_SOURCE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/client/*.c
    ${PROJECT_SOURCE_DIR}/client/*.h ${PROJECT_SOURCE_DIR}/client/*.cpp
    ${PROJECT_SOURCE_DIR}/client/*.hpp ${PROJECT_SOURCE_DIR}/client/*.m)

file(GLOB_RECURSE SERVER_SOURCE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/server/*.c
    ${PROJECT_SOURCE_DIR}/server/*.h CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/server/*.m)

file(GLOB_RECURSE WHIST_DIR_C_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/whist/*.c)

file(GLOB_RECURSE WHIST_DIR_H_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/whist/*.h)

file(GLOB_RECURSE WHIST_DIR_M_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/whist/*.m)

file(GLOB_RECURSE WHIST_DIR_CXX_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/whist/*.cpp)

file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/test/*.c
    ${PROJECT_SOURCE_DIR}/test/*.cpp ${PROJECT_SOURCE_DIR}/test/*.hpp)

list(FILTER WHIST_DIR_H_FILES EXCLUDE REGEX "^.*/nvidia-linux/.*$")
list(FILTER WHIST_DIR_C_FILES EXCLUDE REGEX "^.*/nvidia-linux/.*$")

list(FILTER WHIST_DIR_H_FILES EXCLUDE REGEX ".*/fec/lugi_rs.h")
list(FILTER WHIST_DIR_C_FILES EXCLUDE REGEX ".*/fec/lugi_rs.c")

list(FILTER WHIST_DIR_H_FILES EXCLUDE REGEX ".*/fec/(cm256|gf256|wirehair)/.*\.h")
list(FILTER WHIST_DIR_CXX_FILES EXCLUDE REGEX ".*/fec/(cm256|gf256|wirehair)/.*\.cpp")

add_custom_target(
  clang-format
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${CLIENT_SOURCE_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${SERVER_SOURCE_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${WHIST_DIR_C_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${WHIST_DIR_H_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${WHIST_DIR_M_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${WHIST_DIR_CXX_FILES}
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${TEST_FILES}
)

