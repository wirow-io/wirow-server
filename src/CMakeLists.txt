if(NOT CMAKE_BUILD_TYPE)
  message(
    FATAL_ERROR
      "Please specify the build type -DCMAKE_BUILD_TYPE=Debug|Release|RelWithDebInfo"
  )
endif()

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(InstallRequiredSystemLibraries)

find_package(Threads REQUIRED CMAKE_THREAD_PREFER_PTHREAD)

include(AddEJDB2)
include(AddCURL)
include(AddMediasoup)
include(AddLibDeflate)
if(ENABLE_RECORDING)
  include(AddLibVpx)
  include(AddLibOpus)
  include(AddFFmpeg)
endif()
if(SENTRY_DSN)
  include(AddSentry)
endif()

set(MODULES
    utils
    wrc
    rct
    res
    adm
    lic
    acme
    upd)

set(PROJECT_LLIBRARIES mediasoup_s curl_s ejdb2_s iwnet_s iowow_s)

if(ENABLE_RECORDING)
  list(APPEND MODULES rec)
  list(
    APPEND
    PROJECT_LLIBRARIES
    ffmpegcl_s
    avfilter_s
    avformat_s
    avcodec_s
    swresample_s
    swscale_s
    avutil_s
    vpx_s
    opus_s)
endif()

list(APPEND PROJECT_LLIBRARIES deflate_s Threads::Threads)

if(ENABLE_WHITEBOARD)
  list(APPEND MODULES wb)
endif()

if(SENTRY_DSN)
  list(APPEND PROJECT_LLIBRARIES sentry_s breakpad_s)
endif()

list(APPEND PROJECT_LLIBRARIES dl m)

set(ALL_SRC)
set(ALL_HDRS)
set(PROJECT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
list(APPEND PROJECT_INCLUDE_DIRS ${PROJECT_GENERATED_DIR}
     ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/include
     ${CMAKE_BINARY_DIR}/include/ejdb2)

add_definitions(-D_GNU_SOURCE)
add_definitions(-D_LARGEFILE_SOURCE)
add_definitions(-D_FILE_OFFSET_BITS=64)

file(GLOB ROOT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
list(APPEND ALL_SRC ${ROOT_SRC})

foreach(MODULE IN LISTS MODULES)
  file(GLOB MODULE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/*.c
       ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/*.cpp)
  file(GLOB MODULE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/*.h
       ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/*.hpp)
  list(APPEND ALL_SRC ${MODULE_SRC})
  list(APPEND ALL_HDRS ${MODULE_HDRS})
  list(APPEND PROJECT_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${MODULE})
endforeach(MODULE)

set(CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} \
    -std=gnu11 -fsigned-char -fPIC \
    -Wfatal-errors -Wall -Wextra \
    -Wno-sign-compare -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-function \
    -Wno-missing-field-initializers -Wno-missing-braces \
    -Wno-implicit-fallthrough")

if(ASAN)
  set(CMAKE_C_ASAN "-fsanitize=address -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_C_ASAN}")
elseif (UBSAN)
  set(CMAKE_C_ASAN "-fsanitize=undefined -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_C_ASAN}")
endif()

set(CMAKE_C_FLAGS_DEBUG
    "${CMAKE_C_ASAN} -O0 -g -ggdb -Werror -DDEBUG -D_DEBUG -UNDEBUG -Wno-unused-variable"
)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_ASAN} -O3 -DNDEBUG -g")
set(CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELEASE})
set(CMAKE_C_FLAGS_RELEASEWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

if(CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
endif()

list(REMOVE_DUPLICATES PROJECT_LLIBRARIES)
list(REMOVE_DUPLICATES PROJECT_INCLUDE_DIRS)

include_directories(${PROJECT_INCLUDE_DIRS})

foreach(MODULE IN LISTS MODULES)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${MODULE})
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/tools/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/tools)
  endif()
  if(BUILD_TESTS AND EXISTS
                     ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/tests/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/tests)
  endif()
  if(BUILD_EXAMPLES
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/examples/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/examples)
  endif()
  if(BUILD_BENCHMARKS
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/benchmark/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/benchmark)
  endif()
endforeach(MODULE)

# Static library
if(BUILD_TESTS)
  add_library(greenrooms_s STATIC ${ALL_SRC})
  add_dependencies(greenrooms_s generated license)
  target_link_libraries(greenrooms_s PUBLIC ${PROJECT_LLIBRARIES})
  set_target_properties(
    greenrooms_s
    PROPERTIES VERSION ${PROJECT_VERSION}
               COMPILE_FLAGS "-DIW_STATIC"
               OUTPUT_NAME wirow-${PROJECT_VERSION_MAJOR})
endif()

# Executables
add_executable(grs_g ${ALL_SRC})
add_dependencies(grs_g generated license)
target_link_libraries(grs_g PUBLIC ${PROJECT_LLIBRARIES})
set_target_properties(grs_g PROPERTIES COMPILE_FLAGS "-DIW_EXEC" OUTPUT_NAME
                                                                 wirow_g)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_custom_command(
    TARGET grs_g
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --compress-debug-sections=zlib
            "$<TARGET_FILE:grs_g>")
endif()

add_custom_target(
  grs ALL
  ${CMAKE_STRIP} -o ${CMAKE_BINARY_DIR}/src/wirow $<TARGET_FILE:grs_g>
  BYPRODUCTS ${CMAKE_BINARY_DIR}/src/wirow)
add_dependencies(grs grs_g)

install(FILES $<TARGET_FILE_DIR:grs_g>/wirow
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_SOURCE_DIR}/docs/wirow.pdf
              ${CMAKE_SOURCE_DIR}/docs/wirow_ru.pdf
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(BUILD_DISTRIB)
  add_custom_target(
    distr ALL
    zip -j wirow ${CMAKE_BINARY_DIR}/src/wirow
    ${CMAKE_SOURCE_DIR}/docs/wirow.pdf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_dependencies(distr grs grs_g license_complete)
endif(BUILD_DISTRIB)

message("")
message("CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message("${PROJECT_NAME} LINK LIBS: ${PROJECT_LLIBRARIES}")
message("${PROJECT_NAME} ASAN: ${ASAN}")
message("\n${PROJECT_NAME} INCLUDE DIRS: ${PROJECT_INCLUDE_DIRS}")
message("\n${PROJECT_NAME} SOURCES: ${ALL_SRC}")
message("\n${PROJECT_NAME} CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message("${PROJECT_NAME} BUILD_TESTS: ${BUILD_TESTS}")
message("${PROJECT_NAME} BUILD_DISTRIB: ${BUILD_DISTRIB}")
