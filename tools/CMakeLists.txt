cmake_minimum_required(VERSION 3.18.0 FATAL_ERROR)
project(tools LANGUAGES C)

set(_TARGETS)
set(_TOOLS_LIST strliteral)

file(
  COPY .
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
  FILES_MATCHING
  PATTERN "*.sh")

find_program(YARN_EXEC yarn REQUIRED NO_CMAKE_FIND_ROOT_PATH)

if(CMAKE_CROSSCOMPILING)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools)
  file(TO_NATIVE_PATH ${CMAKE_COMMAND} native_cmake_command)
  file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
       native_cmake_current_source_dir)

  execute_process(
    COMMAND_ECHO STDOUT
    COMMAND
      sh -c
      "CC= ${native_cmake_command} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} ${native_cmake_current_source_dir}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools)

  add_custom_target(
    host_tools
    COMMAND ${CMAKE_COMMAND} --build . --target host_tools --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools)

  include(${CMAKE_CURRENT_BINARY_DIR}/host_tools/host_tools.cmake)

  foreach(tgt IN ITEMS ${_TOOLS_LIST})
    add_custom_target(
      host${tgt}ninja
      COMMAND echo ''
      DEPENDS host_tools
      BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/host_tools/${tgt})
    add_dependencies(host${tgt} host${tgt}ninja)
  endforeach()

else(!CMAKE_CROSSCOMPILING)
  add_custom_target(host_tools DEPENDS ${_TOOLS_LIST})
endif(CMAKE_CROSSCOMPILING)

add_executable(strliteral strliteral.c)

if(NOT CMAKE_CROSSCOMPILING)
  foreach(tgt IN ITEMS ${_TOOLS_LIST})
    add_executable(host${tgt} ALIAS ${tgt})
  endforeach()
  export(
    TARGETS ${_TOOLS_LIST}
    NAMESPACE host
    FILE host_tools.cmake)
endif()

set(_RESOURCES "data_supported_rtp_capabilities" "data_default_router_options")
foreach(RES IN ITEMS ${_RESOURCES})
  set(_BASE "${CMAKE_BINARY_DIR}/include/${RES}")
  list(APPEND _TARGETS ${_BASE}.inc)
  add_custom_command(
    OUTPUT ${_BASE}.inc
    DEPENDS hoststrliteral ${CMAKE_CURRENT_SOURCE_DIR}/${RES}.js
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/generate_text.sh
            $<TARGET_FILE:hoststrliteral> ${_BASE})
endforeach()

list(
  APPEND
  _TARGETS
  "${CMAKE_BINARY_DIR}/include/data_favicon_ico.inc"
  "${CMAKE_BINARY_DIR}/include/data_robots_txt.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_public_js_gz.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_bundle_js_gz.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_bundle_css_gz.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_fonts_css.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_index_html.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_admin_css_gz.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_admin_js_gz.inc"
  "${CMAKE_BINARY_DIR}/include/data_front_image_bg4_svg.inc"
  "${CMAKE_BINARY_DIR}/include/data_notify_new_member_mp3.inc"
  "${CMAKE_BINARY_DIR}/include/data_notify_recording_started_mp3.inc"
  "${CMAKE_BINARY_DIR}/include/data_notify_chat_message_mp3.inc")

set(_ENVS
    "ENABLE_SENTRY=$<IF:$<BOOL:${SENTRY_FRONT_DSN}>,1,0>"
    "ENABLE_WHITEBOARD=$<IF:$<BOOL:${ENABLE_WHITEBOARD}>,1,0>"
    "ENABLE_RECORDING=$<IF:$<BOOL:${ENABLE_RECORDING}>,1,0>"
    "LIMIT_REGISTERED_USERS=$<IF:$<BOOL:${LIMIT_REGISTERED_USERS}>,${LIMIT_REGISTERED_USERS},0>"
)
set(FRONT_BUILD
    ${CMAKE_COMMAND}
    -E
    env
    ${_ENVS}
    ${YARN_EXEC}
    --non-interactive
    --no-progress
    run
    release)

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/include/data_favicon_ico.inc"
         "${CMAKE_BINARY_DIR}/include/data_robots_txt.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_public_js_gz.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_bundle_js_gz.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_bundle_css_gz.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_fonts_css.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_index_html.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_admin_css_gz.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_admin_js_gz.inc"
         "${CMAKE_BINARY_DIR}/include/data_front_image_bg4_svg.inc"
         "${CMAKE_BINARY_DIR}/include/data_notify_new_member_mp3.inc"
         "${CMAKE_BINARY_DIR}/include/data_notify_recording_started_mp3.inc"
         "${CMAKE_BINARY_DIR}/include/data_notify_chat_message_mp3.inc"
  DEPENDS hoststrliteral
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/front
  USES_TERMINAL
  COMMAND ${YARN_EXEC} --non-interactive --no-progress install --check-files
  COMMAND ${FRONT_BUILD}
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_favicon_ico"
    ${CMAKE_SOURCE_DIR}/tools/favicon.ico >
    "${CMAKE_BINARY_DIR}/include/data_favicon_ico.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_robots_txt"
    ${CMAKE_SOURCE_DIR}/tools/robots.txt >
    "${CMAKE_BINARY_DIR}/include/data_robots_txt.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_public_js_gz"
    ${CMAKE_SOURCE_DIR}/front/release/public.js.gz >
    "${CMAKE_BINARY_DIR}/include/data_front_public_js_gz.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_bundle_js_gz"
    ${CMAKE_SOURCE_DIR}/front/release/bundle.js.gz >
    "${CMAKE_BINARY_DIR}/include/data_front_bundle_js_gz.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_bundle_css_gz"
    ${CMAKE_SOURCE_DIR}/front/release/bundle.css.gz >
    "${CMAKE_BINARY_DIR}/include/data_front_bundle_css_gz.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_fonts_css"
    ${CMAKE_SOURCE_DIR}/front/release/fonts.css >
    "${CMAKE_BINARY_DIR}/include/data_front_fonts_css.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_index_html"
    ${CMAKE_SOURCE_DIR}/front/release/index.html >
    "${CMAKE_BINARY_DIR}/include/data_front_index_html.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_admin_css_gz"
    ${CMAKE_SOURCE_DIR}/front/release/admin.css.gz >
    "${CMAKE_BINARY_DIR}/include/data_front_admin_css_gz.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_admin_js_gz"
    ${CMAKE_SOURCE_DIR}/front/release/admin.js.gz >
    "${CMAKE_BINARY_DIR}/include/data_front_admin_js_gz.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_front_image_bg4_svg"
    ${CMAKE_SOURCE_DIR}/front/images/bg4.svg >
    "${CMAKE_BINARY_DIR}/include/data_front_image_bg4_svg.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_notify_new_member_mp3"
    ${CMAKE_SOURCE_DIR}/front/audio/notify_new_member.mp3 >
    "${CMAKE_BINARY_DIR}/include/data_notify_new_member_mp3.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_notify_recording_started_mp3"
    ${CMAKE_SOURCE_DIR}/front/audio/notify_recording_started.mp3 >
    "${CMAKE_BINARY_DIR}/include/data_notify_recording_started_mp3.inc"
  COMMAND
    $<TARGET_FILE:hoststrliteral> -i "data_notify_chat_message_mp3"
    ${CMAKE_SOURCE_DIR}/front/audio/notify_chat_message.mp3 >
    "${CMAKE_BINARY_DIR}/include/data_notify_chat_message_mp3.inc")

if(ENABLE_WHITEBOARD)
  set(WBPRODUCT
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_index.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_main_js_gz.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_main_css_gz.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_image_js_gz.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_vendor_js_gz.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_virgil_woff2.inc"
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_cascadia_woff2.inc"
  )
  list(APPEND _TARGETS ${WBPRODUCT})

  include(AddExcalidraw)
  ExternalProject_Add(
    front_whiteboard
    PREFIX ${CMAKE_BINARY_DIR}
    URL ${CMAKE_SOURCE_DIR}/front-whiteboard
    DEPENDS hoststrliteral extern_excalidraw
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${YARN_EXEC} --non-interactive --no-progress remove
                  @excalidraw/excalidraw
    COMMAND ${YARN_EXEC} --non-interactive --no-progress add "file:${LIBEXCALIDRAW}"
    COMMAND ${YARN_EXEC} --non-interactive --no-progress install --check-files
    COMMAND ${YARN_EXEC} --non-interactive --no-progress run build
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i "data_front_whiteboard_index"
      build/index.html >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_index.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i "data_front_whiteboard_main_js_gz"
      build/main.js.gz >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_main_js_gz.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i "data_front_whiteboard_main_css_gz"
      build/main.css.gz >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_main_css_gz.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i
      "data_front_whiteboard_excalidraw_image_js_gz"
      build/excalidraw-assets/image.js.gz >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_image_js_gz.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i
      "data_front_whiteboard_excalidraw_vendor_js_gz"
      build/excalidraw-assets/vendor.js.gz >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_vendor_js_gz.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i
      "data_front_whiteboard_excalidraw_virgil_woff2"
      build/excalidraw-assets/Virgil.woff2 >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_virgil_woff2.inc"
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i
      "data_front_whiteboard_excalidraw_cascadia_woff2"
      build/excalidraw-assets/Cascadia.woff2 >
      "${CMAKE_BINARY_DIR}/include/data_front_whiteboard_excalidraw_cascadia_woff2.inc"
    INSTALL_COMMAND ""
    LOG_UPDATE OFF
    LOG_BUILD OFF
    LOG_CONFIGURE OFF
    LOG_INSTALL OFF)

  add_custom_command(OUTPUT ${WBPRODUCT} DEPENDS front_whiteboard)

endif(ENABLE_WHITEBOARD)

# Front fonts
foreach(FID RANGE 1 12)
  list(APPEND _TARGETS
       "${CMAKE_BINARY_DIR}/include/data_front_${FID}_woff2.inc")
  add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/include/data_front_${FID}_woff2.inc"
    DEPENDS hoststrliteral
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/front
    COMMAND
      $<TARGET_FILE:hoststrliteral> -i "data_front_${FID}_woff2"
      ${CMAKE_SOURCE_DIR}/front/fonts/${FID}.woff2 >
      "${CMAKE_BINARY_DIR}/include/data_front_${FID}_woff2.inc")
endforeach()

add_custom_target(generated DEPENDS ${_TARGETS})
