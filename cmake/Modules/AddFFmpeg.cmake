include(ExternalProject)

set(LIBAVCODEC "${CMAKE_BINARY_DIR}/lib/libavcodec.a")
set(LIBAVFILTER "${CMAKE_BINARY_DIR}/lib/libavfilter.a")
set(LIBAVFORMAT "${CMAKE_BINARY_DIR}/lib/libavformat.a")
set(LIBAVUTIL "${CMAKE_BINARY_DIR}/lib/libavutil.a")
set(LIBSWSCALE "${CMAKE_BINARY_DIR}/lib/libswscale.a")
set(LIBSWRESAMPLE "${CMAKE_BINARY_DIR}/lib/libswresample.a")
set(LIBFFMPEGCL "${CMAKE_BINARY_DIR}/lib/libffmpegcl.a")

set(PATCH_COMMAND "${CMAKE_CURRENT_LIST_DIR}/AddFFmpeg/patch.sh")

set(CONFIGURE_COMMAND
    "PKG_CONFIG_PATH=${CMAKE_BINARY_DIR}/lib/pkgconfig \
 ./configure \
 --prefix=${CMAKE_BINARY_DIR} \
 --logfile=config.log \
 --disable-all \
 --disable-shared \
 --disable-sse4 \
 --disable-sse3 \
 --disable-runtime-cpudetect \
 --disable-w32threads \
 --disable-os2threads \
 --disable-alsa \
 --disable-appkit \
 --disable-avfoundation \
 --disable-bzlib \
 --disable-coreimage \
 --disable-iconv \
 --disable-lzma \
 --disable-sndio \
 --disable-sdl2 \
 --disable-xlib \
 --disable-zlib \
 --disable-amf \
 --disable-audiotoolbox \
 --disable-cuda-llvm \
 --disable-cuvid \
 --disable-d3d11va \
 --disable-dxva2 \
 --disable-ffnvcodec \
 --disable-nvdec \
 --disable-nvenc \
 --disable-v4l2-m2m \
 --disable-vaapi \
 --disable-vdpau \
 --disable-videotoolbox \
 --disable-programs \
 --enable-pthreads \
 --enable-ffmpegcl \
 --enable-avcodec \
 --enable-avformat \
 --enable-avfilter \
 --enable-swscale \
 --enable-swresample \
 --enable-libvpx \
 --enable-libopus \
 --enable-decoder=libvpx_vp8,libvpx_vp9,libopus \
 --enable-encoder=libvpx_vp8,libvpx_vp9,libopus \
 --enable-parser=vp8,vp9,opus \
 --enable-protocol=file,rtp,tcp,udp,pipe \
 --enable-demuxer=rtp,webm,matroska,opus \
 --enable-muxer=webm,matroska,opus \
 --enable-filter=aformat,format,vflip,hflip,transpose,color,scale,trim,atrim,setpts,asetpts,amerge,anullsrc,pan,null,anull,overlay,concat,copy,acopy,abuffer,buffer,abuffersink,buffersink \
 --enable-bsf=setts \
 --pkg-config-flags=\"--static\" \
 --extra-ldflags=\"-lpthread -L${CMAKE_BINARY_DIR}/lib\" \
 --extra-cflags=\"-I${CMAKE_BINARY_DIR}/include\" \
 --extra-cxxflags=\"-I${CMAKE_BINARY_DIR}/include\"")

 # For debug
 #--disable-optimizations \
 #--optflags=-O0 \

if(DEFINED CMAKE_C_COMPILER)
  set(CONFIGURE_COMMAND "${CONFIGURE_COMMAND} --cc=${CMAKE_C_COMPILER}")
endif()
if(DEFINED CMAKE_CXX_COMPILER)
  set(CONFIGURE_COMMAND "${CONFIGURE_COMMAND} --cxx=${CMAKE_CXX_COMPILER}")
endif()

ExternalProject_Add(
  extern_ffmpeg
  PREFIX ${CMAKE_BINARY_DIR}
  DEPENDS extern_opus extern_vpx
  URL ${CMAKE_SOURCE_DIR}/extra/ffmpeg
  BUILD_IN_SOURCE ON
  PATCH_COMMAND sh -c "${PATCH_COMMAND}"
  CONFIGURE_COMMAND sh -c "${CONFIGURE_COMMAND}"
  BUILD_BYPRODUCTS ${LIBAVCODEC} ${LIBAVFILTER} ${LIBAVFORMAT} ${LIBAVUTIL}
  ${LIBSWSCALE} ${LIBFFMPEGCL} ${LIBSWRESAMPLE}
  LOG_UPDATE OFF
  LOG_BUILD OFF
  LOG_CONFIGURE OFF
  LOG_INSTALL OFF)

add_dependencies(extern_ffmpeg extern_vpx extern_opus)

add_library(avcodec_s STATIC IMPORTED GLOBAL)
add_dependencies(avcodec_s extern_ffmpeg)
set_target_properties(avcodec_s PROPERTIES IMPORTED_LOCATION ${LIBAVCODEC})

add_library(avfilter_s STATIC IMPORTED GLOBAL)
add_dependencies(avfilter_s extern_ffmpeg)
set_target_properties(avfilter_s PROPERTIES IMPORTED_LOCATION ${LIBAVFILTER})

add_library(avformat_s STATIC IMPORTED GLOBAL)
add_dependencies(avformat_s extern_ffmpeg)
set_target_properties(avformat_s PROPERTIES IMPORTED_LOCATION ${LIBAVFORMAT})

add_library(avutil_s STATIC IMPORTED GLOBAL)
add_dependencies(avutil_s extern_ffmpeg)
set_target_properties(avutil_s PROPERTIES IMPORTED_LOCATION ${LIBAVUTIL})

add_library(swscale_s STATIC IMPORTED GLOBAL)
add_dependencies(swscale_s extern_ffmpeg)
set_target_properties(swscale_s PROPERTIES IMPORTED_LOCATION ${LIBSWSCALE})

add_library(swresample_s STATIC IMPORTED GLOBAL)
add_dependencies(swresample_s extern_ffmpeg)
set_target_properties(swresample_s PROPERTIES IMPORTED_LOCATION ${LIBSWRESAMPLE})

add_library(ffmpegcl_s STATIC IMPORTED GLOBAL)
add_dependencies(ffmpegcl_s extern_ffmpeg)
set_target_properties(ffmpegcl_s PROPERTIES IMPORTED_LOCATION ${LIBFFMPEGCL})
