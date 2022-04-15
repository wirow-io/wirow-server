include(ExternalProject)

set(SENTRY_SOURCE_DIR ${CMAKE_SOURCE_DIR}/extra/sentry)
set(LIBSENTRY ${CMAKE_BINARY_DIR}/lib/libsentry.a)
set(LIBBREAKPAD ${CMAKE_BINARY_DIR}/lib/libbreakpad_client.a)

set(PATCH_COMMAND sh -c "${CMAKE_SOURCE_DIR}/cmake/Modules/AddSentry/patch.sh")

set(CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
    -DSENTRY_BUILD_SHARED_LIBS=OFF
    -DSENTRY_BUILD_TESTS=OFF
    -DSENTRY_BUILD_EXAMPLES=OFF
    -DCURL_INCLUDE_DIR=${CURL_SOURCE_DIR}/include
    -DCURL_LIBRARIES=${LIBCURL}
    )

if(DEFINED CMAKE_TOOLCHAIN_FILE)
  list(APPEND CMAKE_ARGS "-DFIND_ROOT=${CMAKE_BINARY_DIR}")
endif()

foreach(extra CMAKE_C_COMPILER CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ${extra})
    list(APPEND CMAKE_ARGS "-D${extra}=${${extra}}")
  endif()
endforeach()

ExternalProject_Add(
  extern_sentry
  PREFIX ${CMAKE_BINARY_DIR}
  URL ${SENTRY_SOURCE_DIR}
  BUILD_IN_SOURCE ON
  PATCH_COMMAND ${PATCH_COMMAND}
  CMAKE_ARGS ${CMAKE_ARGS}
  BUILD_BYPRODUCTS ${LIBSENTRY} ${LIBBREAKPAD}
  LOG_DOWNLOAD OFF
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_dependencies(extern_sentry extern_curl)

add_library(sentry_s STATIC IMPORTED GLOBAL)
add_dependencies(sentry_s extern_curl)
set_target_properties(sentry_s PROPERTIES IMPORTED_LOCATION ${LIBSENTRY})

add_library(breakpad_s STATIC IMPORTED GLOBAL)
add_dependencies(breakpad_s extern_curl)
set_target_properties(breakpad_s PROPERTIES IMPORTED_LOCATION ${LIBBREAKPAD})