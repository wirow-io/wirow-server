cmake_minimum_required(VERSION 3.18.0 FATAL_ERROR)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")
include(CMakeToolsHelpers OPTIONAL)
set(DEB_CHANGELOG_REQUIRED ON)
set(DEB_CHANGELOG "${CMAKE_CURRENT_SOURCE_DIR}/Changelog")
unset(CHANGELOG_LAST_VERSION)
unset(CHANGELOG_LAST_MESSAGE)
include(DebChangelog)

set(PROJECT_NAME "greenrooms")
project(
  ${PROJECT_NAME}
  VERSION 1.0.0
  LANGUAGES C CXX)

set(PROJECT_VENDOR "Softmotions (https://softmotions.com)")
set(PROJECT_WEBSITE "https://wirow.io")
set(PROJECT_MAINTAINER "Anton Adamansky <adamansky@softmotions.com>")
set(PROJECT_DESCRIPTION_SUMMARY "Wirow video conferencing system")
set(PROJECT_DESCRIPTION "Wirow video conferencing system")
set(PROJECT_VERSION_MAJOR ${CHANGELOG_LAST_VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${CHANGELOG_LAST_VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${CHANGELOG_LAST_VERSION_PATCH})
set(PROJECT_VERSION
    ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
set(${PROJECT_NAME}_VERSION ${PROJECT_VERSION})
set(${PROJECT_NAME}_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(${PROJECT_NAME}_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(${PROJECT_NAME}_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build test cases" OFF)
option(ASAN "Turn on address sanitizer" OFF)
option(UBSAN "Turn on UB address sanitizer" OFF)
option(PACKAGE_TGZ "Build .tgz package archive" ON)
option(PACKAGE_ZIP "Build .zip package archive" ON)
option(BUILD_DISTRIB "Build distribution package" OFF)

option(LICENSE_SSL_VERIFY "Verify SSL connections" ON)
option(CREATE_DEV_USER "Create dev testing user" OFF)
option(ENABLE_WHITEBOARD "Enable whiteboard feature" ON)
option(ENABLE_RECORDING "Enable recording feature" ON)
option(LIMIT_REGISTERED_USERS "Limit registered users" OFF)
option(SENTRY_DSN "Backend sentry dsn if any or OFF" OFF)
option(SENTRY_FRONT_DSN "Frontend sentry dsn if any or OFF" OFF)

if(SENTRY_FRONT_DSN)
  string(REGEX
         REPLACE "^(https?)://(.+)/([0-9]+)$" "\\1://\\2/api/\\3/envelope/"
                 SENTRY_FRONT_ENVELOPE ${SENTRY_FRONT_DSN})
  if(${SENTRY_FRONT_ENVELOPE} STREQUAL ${SENTRY_FRONT_DSN})
    message(FATAL_ERROR "Incorrect SENTRY_FRONT_DSN: \"${SENTRY_FRONT_DSN}\"")
  endif()
else()
  set(SENTRY_FRONT_ENVELOPE ${SENTRY_FRONT_DSN})
endif()

include(GitRevision)
include(GNUInstallDirs)
include(ProjectUtils)

macro_ensure_out_of_source_build(
  "${CMAKE_PROJECT_NAME} requires an out of source build.")

if(BUILD_TESTS)
  include(CTest)
  enable_testing()
  find_package(CUnit REQUIRED)
  add_definitions(-DIW_TESTS)
endif(BUILD_TESTS)

set(CPACK_GENERATORS)
if(PACKAGE_TGZ)
  list(APPEND CPACK_GENERATORS "TGZ")
endif()
if(PACKAGE_ZIP)
  list(APPEND CPACK_GENERATORS "ZIP")
endif()

if(CPACK_GENERATORS)
  set(CPACK_GENERATOR "${CPACK_GENERATORS}")
  set(CPACK_SOURCE_IGNORE_FILES
      "/build/"
      "/extra/"
      "/cmake-.*/"
      "/node_modules/"
      "/Makefile$"
      "/\\\\.git/"
      "/\\\\.clang/"
      "/\\\\.vscode/"
      "/\\\\.idea/"
      "\\\\.sh$"
      "\\\\.project$"
      "\\\\.workspace$"
      "\\\\.iml$"
      "\\\\.mk$"
      "\\\\.astylerc$"
      "\\\\.editorconfig$")
  set(PROJECT_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  add_subdirectory(installer)
endif(CPACK_GENERATORS)

if(NOT ENABLE_PPA)
  add_subdirectory(tools)
  add_subdirectory(src)
else()
  set(PACKAGE_DEB ON)
endif()

message("${PROJECT_NAME} GIT_REVISION: ${GIT_REVISION}")
message("${PROJECT_NAME} CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message("${PROJECT_NAME} CPACK_GENERATORS: ${CPACK_GENERATORS}")
message("${PROJECT_NAME} CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message("${PROJECT_NAME} IW_EXEC: ${IW_EXEC}")
if(MXE_HOME)
  message("${PROJECT_NAME} MXE_HOME: ${MXE_HOME}")
endif()
if(CMAKE_SYSTEM_NAME)
  message("${PROJECT_NAME} CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
endif()
message("${PROJECT_NAME} CPU: ${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_SIZEOF_VOID_P)
  message("${PROJECT_NAME} SIZEOF *VOID: ${CMAKE_SIZEOF_VOID_P}")
endif()
message("${PROJECT_NAME} PROJECT: ${CHANGELOG_LAST_LINE}")
message("${PROJECT_NAME} CREATE_DEV_USER: ${CREATE_DEV_USER}")
message("${PROJECT_NAME} ENABLE_WHITEBOARD: ${ENABLE_WHITEBOARD}")
message("${PROJECT_NAME} ENABLE_RECORDING: ${ENABLE_RECORDING}")
message("${PROJECT_NAME} LIMIT_REGISTERED_USERS: ${LIMIT_REGISTERED_USERS}")
message("${PROJECT_NAME} SENTRY_DSN: ${SENTRY_DSN}")
message("${PROJECT_NAME} SENTRY_FRONT_DSN: ${SENTRY_FRONT_DSN}")
message("${PROJECT_NAME} LICENSE_SSL_VERIFY: ${LICENSE_SSL_VERIFY} ")
if(CHANGELOG_MESSAGE)
  message("${PROJECT_NAME} CHANGELOG_MESSAGE:\n  ${CHANGELOG_MESSAGE}")
endif()
