#include <string.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/range.h>
#include <mruby/internal.h>
#include <mruby/presym.h>

#define ENC_ASCII_8BIT "ASCII-8BIT"
#define ENC_BINARY     "BINARY"
#define ENC_UTF8       "UTF-8"

#define ENC_COMP_P(enc, enc_lit) \
  casecmp_p(RSTRING_PTR(enc), RSTRING_LEN(enc), enc_lit, sizeof(enc_lit"")-1)

static mrb_bool
casecmp_p(const char *s1, mrb_int len1, const char *s2, mrb_int len2)
{
  if (len1 != len2) return FALSE;

  const char *e1 = s1 + len1;
  const char *e2 = s2 + len2;
  while (s1 < e1 && s2 < e2) {
    if (*s1 != *s2 && TOUPPER(*s1) != TOUPPER(*s2)) return FALSE;
    s1++;
    s2++;
  }
  return TRUE;
}

static mrb_value
int_chr_binary(mrb_state *mrb, mrb_value num)
{
  mrb_int cp = mrb_as_int(mrb, num);

  if (cp < 0 || 0xff < cp) {
    mrb_raisef(mrb, E_RANGE_ERROR, "%v out of char range", num);
  }
  char c = (char)cp;
  mrb_value str = mrb_str_new(mrb, &c, 1);
  RSTR_SET_ASCII_FLAG(mrb_str_ptr(str));
  return str;
}

#ifdef MRB_UTF8_STRING
static mrb_value
int_chr_utf8(mrb_state *mrb, mrb_value num)
{
  mrb_int cp = mrb_int(mrb, num);
  char utf8[4];
  mrb_int len;
  mrb_value str;
  uint32_t sb_flag = 0;

  if (cp < 0 || 0x10FFFF < cp) {
    mrb_raisef(mrb, E_RANGE_ERROR, "%v out of char range", num);
  }
  if (cp < 0x80) {
    utf8[0] = (char)cp;
    len = 1;
    sb_flag = MRB_STR_SINGLE_BYTE;
  }
  else if (cp < 0x800) {
    utf8[0] = (char)(0xC0 | (cp >> 6));
    utf8[1] = (char)(0x80 | (cp & 0x3F));
    len = 2;
  }
  else if (cp < 0x10000) {
    utf8[0] = (char)(0xE0 |  (cp >> 12));
    utf8[1] = (char)(0x80 | ((cp >>  6) & 0x3F));
    utf8[2] = (char)(0x80 | ( cp        & 0x3F));
    len = 3;
  }
  else {
    utf8[0] = (char)(0xF0 |  (cp >> 18));
    utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    utf8[2] = (char)(0x80 | ((cp >>  6) & 0x3F));
    utf8[3] = (char)(0x80 | ( cp        & 0x3F));
    len = 4;
  }
  str = mrb_str_new(mrb, utf8, len);
  mrb_str_ptr(str)->flags |= sb_flag;
  return str;
}
#endif

/*
 *  call-seq:
 *     str.swapcase!   -> str or nil
 *
 *  Equivalent to <code>String#swapcase</code>, but modifies the receiver in
 *  place, returning <i>str</i>, or <code>nil</code> if no changes were made.
 *  Note: case conversion is effective only in ASCII region.
 */
static mrb_value
str_swapcase_bang(mrb_state *mrb, mrb_value str)
{
  int modify = 0;
  struct RString *s = mrb_str_ptr(str);

  mrb_str_modify(mrb, s);
  char *p = RSTRING_PTR(str);
  char *pend = p + RSTRING_LEN(str);
  while (p < pend) {
    if (ISUPPER(*p)) {
      *p = TOLOWER(*p);
      modify = 1;
    }
    else if (ISLOWER(*p)) {
      *p = TOUPPER(*p);
      modify = 1;
    }
    p++;
  }

  if (modify) return str;
  return mrb_nil_value();
}

/*
 *  call-seq:
 *     str.swapcase   -> new_str
 *
 *  Returns a copy of <i>str</i> with uppercase alphabetic characters converted
 *  to lowercase and lowercase characters converted to uppercase.
 *  Note: case conversion is effective only in ASCII region.
 *
 *     "Hello".swapcase          #=> "hELLO"
 *     "cYbEr_PuNk11".swapcase   #=> "CyBeR_pUnK11"
 */
static mrb_value
str_swapcase(mrb_state *mrb, mrb_value self)
{
  mrb_value str = mrb_str_dup(mrb, self);
  str_swapcase_bang(mrb, str);
  return str;
}

static void
str_concat(mrb_state *mrb, mrb_value self, mrb_value str, mrb_bool binary)
{
  if (mrb_integer_p(str) || mrb_float_p(str)) {
#ifdef MRB_UTF8_STRING
    if (binary) {
      str = int_chr_binary(mrb, str);
    }
    else {
      str = int_chr_utf8(mrb, str);
    }
#else
    str = int_chr_binary(mrb, str);
#endif
  }
  else
    mrb_ensure_string_type(mrb, str);
  mrb_str_cat_str(mrb, self, str);
}

static mrb_value
str_concat0(mrb_state *mrb, mrb_value self, mrb_bool binary)
{
  if (mrb_get_argc(mrb) == 1) {
    str_concat(mrb, self, mrb_get_arg1(mrb), binary);
    return self;
  }

  mrb_value *args;
  mrb_int alen;

  mrb_get_args(mrb, "*", &args, &alen);
  for (mrb_int i=0; i<alen; i++) {
    str_concat(mrb, self, args[i], binary);
  }
  return self;
}

/*
 *  call-seq:
 *     str << obj           -> str
 *     str.concat(*obj)     -> str
 *
 *    s = 'foo'
 *    s.concat('bar', 'baz') # => "foobarbaz"
 *    s                      # => "foobarbaz"
 *
 *  For each given object +object+ that is an \Integer,
 *  the value is considered a codepoint and converted to a character before concatenation:
 *
 *    s = 'foo'
 *    s.concat(32, 'bar', 32, 'baz') # => "foo bar baz"
 *
 */
static mrb_value
str_concat_m(mrb_state *mrb, mrb_value self)
{
  mrb_bool binary = RSTR_BINARY_P(mrb_str_ptr(self));
  return str_concat0(mrb, self, binary);
}

/*
 *  call-seq:
 *     str.append_as_bytes(*obj)     -> str
 *
 *  Works like `concat` but consider arguments as binary strings.
 *
 */
static mrb_value
str_append_as_bytes(mrb_state *mrb, mrb_value self)
{
  return str_concat0(mrb, self, TRUE);
}

/*
 *  call-seq:
 *     str.start_with?([prefixes]+)   -> true or false
 *
 *  Returns true if +str+ starts with one of the +prefixes+ given.
 *
 *    "hello".start_with?("hell")               #=> true
 *
 *    # returns true if one of the prefixes matches.
 *    "hello".start_with?("heaven", "hell")     #=> true
 *    "hello".start_with?("heaven", "paradise") #=> false
 *    "h".start_with?("heaven", "hell")         #=> false
 */
static mrb_value
str_start_with(mrb_state *mrb, mrb_value self)
{
  const mrb_value *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);

  for (mrb_int i = 0; i < argc; i++) {
    int ai = mrb_gc_arena_save(mrb);
    mrb_value sub = argv[i];
    mrb_ensure_string_type(mrb, sub);
    mrb_gc_arena_restore(mrb, ai);
    size_t len_l = RSTRING_LEN(self);
    size_t len_r = RSTRING_LEN(sub);
    if (len_l >= len_r) {
      if (memcmp(RSTRING_PTR(self), RSTRING_PTR(sub), len_r) == 0) {
        return mrb_true_value();
      }
    }
  }
  return mrb_false_value();
}

/*
 *  call-seq:
 *     str.end_with?([suffixes]+)   -> true or false
 *
 *  Returns true if +str+ ends with one of the +suffixes+ given.
 */
static mrb_value
str_end_with(mrb_state *mrb, mrb_value self)
{
  const mrb_value *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);

  for (mrb_int i = 0; i < argc; i++) {
    int ai = mrb_gc_arena_save(mrb);
    mrb_value sub = argv[i];
    mrb_ensure_string_type(mrb, sub);
    mrb_gc_arena_restore(mrb, ai);
    size_t len_l = RSTRING_LEN(self);
    size_t len_r = RSTRING_LEN(sub);
    if (len_l >= len_r) {
      if (memcmp(RSTRING_PTR(self) + (len_l - len_r),
                 RSTRING_PTR(sub),
                 len_r) == 0) {
        return mrb_true_value();
      }
    }
  }
  return mrb_false_value();
}

enum tr_pattern_type {
  TR_UNINITIALIZED = 0,
  TR_IN_ORDER  = 1,
  TR_RANGE = 2,
};

/*
  #tr Pattern syntax

  <syntax> ::= (<pattern>)* | '^' (<pattern>)*
  <pattern> ::= <in order> | <range>
  <in order> ::= (<ch>)+
  <range> ::= <ch> '-' <ch>
*/
struct tr_pattern {
  uint8_t type;                 // 1:in-order, 2:range
  mrb_bool flag_reverse : 1;
  mrb_bool flag_on_heap : 1;
  uint16_t n;
  union {
    uint16_t start_pos;
    char ch[2];
  } val;
  struct tr_pattern *next;
};

#define STATIC_TR_PATTERN { 0 }

static inline void
tr_free_pattern(mrb_state *mrb, struct tr_pattern *pat)
{
  while (pat) {
    struct tr_pattern *p = pat->next;
    if (pat->flag_on_heap) {
      mrb_free(mrb, pat);
    }
    pat = p;
  }
}

static struct tr_pattern*
tr_parse_pattern(mrb_state *mrb, struct tr_pattern *ret, const mrb_value v_pattern, mrb_bool flag_reverse_enable, struct tr_pattern *pat0)
{
  const char *pattern = RSTRING_PTR(v_pattern);
  mrb_int pattern_length = RSTRING_LEN(v_pattern);
  mrb_bool flag_reverse = FALSE;
  mrb_int i = 0;

  if (flag_reverse_enable && pattern_length >= 2 && pattern[0] == '^') {
    flag_reverse = TRUE;
    i++;
  }

  while (i < pattern_length) {
    /* is range pattern ? */
    mrb_bool const ret_uninit = (ret->type == TR_UNINITIALIZED);
    struct tr_pattern *pat1 = ret_uninit ? ret
                              : (struct tr_pattern*)mrb_malloc_simple(mrb, sizeof(struct tr_pattern));
    if (pat1 == NULL) {
      if (pat0) tr_free_pattern(mrb, pat0);
      tr_free_pattern(mrb, ret);
      mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
      return NULL;            /* not reached */
    }
    if ((i+2) < pattern_length && pattern[i] != '\\' && pattern[i+1] == '-') {
      pat1->type = TR_RANGE;
      pat1->flag_reverse = flag_reverse;
      pat1->flag_on_heap = !ret_uninit;
      pat1->n = pattern[i+2] - pattern[i] + 1;
      pat1->next = NULL;
      pat1->val.ch[0] = pattern[i];
      pat1->val.ch[1] = pattern[i+2];
      i += 3;
    }
    else {
      /* in order pattern. */
      mrb_int start_pos = i++;

      while (i < pattern_length) {
        if ((i+2) < pattern_length && pattern[i] != '\\' && pattern[i+1] == '-')
          break;
        i++;
      }

      mrb_int len = i - start_pos;
      if (len > UINT16_MAX) {
        if (pat0) tr_free_pattern(mrb, pat0);
        tr_free_pattern(mrb, ret);
        if (ret != pat1) mrb_free(mrb, pat1);
        mrb_raise(mrb, E_ARGUMENT_ERROR, "tr pattern too long (max 65535)");
      }
      pat1->type = TR_IN_ORDER;
      pat1->flag_reverse = flag_reverse;
      pat1->flag_on_heap = !ret_uninit;
      pat1->n = (uint16_t)len;
      pat1->next = NULL;
      pat1->val.start_pos = (uint16_t)start_pos;
    }

    if (!ret_uninit) {
      struct tr_pattern *p = ret;
      while (p->next != NULL) {
        p = p->next;
      }
      p->next = pat1;
    }
  }

  return ret;
}

static inline mrb_int
tr_find_character(const struct tr_pattern *pat, const char *pat_str, int ch)
{
  mrb_int ret = -1;
  mrb_int n_sum = 0;
  mrb_int flag_reverse = pat ? pat->flag_reverse : 0;

  while (pat != NULL) {
    if (pat->type == TR_IN_ORDER) {
      for (int i = 0; i < pat->n; i++) {
        if (pat_str[pat->val.start_pos + i] == ch) ret = n_sum + i;
      }
    }
    else if (pat->type == TR_RANGE) {
      if (pat->val.ch[0] <= ch && ch <= pat->val.ch[1])
        ret = n_sum + ch - pat->val.ch[0];
    }
    else {
      mrb_assert(pat->type == TR_UNINITIALIZED);
    }
    n_sum += pat->n;
    pat = pat->next;
  }

  if (flag_reverse) {
    return (ret < 0) ? MRB_INT_MAX : -1;
  }
  return ret;
}

static inline mrb_int
tr_get_character(const struct tr_pattern *pat, const char *pat_str, mrb_int n_th)
{
  mrb_int n_sum = 0;

  while (pat != NULL) {
    if (n_th < (n_sum + pat->n)) {
      mrb_int i = (n_th - n_sum);

      switch (pat->type) {
      case TR_IN_ORDER:
        return pat_str[pat->val.start_pos + i];
      case TR_RANGE:
        return pat->val.ch[0]+i;
      case TR_UNINITIALIZED:
        return -1;
      }
    }
    if (pat->next == NULL) {
      switch (pat->type) {
      case TR_IN_ORDER:
        return pat_str[pat->val.start_pos + pat->n - 1];
      case TR_RANGE:
        return pat->val.ch[1];
      case TR_UNINITIALIZED:
        return -1;
      }
    }
    n_sum += pat->n;
    pat = pat->next;
  }

  return -1;
}

static inline void
tr_bitmap_set(uint8_t bitmap[32], uint8_t ch)
{
  uint8_t idx1 = ch / 8;
  uint8_t idx2 = ch % 8;
  bitmap[idx1] |= (1<<idx2);
}

static inline mrb_bool
tr_bitmap_detect(uint8_t bitmap[32], uint8_t ch)
{
  uint8_t idx1 = ch / 8;
  uint8_t idx2 = ch % 8;
  if (bitmap[idx1] & (1<<idx2))
    return TRUE;
  return FALSE;
}

/* compile pattern to bitmap */
static void
tr_compile_pattern(const struct tr_pattern *pat, mrb_value pstr, uint8_t bitmap[32])
{
  const char *pattern = RSTRING_PTR(pstr);
  mrb_int flag_reverse = pat ? pat->flag_reverse : 0;
  int i;

  for (int i=0; i<32; i++) {
    bitmap[i] = 0;
  }
  while (pat != NULL) {
    if (pat->type == TR_IN_ORDER) {
      for (i = 0; i < pat->n; i++) {
        tr_bitmap_set(bitmap, pattern[pat->val.start_pos + i]);
      }
    }
    else if (pat->type == TR_RANGE) {
      for (i = pat->val.ch[0]; i < pat->val.ch[1]; i++) {
        tr_bitmap_set(bitmap, i);
      }
    }
    else {
      mrb_assert(pat->type == TR_UNINITIALIZED);
    }
    pat = pat->next;
  }

  if (flag_reverse) {
    for (i=0; i<32; i++) {
      bitmap[i] ^= 0xff;
    }
  }
}

static mrb_bool
str_tr(mrb_state *mrb, mrb_value str, mrb_value p1, mrb_value p2, mrb_bool squeeze)
{
  struct tr_pattern pat = STATIC_TR_PATTERN;
  struct tr_pattern rep = STATIC_TR_PATTERN;
  mrb_bool flag_changed = FALSE;
  mrb_int lastch = -1;

  mrb_str_modify(mrb, mrb_str_ptr(str));
  tr_parse_pattern(mrb, &pat, p1, TRUE, NULL);
  tr_parse_pattern(mrb, &rep, p2, FALSE, &pat);
  char *s = RSTRING_PTR(str);
  mrb_int len = RSTRING_LEN(str);

  mrb_int i, j;
  for (i=j=0; i<len; i++,j++) {
    mrb_int n = tr_find_character(&pat, RSTRING_PTR(p1), s[i]);

    if (i>j) s[j] = s[i];
    if (n >= 0) {
      flag_changed = TRUE;
      mrb_int c = tr_get_character(&rep, RSTRING_PTR(p2), n);

      if (c < 0 || (squeeze && c == lastch)) {
        j--;
        continue;
      }
      if (c > 0x80) {
        tr_free_pattern(mrb, &pat);
        tr_free_pattern(mrb, &rep);
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "character (%i) out of range", c);
      }
      lastch = c;
      s[i] = (char)c;
    }
  }

  tr_free_pattern(mrb, &pat);
  tr_free_pattern(mrb, &rep);

  if (flag_changed) {
    RSTR_SET_LEN(RSTRING(str), j);
    RSTRING_PTR(str)[j] = 0;
  }
  return flag_changed;
}

/*
 * call-seq:
 *   str.tr(from_str, to_str)   => new_str
 *
 * Returns a copy of str with the characters in from_str replaced by the
 * corresponding characters in to_str.  If to_str is shorter than from_str,
 * it is padded with its last character in order to maintain the
 * correspondence.
 *
 *  "hello".tr('el', 'ip')      #=> "hippo"
 *  "hello".tr('aeiou', '*')    #=> "h*ll*"
 *  "hello".tr('aeiou', 'AA*')  #=> "hAll*"
 *
 * Both strings may use the c1-c2 notation to denote ranges of characters,
 * and from_str may start with a ^, which denotes all characters except
 * those listed.
 *
 *  "hello".tr('a-y', 'b-z')    #=> "ifmmp"
 *  "hello".tr('^aeiou', '*')   #=> "*e**o"
 *
 * The backslash character \ can be used to escape ^ or - and is otherwise
 * ignored unless it appears at the end of a range or the end of the
 * from_str or to_str:
 *
 *
 *  "hello^world".tr("\\^aeiou", "*") #=> "h*ll**w*rld"
 *  "hello-world".tr("a\\-eo", "*")   #=> "h*ll**w*rld"
 *
 *  "hello\r\nworld".tr("\r", "")   #=> "hello\nworld"
 *  "hello\r\nworld".tr("\\r", "")  #=> "hello\r\nwold"
 *  "hello\r\nworld".tr("\\\r", "") #=> "hello\nworld"
 *
 *  "X['\\b']".tr("X\\", "")   #=> "['b']"
 *  "X['\\b']".tr("X-\\]", "") #=> "'b'"
 *
 *  Note: conversion is effective only in ASCII region.
 */
static mrb_value
str_tr_m(mrb_state *mrb, mrb_value str)
{
  mrb_value p1, p2;

  mrb_get_args(mrb, "SS", &p1, &p2);
  mrb_value dup = mrb_str_dup(mrb, str);
  str_tr(mrb, dup, p1, p2, FALSE);
  return dup;
}

/*
 * call-seq:
 *   str.tr!(from_str, to_str)   -> str or nil
 *
 * Translates str in place, using the same rules as String#tr.
 * Returns str, or nil if no changes were made.
 */
static mrb_value
str_tr_bang(mrb_state *mrb, mrb_value str)
{
  mrb_value p1, p2;

  mrb_get_args(mrb, "SS", &p1, &p2);
  if (str_tr(mrb, str, p1, p2, FALSE)) {
    return str;
  }
  return mrb_nil_value();
}

/*
 * call-seq:
 *   str.tr_s(from_str, to_str)   -> new_str
 *
 * Processes a copy of str as described under String#tr, then removes
 * duplicate characters in regions that were affected by the translation.
 *
 *  "hello".tr_s('l', 'r')     #=> "hero"
 *  "hello".tr_s('el', '*')    #=> "h*o"
 *  "hello".tr_s('el', 'hx')   #=> "hhxo"
 */
static mrb_value
str_tr_s(mrb_state *mrb, mrb_value str)
{
  mrb_value p1, p2;

  mrb_get_args(mrb, "SS", &p1, &p2);
  mrb_value dup = mrb_str_dup(mrb, str);
  str_tr(mrb, dup, p1, p2, TRUE);
  return dup;
}

/*
 * call-seq:
 *   str.tr_s!(from_str, to_str)   -> str or nil
 *
 * Performs String#tr_s processing on str in place, returning
 * str, or nil if no changes were made.
 */
static mrb_value
str_tr_s_bang(mrb_state *mrb, mrb_value str)
{
  mrb_value p1, p2;

  mrb_get_args(mrb, "SS", &p1, &p2);
  if (str_tr(mrb, str, p1, p2, TRUE)) {
    return str;
  }
  return mrb_nil_value();
}

static mrb_bool
str_squeeze(mrb_state *mrb, mrb_value str, mrb_value v_pat)
{
  struct tr_pattern pat_storage = STATIC_TR_PATTERN;
  struct tr_pattern *pat = NULL;
  mrb_int i, j;
  mrb_bool flag_changed = FALSE;
  mrb_int lastch = -1;
  uint8_t bitmap[32];

  mrb_str_modify(mrb, mrb_str_ptr(str));
  if (!mrb_nil_p(v_pat)) {
    pat = tr_parse_pattern(mrb, &pat_storage, v_pat, TRUE, NULL);
    tr_compile_pattern(pat, v_pat, bitmap);
    tr_free_pattern(mrb, pat);
  }
  char *s = RSTRING_PTR(str);
  mrb_int len = RSTRING_LEN(str);

  if (pat) {
    for (i=j=0; i<len; i++,j++) {
      if (i>j) s[j] = s[i];
      if (tr_bitmap_detect(bitmap, s[i]) && s[i] == lastch) {
        flag_changed = TRUE;
        j--;
      }
      lastch = s[i];
    }
  }
  else {
    for (i=j=0; i<len; i++,j++) {
      if (i>j) s[j] = s[i];
      if (s[i] >= 0 && s[i] == lastch) {
        flag_changed = TRUE;
        j--;
      }
      lastch = s[i];
    }
  }

  if (flag_changed) {
    RSTR_SET_LEN(RSTRING(str), j);
    RSTRING_PTR(str)[j] = 0;
  }
  return flag_changed;
}

/*
 * call-seq:
 *   str.squeeze([other_str])    -> new_str
 *
 * Builds a set of characters from the other_str
 * parameter(s) using the procedure described for String#count. Returns a
 * new string where runs of the same character that occur in this set are
 * replaced by a single character. If no arguments are given, all runs of
 * identical characters are replaced by a single character.
 *
 *  "yellow moon".squeeze                  #=> "yelow mon"
 *  "  now   is  the".squeeze(" ")         #=> " now is the"
 *  "putters shoot balls".squeeze("m-z")   #=> "puters shot balls"
 */
static mrb_value
str_squeeze_m(mrb_state *mrb, mrb_value str)
{
  mrb_value pat = mrb_nil_value();

  mrb_get_args(mrb, "|S", &pat);
  mrb_value dup = mrb_str_dup(mrb, str);
  str_squeeze(mrb, dup, pat);
  return dup;
}

/*
 * call-seq:
 *   str.squeeze!([other_str])   -> str or nil
 *
 * Squeezes str in place, returning either str, or nil if no
 * changes were made.
 */
static mrb_value
str_squeeze_bang(mrb_state *mrb, mrb_value str)
{
  mrb_value pat = mrb_nil_value();

  mrb_get_args(mrb, "|S", &pat);
  if (str_squeeze(mrb, str, pat)) {
    return str;
  }
  return mrb_nil_value();
}

static mrb_bool
str_delete(mrb_state *mrb, mrb_value str, mrb_value v_pat)
{
  struct tr_pattern pat = STATIC_TR_PATTERN;
  mrb_bool flag_changed = FALSE;
  uint8_t bitmap[32];

  mrb_str_modify(mrb, mrb_str_ptr(str));
  tr_parse_pattern(mrb, &pat, v_pat, TRUE, NULL);
  tr_compile_pattern(&pat, v_pat, bitmap);
  tr_free_pattern(mrb, &pat);

  char *s = RSTRING_PTR(str);
  mrb_int len = RSTRING_LEN(str);
  mrb_int i, j;

  for (i=j=0; i<len; i++,j++) {
    if (i>j) s[j] = s[i];
    if (tr_bitmap_detect(bitmap, s[i])) {
      flag_changed = TRUE;
      j--;
    }
  }
  if (flag_changed) {
    RSTR_SET_LEN(RSTRING(str), j);
    RSTRING_PTR(str)[j] = 0;
  }
  return flag_changed;
}

static mrb_value
str_delete_m(mrb_state *mrb, mrb_value str)
{
  mrb_value pat;

  mrb_get_args(mrb, "S", &pat);
  mrb_value dup = mrb_str_dup(mrb, str);
  str_delete(mrb, dup, pat);
  return dup;
}

static mrb_value
str_delete_bang(mrb_state *mrb, mrb_value str)
{
  mrb_value pat;

  mrb_get_args(mrb, "S", &pat);
  if (str_delete(mrb, str, pat)) {
    return str;
  }
  return mrb_nil_value();
}

/*
 * call_seq:
 *   str.count([other_str])   -> integer
 *
 * Each other_str parameter defines a set of characters to count.  The
 * intersection of these sets defines the characters to count in str.  Any
 * other_str that starts with a caret ^ is negated.  The sequence c1-c2
 * means all characters between c1 and c2.  The backslash character \ can
 * be used to escape ^ or - and is otherwise ignored unless it appears at
 * the end of a sequence or the end of a other_str.
 */
static mrb_value
str_count(mrb_state *mrb, mrb_value str)
{
  mrb_value v_pat = mrb_nil_value();
  struct tr_pattern pat = STATIC_TR_PATTERN;
  uint8_t bitmap[32];

  mrb_get_args(mrb, "S", &v_pat);
  tr_parse_pattern(mrb, &pat, v_pat, TRUE, NULL);
  tr_compile_pattern(&pat, v_pat, bitmap);
  tr_free_pattern(mrb, &pat);

  char *s = RSTRING_PTR(str);
  mrb_int len = RSTRING_LEN(str);
  mrb_int count = 0;
  for (mrb_int i = 0; i < len; i++) {
    if (tr_bitmap_detect(bitmap, s[i])) count++;
  }
  return mrb_fixnum_value(count);
}

static mrb_value
str_hex(mrb_state *mrb, mrb_value self)
{
  return mrb_str_to_integer(mrb, self, 16, FALSE);
}

static mrb_value
str_oct(mrb_state *mrb, mrb_value self)
{
  return mrb_str_to_integer(mrb, self, 8, FALSE);
}

/*
 *  call-seq:
 *     string.chr    ->  string
 *
 *  Returns a one-character string at the beginning of the string.
 *
 *     a = "abcde"
 *     a.chr    #=> "a"
 */
static mrb_value
str_chr(mrb_state *mrb, mrb_value self)
{
  return mrb_str_substr(mrb, self, 0, 1);
}

/*
 *  call-seq:
 *     int.chr([encoding])  ->  string
 *
 *  Returns a string containing the character represented by the +int+'s value
 *  according to +encoding+. +"ASCII-8BIT"+ (+"BINARY"+) and +"UTF-8"+ (only
 *  with +MRB_UTF8_STRING+) can be specified as +encoding+ (default is
 *  +"ASCII-8BIT"+).
 *
 *     65.chr                  #=> "A"
 *     230.chr                 #=> "\xE6"
 *     230.chr("ASCII-8BIT")   #=> "\xE6"
 *     230.chr("UTF-8")        #=> "\u00E6"
 */
static mrb_value
int_chr(mrb_state *mrb, mrb_value num)
{
  mrb_value enc;
  mrb_bool enc_given;

  mrb_get_args(mrb, "|S?", &enc, &enc_given);
  if (!enc_given ||
      ENC_COMP_P(enc, ENC_ASCII_8BIT) ||
      ENC_COMP_P(enc, ENC_BINARY)) {
    return int_chr_binary(mrb, num);
  }
#ifdef MRB_UTF8_STRING
  else if (ENC_COMP_P(enc, ENC_UTF8)) {
    return int_chr_utf8(mrb, num);
  }
#endif
  else {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "unknown encoding name - %v", enc);
  }
  /* not reached */
  return mrb_nil_value();
}

/*
 *  call-seq:
 *     string.succ    ->  string
 *
 *  Returns next sequence of the string;
 *
 *     a = "bed"
 *     a.succ    #=> "bee"
 */
static mrb_value
str_succ_bang(mrb_state *mrb, mrb_value self)
{
  mrb_value result;
  const char *prepend;
  struct RString *s = mrb_str_ptr(self);

  if (RSTRING_LEN(self) == 0)
    return self;

  mrb_str_modify(mrb, s);
  mrb_int l = RSTRING_LEN(self);
  unsigned char *p, *e, *b, *t;
  b = p = (unsigned char*) RSTRING_PTR(self);
  t = e = p + l;
  *(e--) = 0;

  // find trailing ascii/number
  while (e >= b) {
    if (ISALNUM(*e))
      break;
    e--;
  }
  if (e < b) {
    e = p + l - 1;
    result = mrb_str_new_lit(mrb, "");
  }
  else {
    // find leading letter of the ascii/number
    b = e;
    while (b > p) {
      if (!ISALNUM(*b) || (ISALNUM(*b) && *b != '9' && *b != 'z' && *b != 'Z'))
        break;
      b--;
    }
    if (!ISALNUM(*b))
      b++;
    result = mrb_str_new(mrb, (char*) p, b - p);
  }

  while (e >= b) {
    if (!ISALNUM(*e)) {
      if (*e == 0xff) {
        mrb_str_cat_lit(mrb, result, "\x01");
        (*e) = 0;
      }
      else
        (*e)++;
      break;
    }
    prepend = NULL;
    if (*e == '9') {
      if (e == b) prepend = "1";
      *e = '0';
    }
    else if (*e == 'z') {
      if (e == b) prepend = "a";
      *e = 'a';
    }
    else if (*e == 'Z') {
      if (e == b) prepend = "A";
      *e = 'A';
    }
    else {
      (*e)++;
      break;
    }
    if (prepend) mrb_str_cat_cstr(mrb, result, prepend);
    e--;
  }
  result = mrb_str_cat(mrb, result, (char*) b, t - b);
  l = RSTRING_LEN(result);
  mrb_str_resize(mrb, self, l);
  memcpy(RSTRING_PTR(self), RSTRING_PTR(result), l);
  return self;
}

static mrb_value
str_succ(mrb_state *mrb, mrb_value self)
{
  mrb_value str = mrb_str_dup(mrb, self);
  str_succ_bang(mrb, str);
  return str;
}

#ifdef MRB_UTF8_STRING
extern const char mrb_utf8len_table[];

MRB_INLINE mrb_int
utf8code(mrb_state* mrb, const unsigned char* p, const unsigned char *e)
{
  if (p[0] < 0x80) return p[0];

  mrb_int len = mrb_utf8len_table[p[0]>>3];
  if (p+len <= e && len > 1 && (p[1] & 0xc0) == 0x80) {
    if (len == 2)
      return ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
    if ((p[2] & 0xc0) == 0x80) {
      if (len == 3)
        return ((p[0] & 0x0f) << 12) + ((p[1] & 0x3f) << 6)
          + (p[2] & 0x3f);
      if ((p[3] & 0xc0) == 0x80) {
        if (len == 4)
          return ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12)
            + ((p[2] & 0x3f) << 6) + (p[3] & 0x3f);
      }
    }
  }
  mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid UTF-8 byte sequence");
  /* not reached */
  return -1;
}

static mrb_value
str_ord(mrb_state* mrb, mrb_value str)
{
  struct RString *s = mrb_str_ptr(str);
  const unsigned char *p = (unsigned char*)RSTR_PTR(s);
  const unsigned char *e = p + RSTR_LEN(s);
  mrb_int c;

  if (p == e) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "empty string");
  }
  if (RSTR_SINGLE_BYTE_P(s) || RSTR_BINARY_P(s)) {
    c = p[0];
  }
  else {
    c = utf8code(mrb, p, e);
  }
  return mrb_fixnum_value(c);
}

static mrb_value
str_codepoints(mrb_state *mrb, mrb_value str)
{
  struct RString *s = mrb_str_ptr(str);
  const unsigned char *p = (unsigned char*)RSTR_PTR(s);
  const unsigned char *e = p + RSTR_LEN(s);

  mrb->c->ci->mid = 0;
  mrb_value result = mrb_ary_new(mrb);
  if (RSTR_SINGLE_BYTE_P(s) || RSTR_BINARY_P(s)) {
    while (p < e) {
      mrb_ary_push(mrb, result, mrb_int_value(mrb, (mrb_int)*p));
      p++;
    }
  }
  else {
    while (p < e) {
      mrb_int c = utf8code(mrb, p, e);
      mrb_ary_push(mrb, result, mrb_int_value(mrb, c));
      p += mrb_utf8len_table[p[0]>>3];
    }
  }
  return result;
}
#else
static mrb_value
str_ord(mrb_state* mrb, mrb_value str)
{
  if (RSTRING_LEN(str) == 0)
    mrb_raise(mrb, E_ARGUMENT_ERROR, "empty string");
  return mrb_fixnum_value((unsigned char)RSTRING_PTR(str)[0]);
}

static mrb_value
str_codepoints(mrb_state *mrb, mrb_value self)
{
  char *p = RSTRING_PTR(self);
  char *e = p + RSTRING_LEN(self);

  mrb->c->ci->mid = 0;
  mrb_value result = mrb_ary_new(mrb);
  while (p < e) {
    mrb_ary_push(mrb, result, mrb_int_value(mrb, (mrb_int)*p));
    p++;
  }
  return result;
}
#endif

/*
 *  call-seq:
 *     str.delete_prefix!(prefix) -> self or nil
 *
 *  Deletes leading <code>prefix</code> from <i>str</i>, returning
 *  <code>nil</code> if no change was made.
 *
 *     "hello".delete_prefix!("hel") #=> "lo"
 *     "hello".delete_prefix!("llo") #=> nil
 */
static mrb_value
str_del_prefix_bang(mrb_state *mrb, mrb_value self)
{
  mrb_int plen;
  const char *ptr;
  struct RString *str = RSTRING(self);

  mrb_get_args(mrb, "s", &ptr, &plen);
  mrb_int slen = RSTR_LEN(str);
  if (plen > slen) return mrb_nil_value();
  char *s = RSTR_PTR(str);
  if (memcmp(s, ptr, plen) != 0) return mrb_nil_value();
  if (!mrb_frozen_p(str) && (RSTR_SHARED_P(str) || RSTR_FSHARED_P(str))) {
    str->as.heap.ptr += plen;
  }
  else {
    mrb_str_modify(mrb, str);
    s = RSTR_PTR(str);
    memmove(s, s+plen, slen-plen);
  }
  RSTR_SET_LEN(str, slen-plen);
  return self;
}

/*
 *  call-seq:
 *     str.delete_prefix(prefix) -> new_str
 *
 *  Returns a copy of <i>str</i> with leading <code>prefix</code> deleted.
 *
 *     "hello".delete_prefix("hel") #=> "lo"
 *     "hello".delete_prefix("llo") #=> "hello"
 */
static mrb_value
str_del_prefix(mrb_state *mrb, mrb_value self)
{
  mrb_int plen;
  const char *ptr;

  mrb_get_args(mrb, "s", &ptr, &plen);
  mrb_int slen = RSTRING_LEN(self);
  if (plen > slen) return mrb_str_dup(mrb, self);
  if (memcmp(RSTRING_PTR(self), ptr, plen) != 0)
    return mrb_str_dup(mrb, self);
  return mrb_str_substr(mrb, self, plen, slen-plen);
}

/*
 *  call-seq:
 *     str.delete_suffix!(suffix) -> self or nil
 *
 *  Deletes trailing <code>suffix</code> from <i>str</i>, returning
 *  <code>nil</code> if no change was made.
 *
 *     "hello".delete_suffix!("llo") #=> "he"
 *     "hello".delete_suffix!("hel") #=> nil
 */
static mrb_value
str_del_suffix_bang(mrb_state *mrb, mrb_value self)
{
  mrb_int plen;
  const char *ptr;
  struct RString *str = RSTRING(self);

  mrb_get_args(mrb, "s", &ptr, &plen);
  mrb_int slen = RSTR_LEN(str);
  if (plen > slen) return mrb_nil_value();
  char *s = RSTR_PTR(str);
  if (memcmp(s+slen-plen, ptr, plen) != 0) return mrb_nil_value();
  if (!mrb_frozen_p(str) && (RSTR_SHARED_P(str) || RSTR_FSHARED_P(str))) {
    /* no need to modify string */
  }
  else {
    mrb_str_modify(mrb, str);
  }
  RSTR_SET_LEN(str, slen-plen);
  return self;
}

/*
 *  call-seq:
 *     str.delete_suffix(suffix) -> new_str
 *
 *  Returns a copy of <i>str</i> with leading <code>suffix</code> deleted.
 *
 *     "hello".delete_suffix("hel") #=> "lo"
 *     "hello".delete_suffix("llo") #=> "hello"
 */
static mrb_value
str_del_suffix(mrb_state *mrb, mrb_value self)
{
  mrb_int plen;
  const char *ptr;

  mrb_get_args(mrb, "s", &ptr, &plen);
  mrb_int slen = RSTRING_LEN(self);
  if (plen > slen) return mrb_str_dup(mrb, self);
  if (memcmp(RSTRING_PTR(self)+slen-plen, ptr, plen) != 0)
    return mrb_str_dup(mrb, self);
  return mrb_str_substr(mrb, self, 0, slen-plen);
}

#define lesser(a,b) (((a)>(b))?(b):(a))

/*
 * call-seq:
 *   str.casecmp(other_str)   -> -1, 0, +1 or nil
 *
 * Case-insensitive version of <code>String#<=></code>.
 *
 *   "abcdef".casecmp("abcde")     #=> 1
 *   "aBcDeF".casecmp("abcdef")    #=> 0
 *   "abcdef".casecmp("abcdefg")   #=> -1
 *   "abcdef".casecmp("ABCDEF")    #=> 0
 */
static mrb_value
str_casecmp(mrb_state *mrb, mrb_value self)
{
  mrb_value str = mrb_get_arg1(mrb);

  if (!mrb_string_p(str)) return mrb_nil_value();

  struct RString *s1 = mrb_str_ptr(self);
  struct RString *s2 = mrb_str_ptr(str);

  mrb_int len1 = RSTR_LEN(s1);
  mrb_int len2 = RSTR_LEN(s2);
  mrb_int len = lesser(len1, len2);
  char *p1 = RSTR_PTR(s1);
  char *p2 = RSTR_PTR(s2);
  if (p1 == p2) return mrb_fixnum_value(0);

  for (mrb_int i=0; i<len; i++) {
    int c1 = p1[i], c2 = p2[i];
    if (ISASCII(c1) && ISUPPER(c1)) c1 = TOLOWER(c1);
    if (ISASCII(c2) && ISUPPER(c2)) c2 = TOLOWER(c2);
    if (c1 > c2) return mrb_fixnum_value(1);
    if (c1 < c2) return mrb_fixnum_value(-1);
  }
  if (len1 == len2) return mrb_fixnum_value(0);
  if (len1 > len2)  return mrb_fixnum_value(1);
  return mrb_fixnum_value(-1);
}

/*
 * call-seq:
 *   str.casecmp?(other)  -> true, false, or nil
 *
 * Returns true if str and other_str are equal after case folding,
 * false if they are not equal, and nil if other is not a string.
 */
static mrb_value
str_casecmp_p(mrb_state *mrb, mrb_value self)
{
  mrb_value c = str_casecmp(mrb, self);
  if (mrb_nil_p(c)) return c;
  return mrb_bool_value(mrb_fixnum(c) == 0);
}

static mrb_value
str_lines(mrb_state *mrb, mrb_value self)
{
  mrb_value result;
  mrb_int len;
  char *b = RSTRING_PTR(self);
  char *p = b, *t;
  char *e = b + RSTRING_LEN(self);

  mrb->c->ci->mid = 0;
  result = mrb_ary_new(mrb);
  int ai = mrb_gc_arena_save(mrb);
  while (p < e) {
    t = p;
    while (p < e && *p != '\n') p++;
    if (*p == '\n') p++;
    len = (mrb_int) (p - t);
    mrb_ary_push(mrb, result, mrb_str_new(mrb, t, len));
    mrb_gc_arena_restore(mrb, ai);
  }
  return result;
}

/*
 * call-seq:
 *   +string -> new_string or self
 *
 * Returns +self+ if +self+ is not frozen.
 *
 * Otherwise returns <tt>self.dup</tt>, which is not frozen.
 */
static mrb_value
str_uplus(mrb_state *mrb, mrb_value str)
{
  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    return mrb_str_dup(mrb, str);
  }
  else {
    return str;
  }
}

/*
 * call-seq:
 *   -string -> frozen_string
 *
 * Returns a frozen, possibly pre-existing copy of the string.
 *
 */
static mrb_value
str_uminus(mrb_state *mrb, mrb_value str)
{
  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    return str;
  }
  return mrb_obj_freeze(mrb, mrb_str_dup(mrb, str));
}

static mrb_value
str_ascii_only_p(mrb_state *mrb, mrb_value str)
{
  struct RString *s = mrb_str_ptr(str);
  const char *p = RSTR_PTR(s);
  const char *e = p + RSTR_LEN(s);

  while (p < e) {
    if (*p & 0x80) return mrb_false_value();
    p++;
  }
  mrb_str_ptr(str)->flags |= MRB_STR_SINGLE_BYTE;
  return mrb_true_value();
}

static mrb_value
str_b(mrb_state *mrb, mrb_value self)
{
  mrb_value str = mrb_str_dup(mrb, self);
  mrb_str_ptr(str)->flags |= MRB_STR_BINARY;
  return str;
}

void
mrb_mruby_string_ext_gem_init(mrb_state* mrb)
{
  struct RClass *s = mrb->string_class;

  mrb_define_method_id(mrb, s, MRB_SYM(dump),             mrb_str_dump,        MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM_B(swapcase),       str_swapcase_bang,   MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(swapcase),         str_swapcase,        MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(concat),           str_concat_m,        MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_OPSYM(lshift),         str_concat_m,        MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(append_as_bytes),  str_append_as_bytes, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(count),            str_count,           MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(tr),               str_tr_m,            MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, s, MRB_SYM_B(tr),             str_tr_bang,         MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, s, MRB_SYM(tr_s),             str_tr_s,            MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, s, MRB_SYM_B(tr_s),           str_tr_s_bang,       MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, s, MRB_SYM(squeeze),          str_squeeze_m,       MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, s, MRB_SYM_B(squeeze),        str_squeeze_bang,    MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, s, MRB_SYM(delete),           str_delete_m,        MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM_B(delete),         str_delete_bang,     MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM_Q(start_with),     str_start_with,      MRB_ARGS_REST());
  mrb_define_method_id(mrb, s, MRB_SYM_Q(end_with),       str_end_with,        MRB_ARGS_REST());
  mrb_define_method_id(mrb, s, MRB_SYM(hex),              str_hex,             MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(oct),              str_oct,             MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(chr),              str_chr,             MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(succ),             str_succ,            MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM_B(succ),           str_succ_bang,       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(next),             str_succ,            MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM_B(next),           str_succ_bang,       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(ord),              str_ord,             MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM_B(delete_prefix),  str_del_prefix_bang, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(delete_prefix),    str_del_prefix,      MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM_B(delete_suffix),  str_del_suffix_bang, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(delete_suffix),    str_del_suffix,      MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM(casecmp),          str_casecmp,         MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM_Q(casecmp),        str_casecmp_p,       MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_OPSYM(plus),           str_uplus,           MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_OPSYM(minus),          str_uminus,          MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, s, MRB_SYM_Q(ascii_only),     str_ascii_only_p,    MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(b),                str_b,               MRB_ARGS_NONE());

  mrb_define_method_id(mrb, s, MRB_SYM(__lines),          str_lines,           MRB_ARGS_NONE());
  mrb_define_method_id(mrb, s, MRB_SYM(__codepoints),     str_codepoints,      MRB_ARGS_NONE());

  mrb_define_method_id(mrb, mrb->integer_class, MRB_SYM(chr), int_chr, MRB_ARGS_OPT(1));
}

void
mrb_mruby_string_ext_gem_final(mrb_state* mrb)
{
}
