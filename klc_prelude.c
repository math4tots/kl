/* Autogenerated by the KL Compiler */
#include "klc_prelude.h"
#include "klc_os.h"
#include <stdarg.h>
#include <string.h>

#define KLC_MAX_STACK_SIZE 1000

static size_t KLC_stacktrace_size = 0;
static KLC_stack_frame KLC_stacktrace[KLC_MAX_STACK_SIZE];

static size_t KLC_release_on_exit_buffer_cap = 0;
static size_t KLC_release_on_exit_buffer_size = 0;
static KLC_header** KLC_release_on_exit_buffer = NULL;

const KLC_var KLC_null = {
  KLC_TAG_OBJECT,
  { NULL }
};

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

KLC_bool KLCN_Is(KLC_var a, KLC_var b) {
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

KLC_bool KLCN_IsNot(KLC_var a, KLC_var b) {
  return !KLCN_Is(a, b);
}

KLC_bool KLCN_Eq(KLC_var a, KLC_var b) {
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

KLC_bool KLCN_Ne(KLC_var a, KLC_var b) {
  return !KLCN_Eq(a, b);
}

KLC_bool KLCN_Lt(KLC_var a, KLC_var b) {
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

KLC_bool KLCN_Gt(KLC_var a, KLC_var b) {
  return KLCN_Lt(b, a);
}

KLC_bool KLCN_Le(KLC_var a, KLC_var b) {
  return !KLCN_Lt(b, a);
}

KLC_bool KLCN_Ge(KLC_var a, KLC_var b) {
  return !KLCN_Lt(a, b);
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
      KLC_errorf("No such method '%s' for type '%s'", name, type->name);
    }
    return m->body(argc, argv);
  }
}

KLC_int KLCNint_mAdd(KLC_int a, KLC_int b) {
  return a + b;
}

KLC_int KLCNint_mSub(KLC_int a, KLC_int b) {
  return a - b;
}

KLC_int KLCNint_mMul(KLC_int a, KLC_int b) {
  return a * b;
}

KLC_int KLCNint_mDiv(KLC_int a, KLC_int b) {
  return a / b;
}

KLC_int KLCNint_mMod(KLC_int a, KLC_int b) {
  return a % b;
}

KLC_bool KLCNint_mEq(KLC_int a, KLC_int b) {
  return a == b;
}

KLC_bool KLCNint_mNe(KLC_int a, KLC_int b) {
  return a != b;
}

KLC_bool KLCNint_mLt(KLC_int a, KLC_int b) {
  return a < b;
}

KLCNString* KLCNint_mRepr(KLC_int i) {
  char buffer[50];
  sprintf(buffer, "%ld", i);
  return KLC_mkstr(buffer);
}

double KLCNdouble_mAdd(double a, double b) {
  return a + b;
}

double KLCNdouble_mSub(double a, double b) {
  return a - b;
}

double KLCNdouble_mMul(double a, double b) {
  return a * b;
}

double KLCNdouble_mDiv(double a, double b) {
  return a / b;
}

KLC_bool KLCNdouble_mEq(double a, double b) {
  return a == b;
}

KLC_bool KLCNdouble_mNe(double a, double b) {
  return a != b;
}

KLC_bool KLCNdouble_mLt(double a, double b) {
  return a < b;
}

KLCNString* KLCNdouble_mRepr(double d) {
  char buffer[80];
  sprintf(buffer, "%f", d);
  return KLC_mkstr(buffer);
}

KLCNString* KLCNfunction_mGETname(KLC_function f) {
  return KLC_mkstr(f->name);
}

KLCNString* KLCNtype_mGETname(KLC_type t) {
  return KLC_mkstr(t->name);
}

KLC_bool KLCNtype_mEq(KLC_type a, KLC_type b) {
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

KLCNWeakReference* KLCNWeakReference_new(KLC_var v) {
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

KLC_var KLCNWeakReference_mgetNullable(KLCNWeakReference* wr) {
  KLC_retain(wr->obj); /* retain to use as return */
  return KLC_object_to_var(wr->obj);
}

void KLC_deleteString(KLC_header* robj, KLC_header** dq) {
  KLCNString* obj = (KLCNString*) robj;
  free(obj->buffer);
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
  if (!is_ascii) {
    KLC_errorf("Non-ascii strings not yet supported");
  }
  KLC_init_header(&obj->header, &KLC_typeString);
  obj->bytesize = bytesize;
  obj->nchars = bytesize; /* only true when is_ascii */
  obj->buffer = str;
  obj->utf32 = NULL;
  obj->is_ascii = is_ascii;
  return obj;
}

KLCNString* KLC_mkstr(const char *str) {
  size_t len = strlen(str);
  char* buffer = (char*) malloc(sizeof(char) * (len + 1));
  strcpy(buffer, str);
  return KLC_mkstr_with_buffer(len, buffer, KLC_check_ascii(buffer));
}

KLC_int KLCNString_mbytesize(KLCNString* s) {
  /* TODO: This may be lossy. Figure this out */
  return (KLC_int) s->bytesize;
}

KLC_int KLCNString_mGETsize(KLCNString* s) {
  /* TODO: This may be lossy. Figure this out */
  return (KLC_int) s->nchars;
}

KLCNString* KLCNString_mStr(KLCNString* s) {
  KLC_retain((KLC_header*) s);
  return s;
}

KLCNString* KLCNString_mescape(KLCNString* str) {
  size_t i = 0, j = 0, bs = str->bytesize;
  char* buffer = (char*) malloc(sizeof(char) * (2 * bs + 1));
  char* s = str->buffer;

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

KLCNString* KLCNString_mAdd(KLCNString* a, KLCNString* b) {
  size_t bytesize = a->bytesize + b->bytesize;
  char* buffer = (char*) malloc(sizeof(char) * (bytesize + 1));
  strcpy(buffer, a->buffer);
  strcpy(buffer + a->bytesize, b->buffer);
  return KLC_mkstr_with_buffer(bytesize, buffer, a->is_ascii && b->is_ascii);
}

KLC_bool KLCNString_mEq(KLCNString* a, KLCNString* b) {
  return a->bytesize == b->bytesize && strcmp(a->buffer, b->buffer) == 0;
}

KLC_bool KLCNString_mLt(KLCNString* a, KLCNString* b) {
  return strcmp(a->buffer, b->buffer) < 0;
}

void KLCNpanic(KLCNString* message) {
  KLC_errorf("%s", message->buffer);
}

KLCNStringBuilder* KLCNStringBuilder_new() {
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

void KLCNStringBuilder_maddstr(KLCNStringBuilder* sb, KLCNString* s) {
  if (s->bytesize) {
    if (sb->size + s->bytesize + 1 > sb->cap) {
      sb->cap += s->bytesize + 16;
      sb->cap *= 2;
      sb->buffer = (char*) realloc(sb->buffer, sizeof(char) * sb->cap);
    }
    strcpy(sb->buffer + sb->size, s->buffer);
    sb->size += s->bytesize;
  }
}

KLCNString* KLCNStringBuilder_mbuild(KLCNStringBuilder* sb) {
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

void KLCNList_mpush(KLCNList* list, KLC_var v) {
  if (list->size >= list->cap) {
    list->cap += 4;
    list->cap *= 2;
    list->buffer = (KLC_var*) realloc(list->buffer, sizeof(KLC_var) * list->cap);
  }
  list->buffer[list->size++] = v;
  KLC_retain_var(v);
}

KLC_var KLCNList_mGetItem(KLCNList* list, KLC_int i) {
  if (i < 0) {
    i += list->size;
  }
  if (i < 0 || ((size_t) i) >= list->size) {
    KLC_errorf("Index out of bounds (i = %ld, size = %ld)", i, list->size);
  }
  KLC_retain_var(list->buffer[i]);
  return list->buffer[i];
}

KLC_var KLCNList_mSetItem(KLCNList* list, KLC_int i, KLC_var v) {
  if (i < 0) {
    i += list->size;
  }
  if (i < 0 || ((size_t) i) >= list->size) {
    KLC_errorf("Index out of bounds (i = %ld, size = %ld)", i, list->size);
  }
  list->buffer[i] = v;
  KLC_retain_var(v);  /* once for attaching value to list */
  KLC_retain_var(v);  /* once more for using as return value */
  return v;
}

KLC_int KLCNList_mGETsize(KLCNList* list) {
  return (KLC_int) list->size;
}

KLC_var KLCNClosure_new(KLCNList* captures, KLC_function f) {
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

KLC_var KLC_untypedClosure_mCall(int argc, KLC_var* argv) {
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
  KLCNFile_mclose(file);
}

KLCNFile* KLCNFile_new(KLCNString* path, KLCNString* mode) {
  FILE* cfile;
  if (!KLC_is_valid_file_mode(mode->buffer)) {
    KLC_errorf("Invalid file mode: %s", mode->buffer);
  }
  cfile = fopen(path->buffer, mode->buffer);
  if (!cfile) {
    KLC_errorf("Failed to open file at %s", path->buffer);
  }
  return KLC_mkfile(cfile, path->buffer, mode->buffer, 1);
}

void KLCNFile_mclose(KLCNFile* file) {
  if (file->cfile && file->should_close) {
    fclose(file->cfile);
    file->cfile = NULL;
  }
}

void KLCNFile_mwrite(KLCNFile* file, KLCNString* s) {
  if (!file->cfile) {
    KLC_errorf("Trying to write to closed file");
  }
  if (file->mode[0] != 'w' && file->mode[0] != 'a') {
    KLC_errorf(
        "Trying to write to file that's not in write/append mode (%s)",
        file->name);
  }
  fwrite(s->buffer, 1, s->bytesize, file->cfile);
}

KLCNString* KLCNFile_mread(KLCNFile* file) {
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

KLCNFile* KLCN_initstdin() {
  return KLC_mkfile(stdin, ":stdin", "r", 0);
}

KLCNFile* KLCN_initstdout() {
  return KLC_mkfile(stdout, ":stdout", "w", 0);
}

KLCNFile* KLCN_initstderr() {
  return KLC_mkfile(stderr, ":stderr", "w", 0);
}

void KLCNassert(KLC_var cond) {
  if (!KLC_truthy(cond)) {
    KLC_errorf("Assertion failed");
  }
}

void KLCNmain();

int main() {
  KLCNmain();
  KLC_release_queued_before_exit();
  return 0;
}
