/* Autogenerated by the KL Compiler */
#include "klc_prelude.h"
#include <stdarg.h>
#include <string.h>

#define KLC_MAX_STACK_SIZE 1000

#if KLC_POSIX
#include <errno.h>
#endif

/* For now we assume that
 * we always want command line programs.
 * But in the future we can enable
 * KLC_WIN_APP to create windows GUI programs
 */
#if KLC_OS_WINDOWS
#define KLC_WIN_APP 0
#endif

#if KLC_WIN_APP
#pragma comment(lib, "shell32.lib")
static LPCWSTR KLC_lpCmdLine;
#else
static int KLC_argc;
static const char** KLC_argv;
#endif

static size_t KLC_stacktrace_size = 0;
static KLC_stack_frame KLC_stacktrace[KLC_MAX_STACK_SIZE];

static size_t KLC_release_on_exit_buffer_cap = 0;
static size_t KLC_release_on_exit_buffer_size = 0;
static KLC_header** KLC_release_on_exit_buffer = NULL;

const KLC_var KLC_null = {
  KLC_TAG_OBJECT,
  { NULL }
};

static const char KLC_utf8_bom[] = "\xEF\xBB\xBF";

static size_t KLC_utf8_pointsize(char sc) {
  unsigned char c = (unsigned char) sc;
  if (c <= 0x7F) {
    return 1;
  } else if (c >= 0xC0 && c <= 0xDF) {
    return 2;
  } else if (c >= 0xE0 && c <= 0xEF) {
    return 3;
  } else if (c >= 0xF0 && c <= 0xF7) {
    return 4;
  }
  /* Invalid */
  return 0;
}

static size_t KLC_utf8_size_for_point(KLC_char32 c) {
  if (c <= 0x7F) {
    return 1;
  } else if (c >= 0x80 && c <= 0x7FF) {
    return 2;
  } else if (c >= 0x800 && c <= 0xFFFF) {
    return 3;
  } else if (c >= 0x10000 && c <= 0x10FFFF) {
    return 4;
  }
  /* INVALID */
  return 0;
}

static KLC_char32 KLC_utf8_read_next(const char* s, size_t len) {
  size_t i;
  KLC_char32 c = (unsigned char) *s++;
  switch (len) {
    case 2:
      c &= ((1 << 5) - 1);
      break;
    case 3:
      c &= ((1 << 4) - 1);
      break;
    case 4:
      c &= ((1 << 3) - 1);
      break;
  }
  for (i = 1; i < len; i++, s++) {
    c <<= 6;
    c |= ((unsigned char) *s) & ((1 << 6) - 1);
  }
  if (KLC_utf8_size_for_point(c) != len) {
    KLC_errorf("FUBAR: Incorrectly read string");
  }
  return c;
}

static size_t KLC_utf8_write_next(char* s, KLC_char32 c) {
  size_t len = KLC_utf8_size_for_point(c), i;
  if (!len) {
    /* INVALID */
    return 0;
  }
  for (i = 1; i < len; i++) {
    s[len - i] = (1 << 7 | (c % (1 << 6)));
    c >>= 6;
  }
  switch (len) {
    case 1:
      *s = c;
      break;
    case 2:
      *s = (11 << 6) | c;
      break;
    case 3:
      *s = (111 << 5) | c;
      break;
    case 4:
      *s = (1111 << 4) | c;
      break;
    default:
      KLC_errorf("KLC_utf8_write_next: FUBAR");
  }
  return len;
}

static KLC_bool KLC_utf8_has_bom(const char* s) {
  return strncmp(s, KLC_utf8_bom, 3) == 0;
}

static KLC_char32* KLC_utf8_to_utf32(const char* s, size_t bytesize, size_t* size) {
  size_t i = 0, len = 0;
  KLC_char32* buf = (KLC_char32*) malloc(sizeof(KLC_char32) * (bytesize + 1));
  while (i < bytesize) {
    size_t ps = KLC_utf8_pointsize(s[i]);
    if (ps == 0 || i + ps > bytesize) {
      /* TODO: Invalid utf-8 encoding, better error handling */
      free(buf);
      return NULL;
    }
    buf[len++] = KLC_utf8_read_next(s + i, ps);
    i += ps;
  }
  buf[len] = 0;
  *size = len;
  return buf;
}

void KLC_push_frame(const char* filename, const char* function, long ln) {
  KLC_stacktrace[KLC_stacktrace_size].filename = filename;
  KLC_stacktrace[KLC_stacktrace_size].function = function;
  KLC_stacktrace[KLC_stacktrace_size].lineno = ln;
  KLC_stacktrace_size++;
  if (KLC_stacktrace_size == KLC_MAX_STACK_SIZE) {
    KLC_errorf("stackoverflow");
  }
}

void KLC_pop_frame() {
  KLC_stacktrace_size--;
}

const char* KLC_tag_to_string(int tag) {
  switch (tag) {
    case KLC_TAG_BOOL: return "BOOL";
    case KLC_TAG_INT: return "INT";
    case KLC_TAG_DOUBLE: return "DOUBLE";
    case KLC_TAG_FUNCTION: return "FUNCTION";
    case KLC_TAG_TYPE: return "TYPE";
    case KLC_TAG_OBJECT: return "OBJECT";
  }
  KLC_errorf("tag_to_string: invalid tag %d", tag);
  return NULL;
}

void KLC_release_object_on_exit(KLC_header* obj) {
  if (KLC_release_on_exit_buffer_size >= KLC_release_on_exit_buffer_cap) {
    KLC_release_on_exit_buffer_cap += 10;
    KLC_release_on_exit_buffer_cap *= 2;
    KLC_release_on_exit_buffer =
      (KLC_header**) realloc(
        KLC_release_on_exit_buffer,
        sizeof(KLC_header*) * KLC_release_on_exit_buffer_cap);
  }
  KLC_release_on_exit_buffer[KLC_release_on_exit_buffer_size++] = obj;
}

void KLC_release_var_on_exit(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_release_object_on_exit(v.u.obj);
  }
}

void KLC_release_queued_before_exit() {
  size_t i = 0;
  for (; i < KLC_release_on_exit_buffer_size; i++) {
    KLC_release(KLC_release_on_exit_buffer[i]);
  }
  free(KLC_release_on_exit_buffer);
}

void KLC_errorf(const char* fmt, ...) {
  size_t i;
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  for (i = 0; i < KLC_stacktrace_size; i++) {
    KLC_stack_frame *frame = KLC_stacktrace + i;
    fprintf(
      stderr,
      "  %s %s line %lu\n",
      frame->function,
      frame->filename,
      frame->lineno);
  }
  exit(1);
}

void KLC_retain(KLC_header *obj) {
  if (obj) {
    obj->refcnt++;
  }
}

void KLC_retain_var(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_retain(v.u.obj);
  }
}

void KLC_partial_release(KLC_header* obj, KLC_header** delete_queue) {
  if (obj) {
    if (obj->refcnt) {
      obj->refcnt--;
    } else {
      if (obj->weakref) {
        obj->weakref->obj = NULL;
        obj->weakref = NULL;
      }
      obj->next = *delete_queue;
      *delete_queue = obj;
    }
  }
}

void KLC_partial_release_var(KLC_var v, KLC_header** delete_queue) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_partial_release(v.u.obj, delete_queue);
  }
}

void KLC_release(KLC_header *obj) {
  KLC_header* delete_queue = NULL;
  KLC_partial_release(obj, &delete_queue);
  while (delete_queue) {
    obj = delete_queue;
    delete_queue = delete_queue->next;
    obj->type->deleter(obj, &delete_queue);
    free(obj);
  }
}

void KLC_release_var(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_release(v.u.obj);
  }
}

KLC_int KLCNZAIdentityHash(KLC_var x) {
  switch (x.tag) {
    case KLC_TAG_FUNCTION:
      return (KLC_int) x.u.f;
    case KLC_TAG_TYPE:
      return (KLC_int) x.u.t;
    case KLC_TAG_OBJECT:
      return (KLC_int) x.u.obj;
  }
  KLC_errorf("_IdentityHash unsupported for %s", KLC_tag_to_string(x.tag));
  return 0;
}

KLC_bool KLCNZAIs(KLC_var a, KLC_var b) {
  if (a.tag != b.tag) {
    return 0;
  }
  switch (a.tag) {
    case KLC_TAG_BOOL:
    case KLC_TAG_INT:
      return a.u.i == b.u.i;
    case KLC_TAG_DOUBLE:
      return a.u.d == b.u.d;
    case KLC_TAG_FUNCTION:
      return a.u.f == b.u.f;
    case KLC_TAG_TYPE:
      return a.u.t == b.u.t;
    case KLC_TAG_OBJECT:
      return a.u.obj == b.u.obj;
  }
  KLC_errorf("_Is: invalid tag: %d", a.tag);
  return 0;
}

KLC_bool KLCNZAIsNot(KLC_var a, KLC_var b) {
  return !KLCNZAIs(a, b);
}

KLC_bool KLCNZAEq(KLC_var a, KLC_var b) {
  if (a.tag == b.tag) {
    switch (a.tag) {
      case KLC_TAG_BOOL:
        return !!a.u.i == !!b.u.i;
      case KLC_TAG_INT:
        return a.u.i == b.u.i;
      case KLC_TAG_DOUBLE:
        return a.u.d == b.u.d;
      case KLC_TAG_FUNCTION:
        return a.u.f == b.u.f;
      case KLC_TAG_TYPE:
        return a.u.t == b.u.t;
      case KLC_TAG_OBJECT:
        if (a.u.obj == b.u.obj) {
          return 1;
        } else if (a.u.obj && b.u.obj && a.u.obj->type == b.u.obj->type) {
          KLC_var args[2];
          KLC_var r;
          KLC_bool br;
          args[0] = a;
          args[1] = b;
          r = KLC_mcall("Eq", 2, args);
          br = KLC_truthy(r);
          KLC_release_var(r);
          return br;
        } else {
          return 0;
        }
      default:
        KLC_errorf("_Eq: invalid tag: %d", a.tag);
    }
  } else if (a.tag == KLC_TAG_INT && b.tag == KLC_TAG_DOUBLE) {
    return a.u.i == b.u.d;
  } else if (a.tag == KLC_TAG_DOUBLE && b.tag == KLC_TAG_INT) {
    return a.u.d == b.u.i;
  }
  return 0;
}

KLC_bool KLCNZANe(KLC_var a, KLC_var b) {
  return !KLCNZAEq(a, b);
}

KLC_bool KLCNZALt(KLC_var a, KLC_var b) {
  if (a.tag == b.tag) {
    switch (a.tag) {
      case KLC_TAG_INT:
        return a.u.i < b.u.i;
      case KLC_TAG_DOUBLE:
        return a.u.d < b.u.d;
    }
  } else if (a.tag == KLC_TAG_INT && b.tag == KLC_TAG_DOUBLE) {
    return a.u.i < b.u.d;
  } else if (a.tag == KLC_TAG_DOUBLE && b.tag == KLC_TAG_INT) {
    return a.u.d < b.u.i;
  }
  {
    KLC_var args[2];
    args[0] = a;
    args[1] = b;
    return KLC_var_to_bool(KLC_mcall("Lt", 2, args));
  }
}

KLC_bool KLCNZAGt(KLC_var a, KLC_var b) {
  return KLCNZALt(b, a);
}

KLC_bool KLCNZALe(KLC_var a, KLC_var b) {
  return !KLCNZALt(b, a);
}

KLC_bool KLCNZAGe(KLC_var a, KLC_var b) {
  return !KLCNZALt(a, b);
}

KLC_bool KLCNbool(KLC_var v) {
  return KLC_truthy(v);
}

KLC_type KLCNtype(KLC_var v) {
  switch (v.tag) {
    case KLC_TAG_BOOL:
      return &KLC_typebool;
    case KLC_TAG_INT:
      return &KLC_typeint;
    case KLC_TAG_DOUBLE:
      return &KLC_typedouble;
    case KLC_TAG_FUNCTION:
      return &KLC_typefunction;
    case KLC_TAG_TYPE:
      return &KLC_typetype;
    case KLC_TAG_OBJECT:
      return v.u.obj ? v.u.obj->type : &KLC_typenull;
  }
  KLC_errorf("Unrecognized type tag %d", v.tag);
  return NULL;
}

KLC_var KLC_bool_to_var(KLC_bool b) {
  KLC_var ret;
  ret.tag = KLC_TAG_BOOL;
  ret.u.i = b;
  return ret;
}

KLC_var KLC_int_to_var(KLC_int i) {
  KLC_var ret;
  ret.tag = KLC_TAG_INT;
  ret.u.i = i;
  return ret;
}

KLC_var KLC_double_to_var(double d) {
  KLC_var ret;
  ret.tag = KLC_TAG_DOUBLE;
  ret.u.d = d;
  return ret;
}

KLC_var KLC_function_to_var(KLC_function f) {
  KLC_var ret;
  ret.tag = KLC_TAG_FUNCTION;
  ret.u.f = f;
  return ret;
}

KLC_var KLC_type_to_var(KLC_type t) {
  KLC_var ret;
  ret.tag = KLC_TAG_TYPE;
  ret.u.t = t;
  return ret;
}

KLC_var KLC_object_to_var(KLC_header* obj) {
  KLC_var ret;
  ret.tag = KLC_TAG_OBJECT;
  ret.u.obj = obj;
  return ret;
}

KLC_bool KLC_var_to_bool(KLC_var v) {
  /* TODO: Better error message */
  if (v.tag != KLC_TAG_BOOL) {
    KLC_errorf("var_to_bool: expected BOOL but got %s",
               KLC_tag_to_string(v.tag));
  }
  return v.u.i ? 1 : 0;
}

KLC_bool KLC_truthy(KLC_var v) {
  switch (v.tag) {
    case KLC_TAG_BOOL:
    case KLC_TAG_INT:
      return v.u.i;
    case KLC_TAG_DOUBLE:
      return v.u.d;
    case KLC_TAG_FUNCTION:
    case KLC_TAG_TYPE:
      return 1;
    case KLC_TAG_OBJECT:
      return KLC_var_to_bool(KLC_mcall("Bool", 1, &v));
  }
  KLC_errorf("truthy: invalid tag %d", v.tag);
  return 0;
}

KLC_int KLC_var_to_int(KLC_var v) {
  /* TODO: Better error message */
  if (v.tag != KLC_TAG_INT) {
    KLC_errorf("var_to_int: expected INT but got %s",
               KLC_tag_to_string(v.tag));
  }
  return v.u.i;
}

double KLC_var_to_double(KLC_var v) {
  /* TODO: Better error message */
  if (v.tag != KLC_TAG_DOUBLE) {
    KLC_errorf("var_to_double: expected DOBULE but got %s",
               KLC_tag_to_string(v.tag));
  }
  return v.u.d;
}

KLC_function KLC_var_to_function(KLC_var v) {
  if (v.tag != KLC_TAG_FUNCTION) {
    KLC_errorf("var_to_type: expected FUNCTION but got %s",
               KLC_tag_to_string(v.tag));
  }
  return v.u.f;
}

KLC_type KLC_var_to_type(KLC_var v) {
  /* TODO: Better error message */
  if (v.tag != KLC_TAG_TYPE) {
    KLC_errorf("var_to_type: expected TYPE but got %s",
               KLC_tag_to_string(v.tag));
  }
  return v.u.t;
}

KLC_header* KLC_var_to_object(KLC_var v, KLC_type ti) {
  /* TODO: Better error message */
  KLC_header* ret;
  if (v.tag != KLC_TAG_OBJECT) {
    KLC_errorf("var_to_object: not an object");
  }
  if (ti != KLCNtype(v)) {
    KLC_type actual_type = KLCNtype(v);
    KLC_errorf(
      "var_to_object: expected type %s but got type %s",
      ti->name,
      actual_type->name);
  }
  return v.u.obj;
}

KLC_var KLC_var_call(KLC_var f, int argc, KLC_var* argv) {
  if (f.tag == KLC_TAG_FUNCTION) {
    return f.u.f->body(argc, argv);
  } else {
    KLC_var* argv2 = (KLC_var*) malloc(sizeof(KLC_var) * (argc + 1));
    int i;
    KLC_var result;
    argv2[0] = f;
    for (i = 0; i < argc; i++) {
      argv2[i + 1] = argv[i];
    }
    result = KLC_mcall("Call", argc + 1, argv2);
    free(argv2);
    return result;
  }
}

KLC_var KLC_mcall(const char* name, int argc, KLC_var* argv) {
  if (argc == 0) {
    KLC_errorf("mcall requires at least 1 arg");
  }
  {
    KLC_type type = KLCNtype(argv[0]);
    const KLC_methodlist* mlist = type->methods;
    size_t len = mlist->size;
    const KLC_methodinfo* mbuf = mlist->methods;
    const KLC_methodinfo* m = NULL;
    size_t i;
    /* TODO: Faster method dispatch mechanism */
    if (len) {
      int cmp = strcmp(name, mbuf[0].name);
      if (cmp == 0) {
        m = mbuf;
      } else if (cmp > 0) {
        size_t lower = 0;
        size_t upper = len;
        while (lower + 1 < upper) {
          size_t mid = (lower + upper) / 2;
          cmp = strcmp(name, mbuf[mid].name);
          if (cmp == 0) {
            m = mbuf + mid;
            break;
          } else if (cmp < 0) {
            upper = mid;
          } else {
            lower = mid;
          }
        }
      }
    }
    if (!m) {
      KLC_errorf("No method '%s' for type '%s'", name, type->name);
    }
    return m->body(argc, argv);
  }
}

KLC_int KLCNintZFOr(KLC_int a, KLC_int b) {
  return a | b;
}

KLC_int KLCNintZFAnd(KLC_int a, KLC_int b) {
  return a & b;
}

KLC_int KLCNintZFXor(KLC_int a, KLC_int b) {
  return a ^ b;
}

KLC_int KLCNintZFAdd(KLC_int a, KLC_int b) {
  return a + b;
}

KLC_int KLCNintZFSub(KLC_int a, KLC_int b) {
  return a - b;
}

KLC_int KLCNintZFMul(KLC_int a, KLC_int b) {
  return a * b;
}

KLC_int KLCNintZFDiv(KLC_int a, KLC_int b) {
  return a / b;
}

KLC_int KLCNintZFMod(KLC_int a, KLC_int b) {
  return a % b;
}

KLC_bool KLCNintZFEq(KLC_int a, KLC_int b) {
  return a == b;
}

KLC_bool KLCNintZFNe(KLC_int a, KLC_int b) {
  return a != b;
}

KLC_bool KLCNintZFLt(KLC_int a, KLC_int b) {
  return a < b;
}

KLCNString* KLCNintZFRepr(KLC_int i) {
  char buffer[128];
  sprintf(buffer, KLC_INT_FMT, i);
  return KLC_mkstr(buffer);
}

double KLCNdoubleZFAdd(double a, double b) {
  return a + b;
}

double KLCNdoubleZFSub(double a, double b) {
  return a - b;
}

double KLCNdoubleZFMul(double a, double b) {
  return a * b;
}

double KLCNdoubleZFDiv(double a, double b) {
  return a / b;
}

KLC_bool KLCNdoubleZFEq(double a, double b) {
  return a == b;
}

KLC_bool KLCNdoubleZFNe(double a, double b) {
  return a != b;
}

KLC_bool KLCNdoubleZFLt(double a, double b) {
  return a < b;
}

KLC_int KLCNdoubleZFHashCode(double a) {
  /* TODO: Come up with a better hashcode algorithm */
  /* The ideas behind hashing doubles this way:
   *   1. because of the equality relationship with int,
   *      floating point numbers with no fractional part
   *      needs to have a hashcode same as the integer
   *      version.
   *   2. simply rounding to a close integer isn't necessarily
   *      bad, but the problem is that this normally
   *      would result in any two doubles with same
   *      integer parts to land in the same bucket.
   *      we mitigate this by shfiting up by some number
   *      of bits.
   */
  KLC_int one = 1;
  return
    (a == (KLC_int) a) ? ((KLC_int) a) :
    (a <= (one << 32)) ? ((KLC_int) ((one << 16) * a)) :
    ((KLC_int) a);
}

KLCNString* KLCNdoubleZFRepr(double d) {
  char buffer[80];
  sprintf(buffer, "%f", d);
  return KLC_mkstr(buffer);
}

KLCNString* KLCNfunctionZFGETname(KLC_function f) {
  return KLC_mkstr(f->name);
}

KLCNString* KLCNtypeZFGETname(KLC_type t) {
  return KLC_mkstr(t->name);
}

KLC_bool KLCNtypeZFEq(KLC_type a, KLC_type b) {
  return a == b;
}

void KLC_init_header(KLC_header* header, KLC_type type) {
  header->type = type;
  header->refcnt = 0;
  header->next = NULL;
  header->weakref = NULL;
}

void KLC_deleteWeakReference(KLC_header* robj, KLC_header** dq) {
  KLCNWeakReference* wr = (KLCNWeakReference*) robj;
  if (wr->obj) {
    wr->obj->weakref = NULL;
    wr->obj = NULL;
  }
}

KLCNWeakReference* KLCNWeakReferenceZEnew(KLC_var v) {
  KLCNWeakReference* wr = (KLCNWeakReference*) malloc(sizeof(KLCNWeakReference));
  KLC_init_header(&wr->header, &KLC_typeWeakReference);
  if (v.tag != KLC_TAG_OBJECT) {
    KLC_errorf(
      "WeakReference requires an OBJECT value but got %s",
      KLC_tag_to_string(v.tag));
  }
  /* Don't retain v. The whole point is for this reference to be weak */
  wr->obj = v.u.obj;
  if (wr->obj) {
    wr->obj->weakref = wr;
  }
  return wr;
}

KLC_var KLCNWeakReferenceZFgetNullable(KLCNWeakReference* wr) {
  KLC_retain(wr->obj); /* retain to use as return */
  return KLC_object_to_var(wr->obj);
}

KLC_int KLCNWeakReferenceZFGETrefcnt(KLCNWeakReference* wr) {
  return wr->obj ? (wr->obj->refcnt + 1) : 0;
}

void KLC_deleteString(KLC_header* robj, KLC_header** dq) {
  KLCNString* obj = (KLCNString*) robj;
  free(obj->utf8);
  free(obj->utf32);
  #if KLC_OS_WINDOWS
    free(obj->wstr);
  #endif
}

int KLC_check_ascii(const char* str) {
  while (*str) {
    if (((unsigned) *str) >= 128) {
      return 0;
    }
    str++;
  }
  return 1;
}

KLCNString* KLC_mkstr_with_buffer(size_t bytesize, char* str, int is_ascii) {
  KLCNString* obj = (KLCNString*) malloc(sizeof(KLCNString));
  KLC_init_header(&obj->header, &KLC_typeString);
  obj->bytesize = bytesize;
  obj->nchars = is_ascii ? bytesize : 0;
  obj->utf8 = str;
  obj->utf32 = NULL;
  #if KLC_OS_WINDOWS
    obj->wstr = NULL;
  #endif
  obj->hash = 0;
  obj->is_ascii = is_ascii;
  return obj;
}

#if KLC_OS_WINDOWS
  KLCNString* KLC_windows_string_from_wstr_buffer(LPWSTR s) {
    size_t bufsize = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char* buf = (char*) malloc(bufsize);
    KLCNString* ret;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, bufsize, NULL, NULL);
    ret = KLC_mkstr_with_buffer(bufsize - 1, buf, KLC_check_ascii(buf));
    ret->wstr = s;
    return ret;
  }

  KLCNString* KLC_windows_string_from_wstr(LPCWSTR s) {
    LPWSTR buf = (LPWSTR) malloc((wcslen(s) + 1) * 2);
    wcscpy(buf, s);
    return KLC_windows_string_from_wstr_buffer(buf);
  }

  LPCWSTR KLC_windows_get_wstr(KLCNString* s) {
    if (!s->wstr) {
      size_t bufsize = MultiByteToWideChar(CP_UTF8, 0, s->utf8, -1, NULL, 0);
      if (bufsize == 0) {
        KLC_errorf("Windows: UTF-8 to UTF-16 conversion failed");
      }
      s->wstr = (LPWSTR) malloc(2 * bufsize);
      MultiByteToWideChar(CP_UTF8, 0, s->utf8, -1, s->wstr, bufsize);
    }
    return s->wstr;
  }
#endif

KLCNString* KLC_mkstr(const char *str) {
  size_t len = strlen(str);
  char* buffer = (char*) malloc(sizeof(char) * (len + 1));
  strcpy(buffer, str);
  return KLC_mkstr_with_buffer(len, buffer, KLC_check_ascii(buffer));
}

static void KLC_String_init_utf32(KLCNString* s) {
  if (s->bytesize && !s->nchars) {
    s->utf32 = KLC_utf8_to_utf32(s->utf8, s->bytesize, &s->nchars);
  }
}

KLC_int KLCNStringZFGETbytesize(KLCNString* s) {
  return (KLC_int) s->bytesize;
}

KLC_int KLCNStringZFGETsize(KLCNString* s) {
  KLC_String_init_utf32(s);
  return (KLC_int) s->nchars;
}

KLC_int KLCNStringZFHashCode(KLCNString* s) {
  if (s->hash == 0 && s->bytesize != 0) {
    KLC_int h = 0;
    const char* utf8 = s->utf8;
    size_t i, len = s->bytesize;
    for (i = 0; i < len; i++) {
      h = 31 * h + (KLC_int) utf8[i];
    }
    if (h == 0) {
      h = 60761;
    }
    s->hash = h;
  }
  return s->hash;
}

KLCNString* KLCNStringZFStr(KLCNString* s) {
  KLC_retain((KLC_header*) s);
  return s;
}

KLCNString* KLCNStringZFescape(KLCNString* str) {
  size_t i = 0, j = 0, bs = str->bytesize;
  char* buffer = (char*) malloc(sizeof(char) * (2 * bs + 1));
  char* s = str->utf8;

  while (i < bs) {
    switch (s[i]) {
      case '\n':
        buffer[j++] = '\\';
        buffer[j++] = 'n';
        i++;
        break;
      case '\t':
        buffer[j++] = '\\';
        buffer[j++] = 't';
        i++;
        break;
      case '\r':
        buffer[j++] = '\\';
        buffer[j++] = 'r';
        i++;
        break;
      case '\f':
        buffer[j++] = '\\';
        buffer[j++] = 'f';
        i++;
        break;
      case '\v':
        buffer[j++] = '\\';
        buffer[j++] = 'v';
        i++;
        break;
      case '\0':
        buffer[j++] = '\\';
        buffer[j++] = '0';
        i++;
        break;
      case '\a':
        buffer[j++] = '\\';
        buffer[j++] = 'a';
        i++;
        break;
      case '\b':
        buffer[j++] = '\\';
        buffer[j++] = 'b';
        i++;
        break;
      case '\"':
        buffer[j++] = '\\';
        buffer[j++] = '\"';
        i++;
        break;
      case '\'':
        buffer[j++] = '\\';
        buffer[j++] = '\'';
        i++;
        break;
      default:
        buffer[j++] = s[i++];
    }
  }

  buffer = (char*) realloc(buffer, sizeof(char) * (j + 1));
  buffer[j] = '\0';

  return KLC_mkstr_with_buffer(j, buffer, KLC_check_ascii(buffer));
}

KLCNString* KLCNStringZFAdd(KLCNString* a, KLCNString* b) {
  size_t bytesize = a->bytesize + b->bytesize;
  char* buffer = (char*) malloc(sizeof(char) * (bytesize + 1));
  strcpy(buffer, a->utf8);
  strcpy(buffer + a->bytesize, b->utf8);
  return KLC_mkstr_with_buffer(bytesize, buffer, a->is_ascii && b->is_ascii);
}

KLC_bool KLCNStringZFEq(KLCNString* a, KLCNString* b) {
  return a->bytesize == b->bytesize && strcmp(a->utf8, b->utf8) == 0;
}

KLC_bool KLCNStringZFLt(KLCNString* a, KLCNString* b) {
  return strcmp(a->utf8, b->utf8) < 0;
}

KLCNString* KLCNStringZFSlice(KLCNString* s, KLC_int a, KLC_int b) {
  size_t len;
  KLC_String_init_utf32(s);
  len = s->nchars;
  if (a < 0) {
    a += len;
  }
  if (a < 0 || a >= len) {
    KLC_errorf(
        "start index out of bounds (a = "
        KLC_INT_FMT ", len = " KLC_INT_FMT ")",
        a, len);
  }
  if (b < 0) {
    b += len;
  }
  if (b < 0 || b > len) {
    KLC_errorf(
        "end index out of bounds (b = "
        KLC_INT_FMT ", len = " KLC_INT_FMT ")",
        b, len);
  }
  if (a >= b) {
    return KLC_mkstr("");
  }
  if (s->is_ascii) {
    char* buffer = (char*) malloc(sizeof(char) * (b - a + 1));
    size_t i = a, j = 0;
    while (i < b) {
      buffer[j++] = s->utf8[i++];
    }
    buffer[j] = '\0';
    return KLC_mkstr_with_buffer(j, buffer, 1);
  } else {
    char* buffer = (char*) malloc(sizeof(KLC_char32) * (b - a + 1));
    int is_ascii = 1;
    size_t i = a, j = 0;
    while (i < b) {
      size_t d = KLC_utf8_write_next(buffer + j, s->utf32[i++]);
      if (!d) {
        /* KLC_errorf("FUBAR: Invalid unicode codepoint"); */
        KLC_errorf("FUBAR: Invalid unicode codepoint %lu", s->utf32[i - 1]);
      }
      if (d > 1) {
        is_ascii = 0;
      }
      j += d;
    }
    buffer[j] = '\0';
    buffer = (char*) realloc(buffer, sizeof(char) * (j + 1));
    return KLC_mkstr_with_buffer(j, buffer, is_ascii);
  }
}

KLCNBuffer* KLCNStringZFencodeUtf8(KLCNString* s) {
  char* buf = (char*) malloc(sizeof(char) * s->bytesize);
  memcpy(buf, s->utf8, s->bytesize);
  return KLC_mkbuf(s->bytesize, buf);
}

KLC_int KLCNCHARZABITZEinit() {
  return (KLC_int) CHAR_BIT;
}

KLC_int KLCNINTZAMAXZEinit() {
  return (KLC_int) KLC_INT_MAX;
}

KLC_int KLCNINTZAMINZEinit() {
  return (KLC_int) KLC_INT_MIN;
}

KLCNBuffer* KLC_mkbuf(KLC_int size, char* buf) {
  KLCNBuffer* obj = (KLCNBuffer*) malloc(sizeof(KLCNBuffer));
  KLC_init_header(&obj->header, &KLC_typeBuffer);
  obj->size = size;
  obj->buf = buf;
  return obj;
}

KLCNBuffer* KLCNBufferZEnew(KLC_int size) {
  return KLC_mkbuf(
    size,
    size == 0 ? NULL : (char*) calloc(sizeof(char), size));
}

void KLC_deleteBuffer(KLC_header* robj, KLC_header** dq) {
  KLCNBuffer* buf = (KLCNBuffer*) robj;
  free(buf->buf);
  buf->size = 0;
  buf->buf = NULL;
}

KLCNString* KLCNBufferZFdecodeUtf8(KLCNBuffer* buf) {
  size_t bs = buf->size;
  char* buffer = (char*) malloc(sizeof(char) * (bs + 1));
  memcpy(buffer, buf->buf, bs);
  buffer[bs] = '\0';
  return KLC_mkstr_with_buffer(bs, buffer, KLC_check_ascii(buffer));
}

KLC_bool KLCNBufferZFEq(KLCNBuffer* a, KLCNBuffer* b) {
  return a->size == b->size && memcmp(a->buf, b->buf, a->size) == 0;
}

KLC_int KLCNBufferZFGETsize(KLCNBuffer* buf) {
  return (KLC_int) buf->size;
}

KLC_int KLCNBufferZFget1(KLCNBuffer* buf, KLC_int i) {
  KLC_int x;
  if (i < 0 || i >= buf->size) {
    KLC_errorf("Index out of bounds " KLC_INT_FMT, i);
  }
  x = (unsigned char) buf->buf[i];
  return x;
}

void KLCNBufferZFset1(KLCNBuffer* buf, KLC_int i, KLC_int v) {
  if (i < 0 || i >= buf->size) {
    KLC_errorf("Index out of bounds " KLC_INT_FMT, i);
  }
  buf->buf[i] = (unsigned char) v;
}

void KLCNBufferZFresize(KLCNBuffer* buf, KLC_int ns) {
  size_t old_size = buf->size;
  size_t new_size = (size_t) ns;
  buf->size = new_size;
  buf->buf = (char*) realloc(buf->buf, sizeof(char) * new_size);
  if (old_size < new_size) {
    memset(buf->buf + old_size, 0, new_size - old_size);
  }
}

void KLCNpanic(KLCNString* message) {
  KLC_errorf("%s", message->utf8);
}

KLCNStringBuilder* KLCNStringBuilderZEnew() {
  KLCNStringBuilder* obj = (KLCNStringBuilder*) malloc(sizeof(KLCNStringBuilder));
  KLC_init_header(&obj->header, &KLC_typeStringBuilder);
  obj->size = obj->cap = 0;
  obj->buffer = NULL;
  return obj;
}

void KLC_deleteStringBuilder(KLC_header* robj, KLC_header** dq) {
  KLCNStringBuilder* obj = (KLCNStringBuilder*) robj;
  free(obj->buffer);
}

void KLCNStringBuilderZFaddstr(KLCNStringBuilder* sb, KLCNString* s) {
  if (s->bytesize) {
    if (sb->size + s->bytesize + 1 > sb->cap) {
      sb->cap += s->bytesize + 16;
      sb->cap *= 2;
      sb->buffer = (char*) realloc(sb->buffer, sizeof(char) * sb->cap);
    }
    strcpy(sb->buffer + sb->size, s->utf8);
    sb->size += s->bytesize;
  }
}

KLCNString* KLCNStringBuilderZFbuild(KLCNStringBuilder* sb) {
  if (sb->size) {
    size_t bytesize = sb->size;
    char* buffer = (char*) realloc(sb->buffer, sizeof(char) * (sb->size + 1));
    sb->size = sb->cap = 0;
    sb->buffer = NULL;
    return KLC_mkstr_with_buffer(bytesize, buffer, KLC_check_ascii(buffer));
  } else {
    return KLC_mkstr("");
  }
}

KLCNList* KLC_mklist(size_t cap) {
  KLCNList* obj = (KLCNList*) malloc(sizeof(KLCNList));
  KLC_init_header(&obj->header, &KLC_typeList);
  obj->size = 0;
  obj->cap = cap;
  obj->buffer = cap ? (KLC_var*) malloc(sizeof(KLC_var) * cap) : NULL;
  return obj;
}

void KLC_deleteList(KLC_header* robj, KLC_header** dq) {
  KLCNList* list = (KLCNList*) robj;
  size_t i;
  for (i = 0; i < list->size; i++) {
    KLC_partial_release_var(list->buffer[i], dq);
  }
  free(list->buffer);
}

KLCNList* KLCNListZEnew(KLC_int size) {
  KLCNList* list = KLC_mklist((size_t) size);
  KLC_int i;
  for (i = 0; i < size; i++) {
    list->buffer[i] = KLC_null;
  }
  list->size = size;
  return list;
}

void KLCNListZFresize(KLCNList* list, KLC_int ns) {
  size_t new_size = (size_t) ns;
  while (list->size > new_size) {
    KLC_release_var(list->buffer[--list->size]);
  }
  list->cap = new_size;
  list->buffer = (KLC_var*) realloc(list->buffer, sizeof(KLC_var) * list->cap);
  while (list->size < new_size) {
    list->buffer[list->size++] = KLC_null;
  }
}

void KLCNListZFpush(KLCNList* list, KLC_var v) {
  if (list->size >= list->cap) {
    list->cap += 4;
    list->cap *= 2;
    list->buffer = (KLC_var*) realloc(list->buffer, sizeof(KLC_var) * list->cap);
  }
  list->buffer[list->size++] = v;
  KLC_retain_var(v);
}

KLC_var KLCNListZFpop(KLCNList* list) {
  if (list->size == 0) {
    KLC_errorf("pop from empty list");
  }
  return list->buffer[--list->size];
}

KLC_var KLCNListZFGetItem(KLCNList* list, KLC_int i) {
  if (i < 0) {
    i += list->size;
  }
  if (i < 0 || ((size_t) i) >= list->size) {
    KLC_errorf("Index out of bounds (i = %ld, size = %ld)", i, list->size);
  }
  KLC_retain_var(list->buffer[i]);
  return list->buffer[i];
}

KLC_var KLCNListZFSetItem(KLCNList* list, KLC_int i, KLC_var v) {
  if (i < 0) {
    i += list->size;
  }
  if (i < 0 || ((size_t) i) >= list->size) {
    KLC_errorf("Index out of bounds (i = %ld, size = %ld)", i, list->size);
  }
  KLC_retain_var(v);  /* once for attaching value to list */
  KLC_retain_var(v);  /* once more for using as return value */
  KLC_release_var(list->buffer[i]);
  list->buffer[i] = v;
  return v;
}

KLC_int KLCNListZFGETsize(KLCNList* list) {
  return (KLC_int) list->size;
}

KLC_var KLCNClosureZEnew(KLCNList* captures, KLC_function f) {
  KLCNClosure* closure = (KLCNClosure*) malloc(sizeof(KLCNClosure));
  KLC_init_header(&closure->header, &KLC_typeClosure);
  KLC_retain((KLC_header*) captures);
  closure->captures = captures;
  closure->f = f;
  return KLC_object_to_var((KLC_header*) closure);
}

void KLC_deleteClosure(KLC_header* robj, KLC_header** dq) {
  KLCNClosure* closure = (KLCNClosure*) robj;
  KLC_partial_release((KLC_header*) closure->captures, dq);
}

KLC_var KLC_untypedKLCNClosureZFCall(int argc, KLC_var* argv) {
  KLCNClosure* closure;
  if (argc < 1) {
    KLC_errorf("method call with no receiver");
  }
  closure = (KLCNClosure*) argv[0].u.obj;
  {
    int argc2 = closure->captures->size + argc - 1;
    KLC_var* argv2 = (KLC_var*) malloc(sizeof(KLC_var) * (argc2));
    KLC_var result;
    size_t ui;
    int i;
    for (ui = 0; ui < closure->captures->size; ui++) {
      argv2[ui] = closure->captures->buffer[ui];
    }
    for (i = 0; i + 1 < argc; i++) {
      argv2[closure->captures->size + i] = argv[i + 1];
    }
    result = closure->f->body(argc2, argv2);
    free(argv2);
    return result;
  }
}

KLC_bool KLC_is_valid_file_mode(const char* mode) {
  /* For now restrict file modes to just r, w, and a */
  char c = mode[0];
  return (c == 'r' || c == 'w' || c == 'a') && mode[1] == '\0';
}

KLCNFile* KLC_mkfile(FILE* cfile,
                     const char* name,
                     const char* mode,
                     KLC_bool should_close) {
  KLCNFile* file;
  if (!KLC_is_valid_file_mode(mode)) {
    KLC_errorf("Invalid file mode: %s", mode);
  }
  file = (KLCNFile*) malloc(sizeof(KLCNFile));
  KLC_init_header(&file->header, &KLC_typeFile);
  file->cfile = cfile;
  file->name = (char*) malloc(sizeof(char) * (strlen(name) + 1));
  strcpy(file->name, name);
  /* KLC_is_valid_file_mode should verify that mode fits into file->mode */
  strcpy(file->mode, mode);
  file->should_close = should_close;
  return file;
}

void KLC_deleteFile(KLC_header* robj, KLC_header** dq) {
  KLCNFile* file = (KLCNFile*) robj;
  free(file->name);
  KLCNFileZFclose(file);
}

KLCNFile* KLCNopen(KLCNString* path, KLCNString* mode) {
  FILE* cfile;
  if (!KLC_is_valid_file_mode(mode->utf8)) {
    KLC_errorf("Invalid file mode: %s", mode->utf8);
  }
  cfile = fopen(path->utf8, mode->utf8);
  if (!cfile) {
    KLC_errorf("Failed to open file at %s", path->utf8);
  }
  return KLC_mkfile(cfile, path->utf8, mode->utf8, 1);
}

void KLCNFileZFclose(KLCNFile* file) {
  if (file->cfile && file->should_close) {
    fclose(file->cfile);
    file->cfile = NULL;
  }
}

void KLCNFileZFwrite(KLCNFile* file, KLCNString* s) {
  if (!file->cfile) {
    KLC_errorf("Trying to write to closed file");
  }
  if (file->mode[0] != 'w' && file->mode[0] != 'a') {
    KLC_errorf(
        "Trying to write to file that's not in write/append mode (%s)",
        file->name);
  }
  fwrite(s->utf8, 1, s->bytesize, file->cfile);
}

KLCNString* KLCNFileZFread(KLCNFile* file) {
  size_t cap, i, read_size, last;
  char* buffer;

  if (!file->cfile) {
    KLC_errorf("Trying to read from closed file");
  }
  if (file->mode[0] != 'r') {
    KLC_errorf(
        "Trying to read from file that's not in read mode (%s)",
        file->name);
  }

  read_size = cap = 16;
  i = 0;
  buffer = (char*) malloc(sizeof(char) * cap);
  while ((last = fread(buffer + i, 1, read_size, file->cfile)) == read_size) {
    i += last;
    cap *= 2;
    read_size = cap - i;
    buffer = (char*) realloc(buffer, sizeof(char) * cap);
  }
  i += last;

  if (ferror(file->cfile) != 0) {
    KLC_errorf("Error while reading file %s", file->name);
  }

  buffer = (char*) realloc(buffer, sizeof(char) * (i + 1));
  buffer[i] = '\0';
  return KLC_mkstr_with_buffer(i, buffer, KLC_check_ascii(buffer));
}

KLCNTry* KLCNTryZEnew(KLC_bool success, KLC_var value) {
  KLCNTry* obj = (KLCNTry*) malloc(sizeof(KLCNTry));
  KLC_init_header(&obj->header, &KLC_typeTry);
  KLC_retain_var(value);
  obj->success = success;
  obj->value = value;
  return obj;
}

KLC_bool KLCNTryZFBool(KLCNTry* t) {
  return t->success;
}

KLC_var KLCNTryZFgetRawValue(KLCNTry* t) {
  KLC_retain_var(t->value);
  return t->value;
}

void KLC_deleteTry(KLC_header* robj, KLC_header** dq) {
  KLCNTry* obj = (KLCNTry*) robj;
  KLC_partial_release_var(obj->value, dq);
}

KLCNZDWith* KLCNZDWithZEnew(KLC_var v) {
  KLCNZDWith* obj = (KLCNZDWith*) malloc(sizeof(KLCNZDWith));
  KLC_init_header(&obj->header, &KLC_typeZDWith);
  KLC_retain_var(v);
  obj->value = v;
  return obj;
}

void KLC_deleteZDWith(KLC_header* robj, KLC_header** dq) {
  KLCNZDWith* obj = (KLCNZDWith*) robj;
  KLC_release_var(KLC_mcall("Exit", 1, &obj->value));
  KLC_partial_release_var(obj->value, dq);
}

KLCNTry* KLC_failm(const char* message) {
  KLCNString* s = KLC_mkstr(message);
  KLCNTry* t = KLCNTryZEnew(0, KLC_object_to_var((KLC_header*) s));
  KLC_release((KLC_header*) s);
  return t;
}

KLCNFile* KLCNSTDINZEinit() {
  return KLC_mkfile(stdin, ":STDIN", "r", 0);
}

KLCNFile* KLCNSTDOUTZEinit() {
  return KLC_mkfile(stdout, ":STDOUT", "w", 0);
}

KLCNFile* KLCNSTDERRZEinit() {
  return KLC_mkfile(stderr, ":STDERR", "w", 0);
}

KLCNList* KLCNARGSZEinit() {
#if KLC_WIN_APP
  KLCNList* args;
  int argc, i;
  LPWSTR* argv;
  argv = CommandLineToArgvW(KLC_lpCmdLine, &argc);
  args = KLC_mklist(argc);
  for (i = 0; i < argc; i++) {
    KLC_header* arg = (KLC_header*) KLC_windows_string_from_wstr(argv[i]);
    KLCNListZFpush(args, KLC_object_to_var(arg));
    KLC_release(arg);
  }
  LocalFree(argv);
#else
  KLCNList* args = KLC_mklist(KLC_argc);
  int i;
  for (i = 0; i < KLC_argc; i++) {
    KLC_header* arg = (KLC_header*) KLC_mkstr(KLC_argv[i]);
    KLCNListZFpush(args, KLC_object_to_var(arg));
    KLC_release(arg);
  }
#endif
  return args;
}

void KLCNassert(KLC_var cond) {
  if (!KLC_truthy(cond)) {
    KLC_errorf("Assertion failed");
  }
}

KLC_int KLCNerrno() {
  #if KLC_POSIX
    return errno;
  #else
    return -1;
  #endif
}

KLCNString* KLCNstrerror(KLC_int en) {
  #if KLC_POSIX
    return KLC_mkstr(strerror((int) en));
  #else
    return KLC_mkstr("Platform doesn't support error message...");
  #endif
}

void KLCNZEZBmain(void);

#if KLC_WIN_APP
int CALLBACK wWinMain(
    _In_ HINSTANCE hInstance,
    _In_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {
  KLC_lpCmdLine = lpCmdLine;
  KLCNZEZBmain();
  KLC_release_queued_before_exit();
  return 0;
}
#else
int main(int argc, const char** argv) {
  KLC_argc = argc;
  KLC_argv = argv;
  KLCNZEZBmain();
  KLC_release_queued_before_exit();
  return 0;
}
#endif
