#!/bin/sh

set -e

mkdir -p ./libffmpegcl
rm -rf ./libffmpegcl/*

cp -f ./fftools/ffmpeg.* ./libffmpegcl
cp -f ./fftools/cmdutils.* ./libffmpegcl
cp -f ./fftools/opt_* ./libffmpegcl
cp -f ./fftools/ffmpeg_hw.c ./libffmpegcl
cp -f ./fftools/ffmpeg_opt.c ./libffmpegcl
cp -f ./fftools/ffmpeg_filter.c ./libffmpegcl

cat << EOF > ./libffmpegcl/Makefile
NAME = ffmpegcl
DESC = FFmpeg CLI as a library
FFLIBS = avutil

HEADERS = version.h

OBJS =  ffmpeg.o        \\
        cmdutils.o      \\
        ffmpeg_opt.o    \\
        ffmpeg_filter.o \\
        opt_common.o    \\
        ffmpeg_hw.o

EOF

cat << EOF > ./libffmpegcl/version_major.h
#ifndef FFMPEGCL_VERSION_MAJOR_H
#define FFMPEGCL_VERSION_MAJOR_H

#define LIBFFMPEGCL_VERSION_MAJOR 1

#endif
EOF


cat << EOF > ./libffmpegcl/version.h
#ifndef FFMPEGCL_VERSION_H
#define FFMPEGCL_VERSION_H

#include "libavutil/version.h"

#include "version_major.h"

#define LIBFFMPEGCL_VERSION_MINOR 0
#define LIBFFMPEGCL_VERSION_MICRO 0

#define LIBFFMPEGCL_VERSION_INT AV_VERSION_INT(LIBFFMPEGCL_VERSION_MAJOR, \\
                                               LIBFFMPEGCL_VERSION_MINOR, \\
                                               LIBFFMPEGCL_VERSION_MICRO)
#define LIBFFMPEGCL_VERSION     AV_VERSION(LIBFFMPEGCL_VERSION_MAJOR, \\
                                           LIBFFMPEGCL_VERSION_MINOR, \\
                                           LIBFFMPEGCL_VERSION_MICRO)
#define LIBFFMPEGCL_BUILD       LIBFFMPEGCL_VERSION_INT

#define LIBFFMPEGCL_IDENT "ffmpegcl" AV_STRINGIFY(LIBFFMPEGCL_VERSION)

#endif
EOF

cat << EOF > ./libffmpegcl/libffmpegcl.v
LIBFFMPEGCL_MAJOR {
    global:
        ffmpegcl_*
    local:
        *;
};
EOF

cat << EOF > ./.patch
diff --git a/Makefile b/Makefile
index 7e9d8b08c3..08f179d937 100644
--- a/Makefile
+++ b/Makefile
@@ -28,6 +28,8 @@ FFLIBS-\$(CONFIG_POSTPROC)   += postproc
 FFLIBS-\$(CONFIG_SWRESAMPLE) += swresample
 FFLIBS-\$(CONFIG_SWSCALE)    += swscale

+FFLIBS-\$(CONFIG_FFMPEGCL)   += ffmpegcl
+
 FFLIBS := avutil

 DATA_FILES := \$(wildcard \$(SRC_PATH)/presets/*.ffpreset) \$(SRC_PATH)/doc/ffprobe.xsd
@@ -106,6 +108,8 @@ endef

 \$(foreach D,\$(FFLIBS),\$(eval \$(call DOSUBDIR,lib\$(D))))

+\$(foreach D,\$(FFLIBS2),\$(eval \$(call DOSUBDIR,lib\$(D))))
+
 include \$(SRC_PATH)/fftools/Makefile
 include \$(SRC_PATH)/doc/Makefile
 include \$(SRC_PATH)/doc/examples/Makefile
diff --git a/configure b/configure
index 9c8965852b..70d75fb373 100755
--- a/configure
+++ b/configure
@@ -1953,6 +1953,7 @@ LIBRARY_LIST="
     avcodec
     swresample
     avutil
+    ffmpegcl
 "

 LICENSE_LIST="
@@ -3792,6 +3793,7 @@ swresample_deps="avutil"
 swresample_suggest="libm libsoxr stdatomic"
 swscale_deps="avutil"
 swscale_suggest="libm stdatomic"
+ffmpegcl_deps="avcodec avfilter avformat"

 avcodec_extralibs="pthreads_extralibs iconv_extralibs dxva2_extralibs"
 avfilter_extralibs="pthreads_extralibs"
diff --git a/libffmpegcl/ffmpeg.c b/libffmpegcl/ffmpeg.c
index 46bb014de8..1b72cfe214 100644
--- a/libffmpegcl/ffmpeg.c
+++ b/libffmpegcl/ffmpeg.c
@@ -4951,7 +4951,7 @@ static void log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
 {
 }

-int main(int argc, char **argv)
+int ffmpeg_main(int argc, char **argv)
 {
     int i, ret;
     BenchmarkTimeStamps ti;
diff --git a/libffmpegcl/ffmpeg.h b/libffmpegcl/ffmpeg.h
index 606f2afe0c..3951fe0f96 100644
--- a/libffmpegcl/ffmpeg.h
+++ b/libffmpegcl/ffmpeg.h
@@ -679,4 +679,6 @@ int hw_device_setup_for_filter(FilterGraph *fg);

 int hwaccel_decode_init(AVCodecContext *avctx);

+int ffmpeg_main(int argc, char **argv);
+
 #endif /* FFTOOLS_FFMPEG_H */
diff --git a/libffmpegcl/opt_common.c b/libffmpegcl/opt_common.c
index c303db4d09..55d86eeb19 100644
--- a/libffmpegcl/opt_common.c
+++ b/libffmpegcl/opt_common.c
@@ -51,6 +51,9 @@
 #include "libavdevice/avdevice.h"
 #include "libavdevice/version.h"

+#include "libavfilter/avfilter.h"
+#include "libavfilter/version.h"
+
 #include "libswscale/swscale.h"
 #include "libswscale/version.h"
EOF

if ! patch -R -p1 -s -f --dry-run < ./.patch; then
  patch -p1 < ./.patch
fi
