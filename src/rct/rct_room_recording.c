#include "rct_room_recording.h"
#include "rct_producer_export.h"
#include "rct_room_internal.h"
#include "rct_consumer.h"
#include "gr_task_worker.h"
#include "utils/files.h"

#include <ejdb2/ejdb2.h>
#include <iowow/iwarr.h>
#include <iwnet/iwn_proc.h>
#include <iwnet/iwn_scheduler.h>

#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <signal.h>

extern struct gr_env g_env;

static struct slot {
  struct rct_room *room;
  struct rct_producer_export *export;
  struct slot *next;
  int proc_pid;
} *_slots;

struct proc {
  wrc_resource_t export_id;
  IWPOOL *pool;
};

static pthread_mutex_t _mtx = PTHREAD_MUTEX_INITIALIZER;
static atomic_int _num_workers;

static void _proc_spec_destroy(struct proc *proc) {
  --_num_workers;
  if (proc && proc->pool) {
    iwpool_destroy(proc->pool);
  }
}

static void _proc_on_exit(const struct iwn_proc_ctx *ctx) {
  int code = WIFEXITED(ctx->wstatus) ? WEXITSTATUS(ctx->wstatus) : -1;
  if (g_env.log.verbose) {
    iwlog_info("[ffm-rec:%d] terminated, exit code: %d", ctx->pid, code);
  }
  struct proc *proc = ctx->user_data;
  assert(proc);
  wrc_resource_t export_id = 0;
  pthread_mutex_lock(&_mtx);
  for (struct slot *s = _slots; s; s = s->next) {
    if (s->proc_pid == ctx->pid && proc->export_id == s->export->id) {
      export_id = s->export->id;
      s->proc_pid = 0;
      break;
    }
  }
  pthread_mutex_unlock(&_mtx);
  if (export_id) {
    rct_export_consumer_pause(export_id);
  }
  _proc_spec_destroy(proc);
}

static char* _proc_output_file(struct proc *proc, struct rct_producer_export *export, iwrc *rcp) {
  iwrc rc = 0;
  *rcp = 0;
  uint64_t ts;
  int64_t user_id = 0;
  char *res = 0;
  IWXSTR *xstr = 0;
  char cid_dir[FILES_UUID_DIR_BUFSZ];

  // Find associated room member
  rct_lock();
  int rcp_kind = export->producer->spec->rtp_kind;
  rct_room_t *room = export->producer->transport->router->room;
  if (!room) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    rct_unlock();
    goto finish;
  }
  for (rct_room_member_t *m = room->members; m; m = m->next) {
    for (size_t i = 0, l = iwulist_length(&m->resource_refs); i < l; ++i) {
      struct rct_resource_ref *r = iwulist_at2(&m->resource_refs, i);
      if (r->b == (void*) export->producer) {
        user_id = m->user_id;
        break;
      }
    }
  }
  rct_unlock();

  RCB(finish, xstr = iwxstr_new());
  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  if (room->cid_ts >= ts) {
    rc = IW_ERROR_ASSERTION;
    goto finish;
  }
  uint64_t time = ts - room->cid_ts;

  files_uuid_dir_copy(room->cid, cid_dir);
  RCC(rc, finish, iwxstr_printf(xstr, "%s/%s", g_env.recording.dir, cid_dir));
  RCC(rc, finish, iwp_mkdirs(iwxstr_ptr(xstr)));

  RCC(rc, finish, iwxstr_printf(xstr, "/%" PRIu64 "-%" PRId64 "-%s.%s", time, user_id,
                                (rcp_kind == RTP_KIND_VIDEO ? "v" : "a"),
                                "webm"));

finish:
  if (rc) {
    iwxstr_destroy(xstr);
  } else if (xstr) {
    res = iwxstr_ptr(xstr);
    iwxstr_destroy_keep_ptr(xstr);
  }
  *rcp = rc;
  return res;
}

static void _proc_on_stdout(const struct iwn_proc_ctx *ctx, const char *chunk, size_t len) {
  iwlog_info("[ffm-rec:%d] %.*s", ctx->pid, (int) len, chunk);
}

static void _proc_on_stderr(const struct iwn_proc_ctx *ctx, const char *chunk, size_t len) {
  iwlog_info("[ffm-rec:%d] %.*s", ctx->pid, (int) len, chunk);
}

static void _request_key_frame(void *arg) {
  wrc_resource_t consumer_id = (wrc_resource_t) (uintptr_t) arg;
  iwrc rc = rct_consumer_request_key_frame(consumer_id);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static iwrc _proc_spawn(struct rct_producer_export *export) {
  if (_num_workers > g_env.recording.max_processes) {
    iwlog_warn("Reached the maximum number of recorder processes %d", g_env.recording.max_processes);
    return 0;
  }
  iwrc rc = 0;
  char *output_file;
  struct iwn_proc_spec *pspec;
  IWXSTR *xargs = 0;

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  pspec = iwpool_alloc(sizeof(*pspec) + sizeof(struct proc), pool);
  if (!pspec) {
    iwpool_destroy(pool);
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  struct proc *proc = (void*) &pspec[1];
  *pspec = (struct iwn_proc_spec) {
    .poller = g_env.poller,
    .path = g_env.recording.ffmpeg,
    .user_data = proc,
    .on_stdout = _proc_on_stdout,
    .on_stderr = _proc_on_stderr,
    .on_exit = _proc_on_exit,
    .write_stdin = true
  };

  ++_num_workers; // Will be decremented in `_proc_destroy()`

  proc->pool = pool;
  proc->export_id = export->id;

  RCB(finish, xargs = iwxstr_new());
  if (pspec->path == g_env.program_file) {
    iwxstr_cat2(xargs, "f"); // ffmpeg options selector
  }
  if (g_env.recording.verbose) {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1debug"));
  } else {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1-loglevel\1fatal"));
  }
  RCC(rc, finish, iwxstr_cat2(xargs, "\1-copyts"));
  RCC(rc, finish, iwxstr_cat2(xargs, "\1-use_wallclock_as_timestamps" "\1" "1"));
  RCC(rc, finish, iwxstr_cat2(
        xargs,
        "\1-protocol_whitelist"
        "\1pipe,udp,rtp"
        "\1-f"
        "\1sdp"
        "\1-i"
        "\1pipe:0"
        ));

  if (export->producer->spec->rtp_kind == RTP_KIND_AUDIO) {
    RCC(rc, finish, iwxstr_cat2(
          xargs,
          "\1-map"
          "\1" "0:a:0"
          "\1-c:a"
          "\1copy"
          ));
  } else {
    RCC(rc, finish, iwxstr_cat2(
          xargs,
          "\1-map"
          "\1" "0:v:0"
          "\1-c:v"
          "\1copy"
          ));
  }
  RCC(rc, finish, iwxstr_cat2(xargs, "\1-fflags"));
  if (export->producer->spec->rtp_kind == RTP_KIND_AUDIO) {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1flush_packets+discardcorrupt+nobuffer"));
  } else {
    RCC(rc, finish, iwxstr_cat2(xargs, "\1discardcorrupt+nobuffer"));
  }
  RCC(rc, finish, iwxstr_cat2(
        xargs,
        "\1-flags"
        "\1+low_delay"
        ));

  output_file = _proc_output_file(proc, export, &rc);
  RCGO(rc, finish);
  iwxstr_printf(xargs, "\1-y\1%s", output_file);
  free(output_file);

  RCB(finish, pspec->args = iwpool_split_string(pool, iwxstr_ptr(xargs), "\1", true));
  if (g_env.log.verbose) {
    char *cmd = iwn_proc_command_get(pspec);
    if (cmd) {
      iwlog_info("Spawn: %s", cmd);
    }
    free(cmd);
    iwlog_debug("Stdin:\n%s", export->sdp);
  }

  int pid;
  RCC(rc, finish, iwn_proc_spawn(pspec, &pid));
  RCC(rc, finish, iwn_proc_stdin_write(pid, export->sdp, strlen(export->sdp), true));

  pthread_mutex_lock(&_mtx);
  struct slot *slot = export->hook_user_data;
  if (slot) {
    slot->proc_pid = pid;
  }
  pthread_mutex_unlock(&_mtx);

  if (export->consumer->paused) {
    rct_export_consumer_resume(export->id);
  }
  if (export->producer->spec->rtp_kind == RTP_KIND_VIDEO) {
    RCC(rc, finish, iwn_schedule(&(struct iwn_scheduler_spec) {
      .poller = g_env.poller,
      .timeout_ms = 1000,
      .user_data = (void*) (uintptr_t) export->consumer->id,
      .task_fn = _request_key_frame,
    }));
  }

finish:
  if (rc) {
    if (proc) {
      _proc_spec_destroy(proc);
    } else {
      iwpool_destroy(pool);
    }
  }
  iwxstr_destroy(xargs);
  return rc;
}

static void _export_on_resume(struct rct_producer_export *export) {
  int pid = 0;
  pthread_mutex_lock(&_mtx);
  struct slot *slot = export->hook_user_data;
  if (slot) {
    pid = slot->proc_pid;
  }
  pthread_mutex_unlock(&_mtx);
  if (!pid) {
    _proc_spawn(export);
  }
}

static void _export_on_pause(struct rct_producer_export *export) {
  int pid = 0;
  pthread_mutex_lock(&_mtx);
  struct slot *slot = export->hook_user_data;
  if (slot) {
    pid = slot->proc_pid;
    slot->proc_pid = 0;
  }
  pthread_mutex_unlock(&_mtx);
  if (pid) {
    iwn_proc_kill_ensure(g_env.poller, pid, SIGINT, -30, SIGINT);
  }
}

static void _export_on_close(struct rct_producer_export *export) {
  int pid = 0;
  pthread_mutex_lock(&_mtx);
  for (struct slot *s = _slots, *p = 0; s; p = s, s = s->next) {
    if (s->export == export) {
      if (p) {
        p->next = s->next;
      } else {
        _slots = s->next;
      }
      if (export->hook_user_data) {
        struct slot *slot = export->hook_user_data;
        pid = slot->proc_pid;
        slot->proc_pid = 0;
        export->hook_user_data = 0;
      }
      free(s);
      break;
    }
  }
  pthread_mutex_unlock(&_mtx);
  if (pid) {
    iwn_proc_kill_ensure(g_env.poller, pid, SIGINT, -30, SIGINT);
  }
}

static iwrc _export_on_start(struct rct_producer_export *export) {
  iwrc rc = 0;
  bool paused = true;
  struct rct_producer *producer = export->producer;
  struct slot *slot = malloc(sizeof(*slot)), *n = 0;
  RCA(slot, finish);

  *slot = (struct slot) {
    .room = producer->transport->router->room,
    .export = export,
  };

  pthread_mutex_lock(&_mtx);
  for (n = _slots; n && n->next; n = n->next) {
    if (n->export->producer == producer) {
      rc = IW_ERROR_INVALID_STATE;
      iwlog_ecode_error(rc, "Producer %s is exported already", producer->uuid);
      pthread_mutex_unlock(&_mtx);
      goto finish;
    }
  }
  if (n) {
    n->next = slot;
  } else {
    _slots = slot;
  }
  export->hook_user_data = slot;
  paused = producer->paused;
  pthread_mutex_unlock(&_mtx);

  if (!paused) {
    RCC(rc, finish, _proc_spawn(export));
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}

iwrc rct_room_recording_stop(wrc_resource_t room_id) {
  iwrc rc = 0;
  IWULIST refs = { 0 };
  rct_room_t *room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  rct_unlock();
  if (!room) {
    return 0;
  }
  RCC(rc, finish, iwulist_init(&refs, 64, sizeof(wrc_resource_t)));

  // Save export list
  pthread_mutex_lock(&_mtx);
  if (!room->has_started_recording) {
    rct_resource_ref_keep_locking(room, false, -1, __func__);
    pthread_mutex_unlock(&_mtx);
    return 0;
  }
  room->has_started_recording = false;
  for (struct slot *s = _slots; s; s = s->next) {
    if (s->room == room) {
      iwulist_push(&refs, &s->export->id);
    }
  }
  pthread_mutex_unlock(&_mtx);

  for (int i = (int) iwulist_length(&refs) - 1; i >= 0; --i) {
    wrc_resource_t *idp = iwulist_at2(&refs, i);
    rct_producer_export_close(*idp);
  }

finish:
  iwlog_info("Recording stopped for room %s", room->uuid);
  rct_resource_ref_keep_locking(room, false, -1, __func__);
  iwulist_destroy_keep(&refs);
  if (!rc) {
    wrc_notify_event_handlers(WRC_EVT_ROOM_RECORDING_OFF, room_id, 0);
  }
  return rc;
}

static iwrc _producer_export_and_record(rct_producer_t *producer) {
  wrc_resource_t export_id;
  rct_producer_export_params_t params = {
    .on_start       = _export_on_start,
    .on_pause       = _export_on_pause,
    .on_resume      = _export_on_resume,
    .on_close       = _export_on_close,
    .close_on_pause = true
  };
  return rct_producer_export(producer->id, &params, &export_id);
}

iwrc rct_room_recording_start(wrc_resource_t room_id) {
  IWULIST refs;
  bool locked = false;
  iwrc rc = RCR(iwulist_init(&refs, 64, sizeof(rct_producer_t*)));
  rct_room_t *room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  locked = true;
  if (!room || room->has_started_recording) {
    goto finish;
  }
  if (g_env.log.verbose) {
    iwlog_debug("Starting recording, room: %s", room->uuid);
  }
  // Collect refs to all of the room producers
  for (rct_room_member_t *m = room->members; m; m = m->next) {
    for (size_t i = 0, l = iwulist_length(&m->resource_refs); i < l; ++i) {
      struct rct_resource_ref *r = iwulist_at2(&m->resource_refs, i);
      if (r->b && r->b->type == RCT_TYPE_PRODUCER) {
        rct_resource_ref_lk(r->b, 1, __func__);
        RCC(rc, finish, iwulist_push(&refs, &r->b));
      }
    }
  }
  room->num_recording_sessions++;
  room->has_started_recording = true;
  rct_unlock(), locked = false;

  for (size_t i = 0, l = iwulist_length(&refs); i < l; ++i) {
    rct_producer_t **pp = iwulist_at2(&refs, i);
    bool paused = false;
    rct_lock();
    paused = (*pp)->paused;
    rct_unlock();
    iwrc rc = paused ? 0 : _producer_export_and_record(*pp);
    if (rc) {
      iwlog_ecode_warn3(rc);
    }
  }

  wrc_notify_event_handlers(WRC_EVT_ROOM_RECORDING_ON, room_id, 0);

finish:
  rct_resource_ref_keep_locking(room, locked, -1, __func__);
  if (!locked) {
    rct_lock();
  }
  for (int i = 0, l = iwulist_length(&refs); i < l; ++i) {
    rct_producer_t **pp = iwulist_at2(&refs, i);
    rct_resource_ref_lk(*pp, -1, __func__);
  }
  rct_unlock();
  iwulist_destroy_keep(&refs);
  return rc;
}

static iwrc _on_producer_created_or_resumed(wrc_resource_t producer_id) {
  iwrc rc = 0;
  rct_producer_t *producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__);
  bool has_started_recording = false, exported = false;
  if (producer) {
    rct_room_t *room = producer->transport->router->room;
    if (room) {
      has_started_recording = room->has_started_recording;
      exported = producer->export != 0;
    }
  }
  rct_unlock();
  if (has_started_recording && !exported) {
    rc = _producer_export_and_record(producer);
  }
  if (producer) {
    rct_resource_ref_unlock(producer, false, -1, __func__);
  }
  return rc;
}

static void _on_room_closed(JBL data) {
  int64_t nrs = 0;
  jbl_object_get_i64(data, "nrs", &nrs);
  if (nrs > 0) { // Only if room had recording sessions
    JBL jbl = 0;
    const char *cid = 0, *uuid = 0;
    jbl_object_get_str(data, "cid", &cid);
    jbl_object_get_str(data, "room", &uuid);
    if (uuid && cid && !jbl_create_empty_object(&jbl)) {
      jbl_set_string(jbl, "uuid", uuid);
      // Room cid will be used as task hook
      gr_persistent_task_submit(PT_RECORDING_POSTPROC, cid, jbl);
      jbl_destroy(&jbl);
    }
  }
}

static uint32_t _event_handler_id;

static iwrc _event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  switch (evt) {
    case WRC_EVT_PRODUCER_RESUME:
    case WRC_EVT_PRODUCER_CREATED:
      _on_producer_created_or_resumed(resource_id);
      break;
    case WRC_EVT_ROOM_CLOSED:
      _on_room_closed(data);
      break;
    default:
      break;
  }
  return 0;
}

iwrc rct_room_recording_module_init(void) {
  return wrc_add_event_handler(_event_handler, 0, &_event_handler_id);
}

void rct_room_recording_module_close(void) {
  wrc_remove_event_handler(_event_handler_id);
}
