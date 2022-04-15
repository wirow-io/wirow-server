include(ExternalProject)

set(BYPRODUCT "${CMAKE_BINARY_DIR}/lib/libdeflate.a")

set(BUILD_ENV "PREFIX=${CMAKE_BINARY_DIR}")

if(DEFINED CMAKE_C_COMPILER)
  set(BUILD_ENV "CC=${CMAKE_C_COMPILER} ${BUILD_ENV}")
endif()

if(DEFINED CMAKE_CXX_COMPILER)
  set(BUILD_ENV "CXX=${CMAKE_CXX_COMPILER} ${BUILD_ENV}")
endif()

set(BUILD_COMMAND "${BUILD_ENV} make")
set(INSTALL_COMMAND "${BUILD_ENV} make install")

ExternalProject_Add(
  extern_deflate
  PREFIX ${CMAKE_BINARY_DIR}
  URL ${CMAKE_SOURCE_DIR}/extra/libdeflate
  BUILD_BYPRODUCTS ${BYPRODUCT}
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND ""
  BUILD_COMMAND sh -c "${BUILD_COMMAND}"
  INSTALL_COMMAND sh -c "${INSTALL_COMMAND}"
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_library(deflate_s STATIC IMPORTED GLOBAL)
add_dependencies(deflate_s extern_deflate)
set_target_properties(deflate_s PROPERTIES IMPORTED_LOCATION ${BYPRODUCT})
