/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include "recording_postproc.h"
#include "wrc/wrc.h"
#include "grh_ws.h"
#include "utils/files.h"

#include <iowow/iwpool.h>
#include <iowow/iwp.h>
#include <iowow/iwconv.h>
#include <iowow/iwarr.h>
#include <iwnet/iwn_proc.h>

#include <libavformat/avformat.h>

#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <regex.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

extern struct gr_env g_env;

#define FLG_VIDEO      0x01
#define FLG_AUDIO      0x02
#define FLG_FIX_NEEDED 0x04
#define FLG_ACTIVE     0x08

#define FLG_POINT_START 0x01

struct _ctx;

struct _box {
  double x, y, w, h;
};

struct _m {
  int64_t duration;
  int64_t start_time;
  int64_t user_id;
  int     id;
  int     nchannels; // Audio channels
  int     nstream;   // Stream number
  const char *container;
  const char *fname;
  char       *path;
  struct _ctx *ctx;
  struct _m   *next;
  struct _box  box;
  uint8_t      flags;
};

struct _ctx {
  int64_t task_id;
  int64_t room_ctime;
  JBL     room;
  const char *basedir;
  const char *output_fname;
  IWPOOL     *pool;
  struct _m  *media;
  struct _box size;
};

static iwrc _m_process(struct _ctx *ctx, struct _m *m) {
  // Validate media file and get recording durarion and number of audio channels
  iwrc rc = 0;
  int rci = 0;
  AVFormatContext *fctx = 0;
  m->nstream = -1;

  if ((rci = avformat_open_input(&fctx, m->path, 0, 0))) {
    iwlog_warn2(av_err2str(rci));
    rc = GR_ERROR_MEDIA_PROCESSING;
    goto finish;
  }
  if (avformat_find_stream_info(fctx, 0)) {
    m->flags |= FLG_FIX_NEEDED;
  }
  m->duration = (int64_t) (fctx->duration * 1000 / AV_TIME_BASE);
  if (fctx->start_time && (fctx->start_time / AV_TIME_BASE) > 100000) {
    int64_t st = fctx->start_time * 1000 / AV_TIME_BASE;
    if (st >= ctx->room_ctime) {
      m->start_time = st;
    }
    m->duration = (int64_t) (fctx->duration * 1000 / AV_TIME_BASE) - m->start_time;
  }

  if (m->duration < 1 || m->duration > 40000000LL) { //~ 6hours
    iwlog_warn("FFR | Invalid duration: %" PRId64 " media: %s", m->duration, m->fname);
    m->flags |= FLG_FIX_NEEDED;
  }
  if (m->flags & FLG_AUDIO) {
    for (int i = 0; i < fctx->nb_streams; ++i) {
      if (fctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        m->nstream = i;
        m->nchannels = fctx->streams[i]->codecpar->ch_layout.nb_channels;
        break;
      }
    }
    if (m->nchannels == 0) {
      m->flags |= FLG_FIX_NEEDED;
    }
  }
  if (m->flags & FLG_VIDEO) {
    for (int i = 0; i < fctx->nb_streams; ++i) {
      if (fctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        m->nstream = i;
        break;
      }
    }
  }
  if (m->nstream == -1) {
    iwlog_warn("Failed to find main media stream in %s", m->path);
    rc = GR_ERROR_MEDIA_PROCESSING;
    goto finish;
  }

finish:
  if (fctx) {
    avformat_close_input(&fctx);
  }
  return rc;
}

static struct _m* _m_create(regex_t *rx, struct _ctx *ctx, const char *fname) {
  iwrc rc = 0;
  struct _m *m = 0;
  // <start ts>-<user id>-<a|v>.ex
  // (\\d+)-(\\d+)-(a|v)\\.(webm|flv|mkv|avi)
  const size_t pmlen = 5;
  const size_t fnlen = strlen(fname);
  regmatch_t pm[pmlen];

  if (regexec(rx, fname, pmlen, pm, 0) != 0) {
    return 0;
  }

  m = iwpool_calloc(sizeof(*m), ctx->pool);
  if (!m) {
    return 0;
  }
  for (int i = 1; i < pmlen; ++i) {
    if (pm[i].rm_so == -1) {
      break;
    }
    char buf[fnlen + 1];
    memcpy(buf, fname, fnlen);
    buf[pm[i].rm_eo] = '\0';
    const char *group = buf + pm[i].rm_so;
    switch (i) {
      case 1:
        m->start_time = ctx->room_ctime + iwatoi(group);
        break;
      case 2:
        m->user_id = iwatoi(group);
        break;
      case 3:
        if (strcmp("a", group) == 0) {
          m->flags |= FLG_AUDIO;
        } else if (strcmp("v", group) == 0) {
          m->flags |= FLG_VIDEO;
        }
        break;
      case 4:
        m->container = iwpool_strdup(ctx->pool, group, &rc);
        RCGO(rc, error);
        break;
    }
  }
  RCB(error, m->path = iwpool_printf(ctx->pool, "%s/%s", ctx->basedir, fname));
  m->fname = m->path + strlen(ctx->basedir) + 1;
  return m;

error:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return 0;
}

static iwrc _m_collect(struct _ctx *ctx) {
  iwrc rc = 0;
  DIR *d = 0;
  regex_t rx = { 0 };
  struct dirent *dir;

  d = opendir(ctx->basedir);
  if (!d) {
    rc = iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
    goto finish;
  }
  const char *rxval = "^([0-9]+)-([0-9]+)-(a|v)\\.(webm|flv|mkv|avi)$";
  if (regcomp(&rx, rxval, REG_EXTENDED)) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }
  struct _m *pm = 0;
  while ((dir = readdir(d))) {
    struct _m *m;
    if ((m = _m_create(&rx, ctx, dir->d_name))) {
      if (_m_process(ctx, m) == 0) {
        if (pm) {
          pm->next = m;
        } else if (!ctx->media) {
          ctx->media = m;
        }
        pm = m;
      }
    }
  }

finish:
  regfree(&rx);
  if (d) {
    closedir(d);
  }
  return rc;
}

static void _ffmfix_on_output(const struct iwn_proc_ctx *ctx, const char *chunk, size_t len) {
  iwlog_info("FFR | [fix:%d] %.*s", ctx->pid, (int) len, chunk);
}

static void _ffmfix_on_exit(const struct iwn_proc_ctx *ctx) {
  int code = WIFEXITED(ctx->wstatus) ? WEXITSTATUS(ctx->wstatus) : -1;
  iwlog_info("FFR | [fix] exited, code: %d", code);
}

/// Trying to fix corrupted media files: genpts, write headers
static iwrc _m_fix(struct _ctx *ctx) {
  iwrc rc = 0;
  IWXSTR *xargs = 0;
  int c = 0;
  IWPOOL *pool = 0;

  for (struct _m *m = ctx->media; m; m = m->next) {
    if (m->flags & FLG_FIX_NEEDED) {
      c = 1;
      break;
    }
  }
  if (!c) {
    return 0;
  }
  RCB(finish, xargs = iwxstr_new());
  RCB(finish, pool = iwpool_create_empty());

  if (g_env.recording.ffmpeg == g_env.program_file) {
    RCC(rc, finish, iwxstr_cat2(xargs, "f"));
  }
  RCC(rc, finish, iwxstr_cat2(
        xargs,
        "\1-y\1-fflags\1+genpts"
        "\1-err_detect\1ignore_err"));

  if (g_env.recording.verbose) {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1debug"));
  } else {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1fatal"));
  }

  for (struct _m *m = ctx->media; m; m = m->next) {
    if (!(m->flags & FLG_FIX_NEEDED)) {
      continue;
    }
    RCC(rc, finish, iwxstr_printf(xargs, "\1-i\1%s", m->path));
  }

  c = 0;
  for (struct _m *m = ctx->media; m; m = m->next) {
    if (!(m->flags & FLG_FIX_NEEDED)) {
      continue;
    }
    char *path = strrchr(m->path, '/');
    if (!path) {
      continue;
    }
    if (m->flags & FLG_AUDIO) {
      char *p = strchr(path, 'a');
      if (!p) {
        continue;
      }
      *p = 'b';
    } else if (m->flags & FLG_VIDEO) {
      char *p = strchr(path, 'v');
      if (!p) {
        continue;
      }
      *p = 'w';
    }
    RCC(rc, finish, iwxstr_printf(xargs, "\1-map\1%d\1-c\1copy\1%s", c++, m->path));
  }

  struct iwn_proc_spec pspec = (struct iwn_proc_spec) {
    .poller = g_env.poller,
    .path = g_env.recording.ffmpeg,
    .on_stdout = _ffmfix_on_output,
    .on_stderr = _ffmfix_on_output,
    .on_exit = _ffmfix_on_exit
  };

  RCB(finish, pspec.args = iwpool_split_string(pool, iwxstr_ptr(xargs), "\1", true));

  int pid;
  RCC(rc, finish, iwn_proc_spawn(&pspec, &pid));
  RCC(rc, finish, iwn_proc_wait(pid));

  // Now filter out unrecoverable media items
  for (struct _m *m = ctx->media, *p = 0; m; m = m->next) {
    m->flags &= ~FLG_FIX_NEEDED;
    if (!_m_process(ctx, m) || (m->flags & FLG_FIX_NEEDED)) {
      if (p) {
        p->next = m->next;
      } else {
        ctx->media = m->next;
      }
    } else {
      p = m;
    }
  }

finish:
  iwpool_destroy(pool);
  iwxstr_destroy(xargs);
  return rc;
}

struct _pt {
  struct _m *media;
  int64_t    time;
  uint8_t    flags;
};

struct _st {
  int      id;
  IWULIST *mlist; // List of (struct _media*)
  int64_t  start_time;
  int64_t  end_time;
};

struct _pctx {         // Postprocessing context
  IWULIST     *stlist; // Steps list (struct _st)
  IWXSTR      *spec;   // FFMpeg filter spec (stdin)
  const char **args;   // FFMpeg arguments
  struct _ctx *ctx;
};

static int _postproc_sort_pt(const void *o1, const void *o2, void *op) {
  const struct _pt *p1 = o2;
  const struct _pt *p2 = o1;
  return p1->time < p2->time ? -1 : p1->time > p2->time ? 1 : 0;
}

static iwrc _postproc_create_args(struct _pctx *pctx) {
  iwrc rc = 0;
  IWXSTR *xargs = iwxstr_new();
  if (!xargs) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  struct _ctx *ctx = pctx->ctx;
  if (g_env.recording.ffmpeg == g_env.program_file) {
    RCC(rc, finish, iwxstr_cat2(xargs, "f"));
  }
  RCC(rc, finish, iwxstr_cat2(xargs, "\1-y"));
  if (g_env.recording.verbose) {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1debug"));
  } else {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1fatal"));
  }
  for (struct _m *m = ctx->media; m; m = m->next) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1-i\1%s/%s", ctx->basedir, m->fname));
  }
  RCC(rc, finish, iwxstr_cat2(xargs, "\1-filter_complex_script\1pipe:0"));
  RCC(rc, finish, iwxstr_cat2(xargs,
                              "\1-c:v\1libvpx-vp9\1-cpu-used\1" "5" "\1-deadline\1realtime" "\1-auto-alt-ref\1" "0"
                              "\1-c:a\1libopus\1-application\1voip"));
  RCC(rc, finish, iwxstr_printf(xargs, "\1-map\1[vid]\1-map\1[aud]\1%s/%s",
                                ctx->basedir, ctx->output_fname));
  RCB(finish, pctx->args = iwpool_split_string(pctx->ctx->pool, iwxstr_ptr(xargs), "\1", true));

finish:
  iwxstr_destroy(xargs);
  return rc;
}

static bool _m_has(IWULIST *mlist, int64_t user_id, int flags) {
  for (int i = 0; i < mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(mlist, i);
    if ((user_id < 0 || m->user_id == user_id) && (m->flags & flags)) {
      return true;
    }
  }
  return false;
}

static void _m_apply_layout(struct _m *m, struct _ctx *ctx, int mtotal, int idx) {
  // Used simple grid layout
  int yy = 1, xx, c = 0;
  if (mtotal < 4) {
    xx = mtotal;
  } else {
    xx = (int) ceil(sqrt(mtotal));
    yy = xx;
  }
  for (int y = 0; y < yy; ++y) {
    for (int x = 0; x < xx; ++x) {
      if (c++ == idx) {
        m->box = (struct _box) {
          .w = ctx->size.w / xx,
          .h = ctx->size.h / yy,
          .x = ctx->size.w / xx * x,
          .y = ctx->size.h / yy * y
        };
        return;
      }
    }
  }
}

static iwrc _postproc_generate_step(struct _pctx *pctx, struct _st *st) {
  iwrc rc = 0;
  int n = 0, n2 = 0;
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  for (int i = 0; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    m->flags &= ~FLG_ACTIVE;
    if ((m->flags & FLG_VIDEO) || _m_has(st->mlist, m->user_id, FLG_VIDEO) == false) {
      m->flags |= FLG_ACTIVE;
      n++;
    }
  }
  for (int i = 0; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    if (m->flags & FLG_ACTIVE) {
      _m_apply_layout(m, pctx->ctx, n, n2++);
    }
  }
  IWXSTR *fspec = pctx->spec;
  struct _box size = pctx->ctx->size;
  double duration = (double) (st->end_time - st->start_time) / 1000.0;
  RCC(rc, finish,
      iwxstr_printf(
        fspec,
        "color=s=%.0fx%.0f,trim=0:%.3f[s%d_bg];",
        size.w, size.h, duration, st->id));

  for (int i = 0; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    if (!(m->flags & FLG_ACTIVE)) {
      continue;
    }
    if (m->flags & FLG_VIDEO) {
      double trim_s = (double) (st->start_time - m->start_time) / 1000.0;
      double trim_e = duration + trim_s;
      RCC(rc, finish, iwxstr_printf(
            fspec,
            "[%d:v]trim=%.3f:%.3f,setpts=PTS-STARTPTS,",
            m->id, trim_s, trim_e));
    } else {
      // TODO: Insert user ava
      RCC(rc, finish,
          iwxstr_printf(
            fspec,
            "color=s=640x480:c=#003a58,trim=0:%.3f,", duration));
    }
    RCC(rc, finish, iwxstr_printf(
          fspec,
          "scale=w='if(gt(iw/ih,%.3f),%.3f,-2)':h='if(gt(iw/ih,%.3f),-2,%.3f)':eval=init[s%d_%d_v];",
          m->box.w / m->box.h, m->box.w,
          m->box.w / m->box.h, m->box.h,
          st->id, m->id));
  }

  for (int i = 0, j = 0, prev; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    if (!(m->flags & FLG_ACTIVE)) {
      continue;
    }
    if (j == 0) {
      RCC(rc, finish, iwxstr_printf(fspec, "[s%d_bg]", st->id));
    } else {
      RCC(rc, finish, iwxstr_printf(fspec, "[s%d_ov_%d]", st->id, prev));
    }
    RCC(rc, finish, iwxstr_printf(
          fspec,
          "[s%d_%d_v]overlay=x='(%.3f-w)/2+%.3f':y='(%.3f-h)/2+%.3f':eval=init%s",
          st->id, m->id,
          m->box.w, m->box.x,
          m->box.h, m->box.y,
          (j == 0 ? ":shortest=1" : "")));
    if (--n == 0) {
      RCC(rc, finish, iwxstr_printf(fspec, "[s%d_out_v];", st->id));
    } else {
      RCC(rc, finish, iwxstr_printf(fspec, "[s%d_ov_%d];", st->id, m->id));
    }
    prev = m->id;
    ++j;
  }

  for (int i = 0; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    if (m->flags & FLG_VIDEO) {
      continue;
    }
    double trim_s = (double) (st->start_time - m->start_time) / 1000.0;
    double trim_e = duration + trim_s;
    RCC(rc, finish, iwxstr_printf(
          fspec,
          "[%d:a]atrim=%.3f:%.3f,asetpts=PTS-STARTPTS[s%d_%d_a];",
          m->id, trim_s, trim_e, st->id, m->id
          ));
  }

  // Mix audio channels
  n = 0, n2 = 0;
  for (int i = 0; i < st->mlist->num; ++i) {
    struct _m *m = *(struct _m**) iwulist_at2(st->mlist, i);
    if (!(m->flags & FLG_AUDIO)) {
      continue;
    }
    if (m->nchannels > 0) {
      n2++;
      n += m->nchannels;
      RCC(rc, finish, iwxstr_printf(fspec, "[s%d_%d_a]", st->id, m->id));
    }
  }
  for (int i = 0; i < n; ++i) {
    if (i > 0) {
      RCC(rc, finish, iwxstr_cat(xstr, "+", 1));
    }
    RCC(rc, finish, iwxstr_printf(xstr, "c%d", i));
  }
  if (n) {
    RCC(rc, finish, iwxstr_printf(
          fspec,
          "amerge=inputs=%d,pan='1c|c0<%s'[s%d_out_a];",
          n2, iwxstr_ptr(xstr), st->id));
  } else {
    RCC(rc, finish, iwxstr_printf(
          fspec,
          "anullsrc=r=48000:cl=mono,atrim=0:%.3f,asetpts=PTS-STARTPTS[s%d_out_a];",
          duration, st->id));
  }

finish:
  iwxstr_destroy(xstr);
  return rc;
}

static void _ffgen_on_output(const struct iwn_proc_ctx *ctx, const char *chunk, size_t len) {
  iwlog_info("FFR | [gen:%d] %.*s", ctx->pid, (int) len, chunk);
}

static void _ffgen_on_err(const struct iwn_proc_ctx *ctx, const char *chunk, size_t len) {
  iwlog_info("FFR | [gen:%d] %.*s", ctx->pid, (int) len, chunk);
}

static void _ffgen_on_exit(const struct iwn_proc_ctx *ctx) {
  int code = WIFEXITED(ctx->wstatus) ? WEXITSTATUS(ctx->wstatus) : -1;
  iwlog_info("FFR | [gen] exited, code: %d", code);
}

static iwrc _postproc_generate_spec(struct _pctx *pctx) {
  iwrc rc = 0;
  int i = 0;
  for ( ; i < pctx->stlist->num; ++i) {
    struct _st *st = (struct _st*) iwulist_at2(pctx->stlist, i);
    RCC(rc, finish, _postproc_generate_step(pctx, st));
  }
  for (i = 0; i < pctx->stlist->num; ++i) {
    struct _st *st = (struct _st*) iwulist_at2(pctx->stlist, i);
    RCC(rc, finish, iwxstr_printf(pctx->spec, "[s%d_out_v][s%d_out_a]", st->id, st->id));
  }
  RCC(rc, finish, iwxstr_printf(pctx->spec, "concat=n=%d:v=1:a=1[vid][aud]", i));
  RCC(rc, finish, _postproc_create_args(pctx));

finish:
  return rc;
}

static iwrc _postproc_run(struct _ctx *ctx) {
  int pid = -1;
  IWULIST ptlist; // Pointer list (struct _pt)
  IWULIST stlist; // Steps list (struct _st)
  IWULIST mlist;  // Current media list
  struct _pctx pctx = {
    .stlist = &stlist,
    .ctx    = ctx,
    .spec   = iwxstr_new(),
  };

  iwrc rc = RCR(iwulist_init(&ptlist, 64, sizeof(struct _pt)));
  RCC(rc, finish, iwulist_init(&stlist, 64, sizeof(struct _st)));
  RCC(rc, finish, iwulist_init(&mlist, 8, sizeof(struct _m*)));
  RCB(finish, pctx.spec);

  int i = 0;
  for (struct _m *m = ctx->media; m; m = m->next) {
    RCC(rc, finish, iwulist_push(&ptlist, &(struct _pt) {
      .media = m,
      .time = m->start_time,
      .flags = FLG_POINT_START
    }));
    RCC(rc, finish, iwulist_push(&ptlist, &(struct _pt) {
      .media = m,
      .time = m->start_time + m->duration
    }));
  }
  iwulist_sort(&ptlist, _postproc_sort_pt, 0);

  {
    // Now reorder global media list
    struct _m *pm = 0;
    for (int i = (int) ptlist.num - 1; i >= 0; --i) {
      struct _pt *pt = (struct _pt*) iwulist_at2(&ptlist, i);
      if (!(pt->flags & FLG_POINT_START)) {
        continue;
      }
      pt->media->next = 0;
      if (!pm) {
        ctx->media = pt->media;
        ctx->media->id = 0;
      } else {
        pm->next = pt->media;
        pt->media->id = pm->id + 1;
      }
      pm = pt->media;
    }
  }

  int64_t ptime = -1;
  for (i = 0; ptlist.num; iwulist_pop(&ptlist)) {
    struct _pt *pt = iwulist_at2(&ptlist, ptlist.num - 1);
    if (ptime != -1 && mlist.num && (ptlist.num == 1 || pt->time != ptime)) {
      struct _st st = {
        .id         = i++,
        .start_time = ptime,
        .end_time   = pt->time,
        .mlist      = iwulist_clone(&mlist) // TODO: sorting of files
      };
      RCB(finish, st.mlist);
      iwulist_push(&stlist, &st);
    }
    if (pt->flags & FLG_POINT_START) {
      RCC(rc, finish, iwulist_push(&mlist, &pt->media));
    } else {
      for (int j = 0; j < mlist.num; ++j) {
        struct _m *m = *(struct _m**) iwulist_at2(&mlist, j);
        if (m == pt->media) {
          iwulist_remove(&mlist, j);
          break;
        }
      }
    }
    ptime = pt->time;
  }
  if (stlist.num == 0) {
    goto finish;
  }
  if (g_env.recording.verbose) {
    iwlog_info2("FFR | Sequences:");
    for (int i = 0; i < stlist.num; ++i) {
      struct _st *st = iwulist_at2(&stlist, i);
      iwlog_info("FFR | seq id=%d c=%zd s=%" PRId64 " e=%" PRId64 " d=%" PRId64,
                 i, st->mlist->num, st->start_time, st->end_time, st->end_time - st->start_time);
    }
  }
  RCC(rc, finish, _postproc_generate_spec(&pctx));

  if (g_env.recording.verbose) {
    iwlog_info("FFR | Filter %s", iwxstr_ptr(pctx.spec));
  }

  struct iwn_proc_spec pspec = (struct iwn_proc_spec) {
    .poller = g_env.poller,
    .path = g_env.recording.ffmpeg,
    .args = pctx.args,
    .on_stdout = _ffgen_on_output,
    .on_stderr = _ffgen_on_err,
    .on_exit = _ffgen_on_exit,
    .write_stdin = true
  };

  RCC(rc, finish, iwn_proc_spawn(&pspec, &pid));
  RCC(rc, finish, iwn_proc_stdin_write(pid, iwxstr_ptr(pctx.spec), iwxstr_size(pctx.spec), true));
  RCC(rc, finish, iwn_proc_wait(pid));

finish:
  iwulist_destroy_keep(&ptlist);
  iwulist_destroy_keep(&mlist);
  for (int i = 0; i < stlist.num; ++i) {
    struct _st *st = iwulist_at2(&stlist, i);
    iwulist_destroy(&st->mlist);
  }
  iwulist_destroy_keep(&stlist);
  iwxstr_destroy(pctx.spec);
  if (rc && pid > -1) {
    iwn_proc_kill(pid, SIGINT);
  }
  return rc;
}

static iwrc _postproc(struct _ctx *ctx) {
  iwrc rc = RCR(_m_collect(ctx));
  RCC(rc, finish, _m_fix(ctx));
  if (!ctx->media) {
    // No valid media to process
    rc = GR_ERROR_MEDIA_PROCESSING;
    iwlog_error2("No valid media to process");
    goto finish;
  }
  rc = _postproc_run(ctx);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}

int recording_postproc(int64_t task_id, JBL task) {
  iwlog_info("FFR | Processing room recording task");
  iwrc rc = 0;
  int ret = 1;
  JQL q = 0;
  EJDB_LIST list = 0;
  JBL jbl = 0;
  IWXSTR *xstr = 0;
  const char *cid = 0, *uuid = 0, *name = 0, *dirname;
  int64_t ctime;
  char cid_dir[FILES_UUID_DIR_BUFSZ];

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    rc = iwrc_set_errno(rc, IW_ERROR_ALLOC);
    goto finish;
  }

  RCC(rc, finish, jbl_at(task, "/spec", &jbl));
  RCC(rc, finish, jbl_object_get_str(task, "hook", &cid));
  RCC(rc, finish, jbl_object_get_str(jbl, "uuid", &uuid));
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] or /[cid = :?]"));
  RCC(rc, finish, jql_set_str(q, 0, 0, cid));
  RCC(rc, finish, jql_set_str(q, 0, 1, cid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
  if (!list->first) {
    ret = -1;
    goto finish;
  }

  files_uuid_dir_copy(cid, cid_dir);
  RCB(finish, dirname = iwpool_printf(pool, "%s/%s", g_env.recording.dir, cid_dir));
  RCC(rc, finish, jbl_object_get_i64(list->first->raw, "ctime", &ctime));
  RCC(rc, finish, jbl_object_get_str(list->first->raw, "name", &name));

  struct _ctx ctx = (struct _ctx) {
    .task_id = task_id,
    .output_fname = "output.webm",
    .room = list->first->raw,
    .room_ctime = ctime,
    .basedir = dirname,
    .pool = pool,
    .size = {
      .w = 1280,
      .h = 960
    }
  };

  RCC(rc, finish, _postproc(&ctx));

  // Now attach artifacts to the room entity
  jbl_destroy(&jbl);
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "recf", ctx.output_fname));
  RCC(rc, finish, ejdb_patch_jbl(g_env.db, "rooms", jbl, list->first->id));

  jbl_destroy(&jbl);
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "event", "WRC_EVT_ROOM_RECORDING_PP"));
  RCC(rc, finish, jbl_set_string(jbl, "cid", cid));
  RCC(rc, finish, jbl_set_string(jbl, "name", name));

  // Now notify users and rest of system
  RCB(finish, xstr = iwxstr_new());
  if (!jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0)) {
    grh_ws_send_all_room_participants(list->first->id, iwxstr_ptr(xstr), iwxstr_size(xstr));
  }
  wrc_notify_event_handlers(WRC_EVT_ROOM_RECORDING_PP, 0, jbl);
  jbl = 0; // jbl ownership is transfered to wrc_notify_event_handlers()

finish:
  ejdb_list_destroy(&list);
  jql_destroy(&q);
  iwpool_destroy(pool);
  jbl_destroy(&jbl);
  iwxstr_destroy(xstr);
  if (rc) {
    ret = -1;
  }
  return ret;
}

#ifdef IW_TESTS

iwrc recording_postproc_test(const char *basedir, JBL room) {
  iwrc rc = 0;
  int64_t ctime;
  IWPOOL *pool = iwpool_create_empty();
  RCC(rc, finish, jbl_object_get_i64(room, "ctime", &ctime));
  rc = _postproc(&(struct _ctx) {
    .task_id = 1,
    .output_fname = "output.webm",
    .room = room,
    .room_ctime = ctime,
    .basedir = basedir,
    .pool = pool,
    .size = {
      .w = 1280,
      .h = 960
    }
  });

finish:
  iwpool_destroy(pool);
  return rc;
}

#endif
