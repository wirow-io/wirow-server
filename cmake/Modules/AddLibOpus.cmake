include(ExternalProject)

set(BYPRODUCT "${CMAKE_BINARY_DIR}/lib/libopus.a")

set(CONFIGURE_ENV "X86_SSE4_1_CFLAGS= X86_AVX_CFLAGS= ")
if(DEFINED CMAKE_C_COMPILER)
  set(CONFIGURE_ENV "CC=${CMAKE_C_COMPILER} ${CONFIGURE_ENV}")
endif()

set(CONFIGURE_COMMAND
    "(test -f ./configure || ./autogen.sh) && \
    ${CONFIGURE_ENV} \
    ./configure --prefix=${CMAKE_BINARY_DIR} \
                --disable-rtcd --disable-extra-programs --disable-doc --disable-shared")

if (CROSS_HOST)
  set(CONFIGURE_COMMAND "${CONFIGURE_COMMAND} --host=${CROSS_HOST}")
endif()


ExternalProject_Add(
  extern_opus
  PREFIX ${CMAKE_BINARY_DIR}
  URL ${CMAKE_SOURCE_DIR}/extra/opus
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND sh -c "${CONFIGURE_COMMAND}"
  BUILD_BYPRODUCTS ${BYPRODUCT}
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_library(opus_s STATIC IMPORTED GLOBAL)
add_dependencies(opus_s extern_opus)
set_target_properties(opus_s PROPERTIES IMPORTED_LOCATION ${BYPRODUCT})
