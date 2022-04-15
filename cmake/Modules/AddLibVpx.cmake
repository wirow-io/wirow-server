set(BYPRODUCT "${CMAKE_BINARY_DIR}/lib/libvpx.a")

set(CONFIGURE_COMMAND
    "./configure --prefix=${CMAKE_BINARY_DIR} --disable-examples --disable-docs \
                        --disable-tools --enable-vp8 --enable-vp9 --enable-vp9-highbitdepth \
                        --as=yasm --disable-sse4_1 --disable-runtime_cpu_detect \
                        --disable-unit-tests")
if(DEFINED CMAKE_C_COMPILER)
  set(BUILD_COMMAND "CC=${CMAKE_C_COMPILER} ${BUILD_COMMAND}")
endif()
if(DEFINED CMAKE_CXX_COMPILER)
  set(BUILD_COMMAND "CXX=${CMAKE_CXX_COMPILER} ${BUILD_COMMAND}")
endif()

ExternalProject_Add(
  extern_vpx
  PREFIX ${CMAKE_BINARY_DIR}
  URL ${CMAKE_SOURCE_DIR}/extra/libvpx
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND sh -c "${CONFIGURE_COMMAND}"
  BUILD_BYPRODUCTS ${BYPRODUCT}
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_library(vpx_s STATIC IMPORTED GLOBAL)
add_dependencies(vpx_s extern_vpx)
set_target_properties(vpx_s PROPERTIES IMPORTED_LOCATION ${BYPRODUCT})
