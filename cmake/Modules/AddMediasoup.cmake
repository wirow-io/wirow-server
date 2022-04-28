include(ExternalProject)

set(MEDIASOUP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/extra/mediasoup)
set(MEDIASOUP_OUT_DIR ${CMAKE_BINARY_DIR}/mediasoup)
set(ABASE "${MEDIASOUP_OUT_DIR}/Release")

set(LIBMEDIASOUP "${ABASE}/libmediasoup-worker.a")

find_program(MAKE_EXEC gmake)
if(NOT MAKE_EXEC)
  set(MAKE_EXEC make)
endif()

set(MEDIASOUP_BUILD_COMMAND
  "${MAKE_EXEC} -C ${MEDIASOUP_SOURCE_DIR}/worker \
    MEDIASOUP_BUILDTYPE=Release MEDIASOUP_OUT_DIR=${MEDIASOUP_OUT_DIR} \
          libmediasoup-worker")

if(CMAKE_CROSSCOMPILING)
  if(CROSS_NAME)
    configure_file(
      ${CMAKE_CURRENT_LIST_DIR}/AddMediasoup/meson-${CROSS_NAME}.ini.in
      ${CMAKE_BINARY_DIR}/meson-${CROSS_NAME}.ini @ONLY)
    set(MEDIASOUP_BUILD_COMMAND
        "MESON_ARGS='--cross-file ${CMAKE_BINARY_DIR}/meson-${CROSS_NAME}.ini' ${MEDIASOUP_BUILD_COMMAND}"
    )
  else()
    message(
      FATAL_ERROR
        "Unknown cross-compilation environment, check the CROSS_NAME variable")
  endif()
else()
  if(DEFINED CMAKE_C_COMPILER)
    set(MEDIASOUP_BUILD_COMMAND
        "CC=${CMAKE_C_COMPILER} ${MEDIASOUP_BUILD_COMMAND}")
  endif()
  if(DEFINED CMAKE_CXX_COMPILER)
    set(MEDIASOUP_BUILD_COMMAND
        "CXX=${CMAKE_CXX_COMPILER} ${MEDIASOUP_BUILD_COMMAND}")
  endif()
endif()

ExternalProject_Add(
  extern_mediasoup
  BUILD_ALWAYS ON
  SOURCE_DIR ${MEDIASOUP_SOURCE_DIR}
  PREFIX ${CMAKE_BINARY_DIR}
  BUILD_IN_SOURCE ON
  BUILD_COMMAND sh -c "${MEDIASOUP_BUILD_COMMAND}"
  BUILD_BYPRODUCTS ${LIBMEDIASOUP}
  CONFIGURE_COMMAND ""
  INSTALL_COMMAND ""
  LOG_DOWNLOAD OFF
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_library(mediasoup_s STATIC IMPORTED GLOBAL)
add_dependencies(mediasoup_s extern_mediasoup)
set_target_properties(
  mediasoup_s PROPERTIES IMPORTED_LOCATION ${LIBMEDIASOUP}
                         IMPORTED_LINK_INTERFACE_LANGUAGES CXX)
