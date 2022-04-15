#include "html.h"

#include <ejdb2/iowow/iwxstr.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>

IW_INLINE bool _is_allowed_tag(const char *tag) {
  // Warning: strictly check tag names to disallow injection
  // Best practive is whitelist but we also can use smart filtering
  if (!strcasecmp(tag, "SCRIPT") || !strcasecmp(tag, "STYLE") || !strcasecmp(tag, "IFRAME")) {
    return false;
  }
  // Original JS code generates upper case tags since it is canonical
  // https://developer.mozilla.org/en-US/docs/Web/API/Element/tagName
  // Should allow "div", "span", "br", "title", "hr", "a", "h*", maybe some more
  return strspn(tag, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") == strlen(tag);
}

IW_INLINE bool _is_allowed_attr(const char *tag, const char *attr, const char *value) {
  // Warning: strictly check argument names to disallow injection
  // Best practive is whitelist but we also can use smart filtering
  // target=_blank is expicitly added in _html_safe_alloc_from_jbn
  if (!strncasecmp(attr, "on", 2) || !strcasecmp(attr, "class") || !strcasecmp(attr, "target")) {
    return false;
  }
  if (!strcasecmp(attr, "href")) {
    return !strncasecmp(value, "http://", 7) || !strncasecmp(value, "https://", 8);
  }
  // Should allow "style", "src", "target", "href", "color", "background", maybe some more
  return strspn(attr, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == strlen(attr);
}

IW_INLINE bool _is_self_closing(const char *tag) {
  // Some of void elements that supports self closing tags
  return !strcasecmp(tag, "IMG") || !strcasecmp(tag, "HR") || !strcasecmp(tag, "BR");
}

IW_INLINE iwrc _cat_escape_attr(IWXSTR *xstr, const char *attr) {
  // Escape characters '"' and '\''
  const char *ch = attr;
  while ((ch = strpbrk(attr, "\"'")) != 0) {
    RCR(iwxstr_cat(xstr, attr, ch - attr));
    if (*ch == '"') {
      RCR(iwxstr_cat(xstr, "&quot;", 6));
    } else {
      RCR(iwxstr_cat(xstr, "&#39;", 5));
    }
    attr = ch + 1;
  }
  RCR(iwxstr_cat2(xstr, attr));
  return 0;
}

IW_INLINE iwrc _cat_escape_text(IWXSTR *xstr, const char *attr) {
  // Escape characters '&', '<' and '>'
  const char *ch = attr;
  while ((ch = strpbrk(attr, "&<>")) != 0) {
    RCR(iwxstr_cat(xstr, attr, ch - attr));
    if (*ch == '&') {
      RCR(iwxstr_cat(xstr, "&amp;", 5));
    } else {
      RCR(iwxstr_cat(xstr, *ch == '<' ? "&lt;" : "&gt;", 4));
    }
    attr = ch + 1;
  }
  RCR(iwxstr_cat2(xstr, attr));
  return 0;
}

static iwrc _html_safe_alloc_from_jbn(IWXSTR *xstr, JBL_NODE node) {
  if (!node) {
    return 0;
  }
  if (node->type == JBV_ARRAY) {
    for (JBL_NODE i = node->child; i != 0; i = i->next) {
      RCR(_html_safe_alloc_from_jbn(xstr, i));
    }
  } else if (node->type == JBV_STR) {
    RCR(_cat_escape_text(xstr, node->vptr));
  } else if (node->type != JBV_OBJECT) {
    return 0;
  }

  JBL_NODE tag, attrs, children;
  if (jbn_at(node, "/t", &tag) || tag->type != JBV_STR || !_is_allowed_tag(tag->vptr)) {
    return 0;
  }
  if (jbn_at(node, "/a", &attrs) || attrs->type != JBV_OBJECT || attrs->child == 0) {
    attrs = 0;
  }
  if (jbn_at(node, "/c", &children) || children->type != JBV_ARRAY || children->child == 0) {
    children = 0;
  }

  RCR(iwxstr_cat(xstr, "<", 1));
  RCR(iwxstr_cat(xstr, tag->vptr, tag->vsize));

  if (!strcasecmp(tag->vptr, "A")) {
    RCR(iwxstr_cat(xstr, " target=_blank", 14));
  }

  for (JBL_NODE i = attrs ? attrs->child : 0; i != 0; i = i->next) {
    if (i->type == JBV_STR && _is_allowed_attr(tag->vptr, i->key, i->vptr)) {
      RCR(iwxstr_cat(xstr, " ", 1));
      RCR(iwxstr_cat(xstr, i->key, i->klidx));
      if (i->vsize) {
        RCR(iwxstr_cat(xstr, "=\"", 2));
        RCR(_cat_escape_attr(xstr, i->vptr));
        RCR(iwxstr_cat(xstr, "\"", 1));
      }
    }
  }

  if (children || !_is_self_closing(tag->key)) {
    RCR(iwxstr_cat(xstr, ">", 1));

    RCR(_html_safe_alloc_from_jbn(xstr, children));

    RCR(iwxstr_cat(xstr, "</", 2));
    RCR(iwxstr_cat(xstr, tag->vptr, tag->vsize));
    RCR(iwxstr_cat(xstr, ">", 1));
  } else {
    RCR(iwxstr_cat(xstr, "/>", 2));
  }

  return 0;
}

char* html_safe_alloc_from_jbn(JBL_NODE message) {
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return 0;
  }

  if (_html_safe_alloc_from_jbn(xstr, message)) {
    iwxstr_destroy(xstr);
    return 0;
  }

  char *ret = iwxstr_ptr(xstr);
  iwxstr_destroy_keep_ptr(xstr);
  return ret;
}
