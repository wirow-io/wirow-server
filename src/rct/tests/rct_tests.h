#include "wrc/wrc.h"
#include <assert.h>
#include <ejdb2/iowow/iwstree.h>
#include <ejdb2/jbl.h>
#include <stdint.h>
#include <pthread.h>

#define RCT_BUFFERED_EVENTS 64

struct _event_stats {
  IWSTREE *stats; // Mapping event wrc_event_e => int
  pthread_mutex_t mtx;
  pthread_cond_t  econd;
  wrc_event_e     last_events[RCT_BUFFERED_EVENTS];
  int last_events_idx;
} event_stats = {
  .mtx   = PTHREAD_MUTEX_INITIALIZER,
  .econd = PTHREAD_COND_INITIALIZER
};

static bool _rct_is_matched_event(wrc_event_e evt) {
  for (int i = 0; i < RCT_BUFFERED_EVENTS; ++i) {
    if (event_stats.last_events[i] == evt) {
      return true;
    }
  }
  return false;
}

static void _rct_reset_awaited_event(void) {
  pthread_mutex_lock(&event_stats.mtx);
  memset(event_stats.last_events, 0, sizeof(event_stats.last_events));
  pthread_mutex_unlock(&event_stats.mtx);
}

static void _rct_await_event(wrc_event_e evt) {
  pthread_mutex_lock(&event_stats.mtx);
  if (_rct_is_matched_event(evt)) {
    goto finish;
  }
  while (1) {
    pthread_cond_wait(&event_stats.econd, &event_stats.mtx);
    if (_rct_is_matched_event(evt)) {
      break;
    }
  }
finish:
  pthread_mutex_unlock(&event_stats.mtx);
}

static void _rct_event_stats_reset(struct _event_stats *s) {
  assert(s && s->stats);
  pthread_mutex_lock(&s->mtx);
  iwstree_clear(s->stats);
  pthread_mutex_unlock(&s->mtx);
}

static iwrc _rct_event_stats_register(struct _event_stats *s, wrc_event_e evt) {
  assert(s);
  pthread_mutex_lock(&s->mtx);
  iwrc rc = 0;
  uintptr_t idx = (uintptr_t) evt;
  IWSTREE *stree = s->stats;
  assert(stree);

  s->last_events[s->last_events_idx] = evt;
  s->last_events_idx = (s->last_events_idx + 1) % 64;

  uintptr_t cnt = (uintptr_t) iwstree_get(stree, (void*) idx);
  ++cnt;
  rc = iwstree_put_overwrite(stree, (void*) idx, (void*) cnt);
  pthread_cond_broadcast(&event_stats.econd);
  pthread_mutex_unlock(&s->mtx);
  return rc;
}

static int _rct_event_stats_count(struct _event_stats *s, wrc_event_e evt) {
  pthread_mutex_lock(&s->mtx);
  uintptr_t idx = (uintptr_t) evt;
  uintptr_t cnt = (uintptr_t) iwstree_get(s->stats, (void*) idx);
  pthread_mutex_unlock(&s->mtx);
  return (int) cnt;
}

static iwrc _rct_event_stats_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  struct _event_stats *s = op;
  assert(s);
  iwrc rc = _rct_event_stats_register(s, evt);
  return rc;
}

static void _rct_event_stats_print(struct _event_stats *s) {
  pthread_mutex_lock(&s->mtx);
  IWSTREE_ITER it;
  iwrc rc = iwstree_iter_init(s->stats, &it);
  RCGO(rc, finish);
  while (iwstree_iter_has_next(&it)) {
    void *k, *v;
    rc = iwstree_iter_next(&it, &k, &v);
    RCBREAK(rc);
    int evt = (uintptr_t) k;
    int cnt = (uintptr_t) v;
    fprintf(stderr, "%s => %d\n", wrc_event_name(evt), cnt);
  }
  iwstree_iter_close(&it);

finish:
  pthread_mutex_unlock(&s->mtx);
}

static uint32_t _event_stats_handler_id;

static iwrc _rct_event_stats_init() {
  if (!event_stats.stats) {
    event_stats.stats = iwstree_create(0, 0);
    assert(event_stats.stats);
  }
  return wrc_add_event_handler(_rct_event_stats_handler, &event_stats, &_event_stats_handler_id);
}

static void _rct_event_stats_destroy() {
  wrc_remove_event_handler(_event_stats_handler_id);
  pthread_mutex_lock(&event_stats.mtx);
  if (event_stats.stats) {
    iwstree_destroy(event_stats.stats);
    event_stats.stats = 0;
  }
  pthread_mutex_unlock(&event_stats.mtx);
  pthread_mutex_destroy(&event_stats.mtx);
  pthread_cond_destroy(&event_stats.econd);
}
