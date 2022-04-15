include(ExternalProject)

set(LIBEXCALIDRAW "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/excalidraw/excalidraw.tgz")

set(BUILD_COMMAND "${CMAKE_SOURCE_DIR}/cmake/Modules/AddExcalidraw/build.sh ${LIBEXCALIDRAW}")

ExternalProject_Add(
  extern_excalidraw
  PREFIX ${CMAKE_BINARY_DIR}
  URL ${CMAKE_SOURCE_DIR}/extra/excalidraw
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE ON
  BUILD_COMMAND sh -c "${BUILD_COMMAND}"
  INSTALL_COMMAND ""
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_custom_command(OUTPUT ${LIBEXCALIDRAW} DEPENDS extern_excalidraw)