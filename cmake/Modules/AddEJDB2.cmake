include(ExternalProject)

set(EJDB2_SOURCE_DIR ${CMAKE_SOURCE_DIR}/extra/ejdb)
set(BYPRODUCT
    "${CMAKE_BINARY_DIR}/lib/libejdb2-2.a"
    "${CMAKE_BINARY_DIR}/lib/libiwnet-1.a"
    "${CMAKE_BINARY_DIR}/lib/libiowow-1.a")

set(CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR} -DASAN=${ASAN}
    -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DENABLE_HTTP=ON)

foreach(extra CMAKE_C_COMPILER CMAKE_TOOLCHAIN_FILE TEST_TOOL_CMD)
  if(DEFINED ${extra})
    list(APPEND CMAKE_ARGS "-D${extra}=${${extra}}")
  endif()
endforeach()
message("EJDB2 CMAKE_ARGS: ${CMAKE_ARGS}")

ExternalProject_Add(
  extern_ejdb2
  PREFIX ${CMAKE_BINARY_DIR}
  SOURCE_DIR ${EJDB2_SOURCE_DIR}
  BUILD_IN_SOURCE OFF
  UPDATE_COMMAND ""
  UPDATE_DISCONNECTED 1
  CMAKE_ARGS ${CMAKE_ARGS}
  BUILD_BYPRODUCTS ${BYPRODUCT})

add_library(iwnet_s STATIC IMPORTED GLOBAL)
set_target_properties(
  iwnet_s PROPERTIES IMPORTED_LOCATION
                     ${CMAKE_BINARY_DIR}/lib/libiwnet-1.a)

add_library(iowow_s STATIC IMPORTED GLOBAL)
set_target_properties(
  iowow_s PROPERTIES IMPORTED_LOCATION
                     ${CMAKE_BINARY_DIR}/lib/libiowow-1.a)

add_library(ejdb2_s STATIC IMPORTED GLOBAL)
set_target_properties(
  ejdb2_s PROPERTIES IMPORTED_LOCATION
                     ${CMAKE_BINARY_DIR}/lib/libejdb2-2.a)

add_dependencies(ejdb2_s extern_ejdb2)
add_dependencies(iwnet_s extern_ejdb2)
add_dependencies(iowow_s extern_ejdb2)

#set_target_properties(iwnet_s PROPERTIES INTERFACE_LINK_LIBRARIES "iowow_s")
#set_target_properties(ejdb2_s PROPERTIES INTERFACE_LINK_LIBRARIES "iwnet_s")
#set_target_properties(iowow_s PROPERTIES INTERFACE_LINK_LIBRARIES "m")
