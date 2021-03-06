#include "kcrt.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct KLC_StackEntry KLC_StackEntry;
typedef struct KLC_RelaseOnExitQueue KLC_RelaseOnExitQueue;

struct KLC_Error {
  char* message;
  size_t stack_entry_count;
  KLC_StackEntry* stack;
};

struct KLC_StackEntry {
  const char* file_name;
  const char* function_name;
  size_t line_number;
};

struct KLC_Stack {
  size_t size, cap;
  KLC_StackEntry* buffer;
};

struct KLC_RelaseOnExitQueue {
  size_t size;
  size_t cap;
  KLC_Header** buffer;
};

/* This is kind of a hack having this here */
/* This is repeated in the output of builtins.k */
typedef struct KLCCSbuiltins_DLambda Lambda;
struct KLCCSbuiltins_DLambda {
  KLC_Header header;
  KLC_Lambda_capture *KLCCF_Ucapture;
  KLC_Lambda_body *KLCCF_Ubody;
};

static KLC_Error* KLC_int_method_add(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_sub(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_mul(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_div(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_mod(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_pow(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_lt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_gt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_le(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_int_method_ge(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_add(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_sub(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_mul(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_div(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_mod(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_pow(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_lt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_gt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_le(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);
static KLC_Error* KLC_float_method_ge(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv);

static KLC_RelaseOnExitQueue exit_release_queue;

static KLC_MethodEntry KLC_int_methods[] = {
  /* NOTE: Order here is important */
  { "__add", KLC_int_method_add },
  { "__div", KLC_int_method_div },
  { "__ge", KLC_int_method_ge },
  { "__gt", KLC_int_method_gt },
  { "__le", KLC_int_method_le },
  { "__lt", KLC_int_method_lt },
  { "__mod", KLC_int_method_mod },
  { "__mul", KLC_int_method_mul },
  { "__pow", KLC_int_method_pow },
  { "__sub", KLC_int_method_sub },
};

static KLC_MethodEntry KLC_float_methods[] = {
  /* NOTE: Order here is important */
  { "__add", KLC_float_method_add },
  { "__div", KLC_float_method_div },
  { "__ge", KLC_float_method_ge },
  { "__gt", KLC_float_method_gt },
  { "__le", KLC_float_method_le },
  { "__lt", KLC_float_method_lt },
  { "__mod", KLC_float_method_mod },
  { "__mul", KLC_float_method_mul },
  { "__pow", KLC_float_method_pow },
  { "__sub", KLC_float_method_sub },
};

KLC_var KLC_null = {0};

KLC_Class KLC_type_class = {
  "builtins",
  "type",
  NULL,
  0,
  NULL,
};

KLC_Class KLC_int_class = {
  "builtins",
  "int",
  NULL,
  sizeof(KLC_int_methods) / sizeof(KLC_MethodEntry),
  KLC_int_methods,
};

KLC_Class KLC_float_class = {
  "builtins",
  "float",
  NULL,
  sizeof(KLC_float_methods) / sizeof(KLC_MethodEntry),
  KLC_float_methods,
};

static const char* KLC_tag_to_str(int tag) {
  switch (tag) {
    case KLC_TAG_OBJECT:
      return "OBJECT";
    case KLC_TAG_BOOL:
      return "BOOL";
    case KLC_TAG_INT:
      return "INT";
    case KLC_TAG_FLOAT:
      return "FLOAT";
    case KLC_TAG_TYPE:
      return "TYPE";
  }
  return "INVALID";
}

static KLC_Error* KLC_expect_argc(KLC_Stack* stack, int argc, int exp) {
  if (argc != exp) {
    return KLC_errorf(0, stack, "Expected %d args but got %d", exp, argc);
  }
  return NULL;
}

static KLC_Error* KLC_expect_tag(KLC_Stack* stack, KLC_var x, int tag) {
  if (x.tag != tag) {
    return KLC_errorf(
      0, stack, "Invalid type (wrong tag, expected %s but got %s)",
      KLC_tag_to_str(tag),
      KLC_tag_to_str(x.tag)
    );
  }
  return NULL;
}

static KLC_Error* KLC_int_method_add(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_int(argv[0].u.i + argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(argv[0].u.i + argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_sub(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_int(argv[0].u.i - argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(argv[0].u.i - argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_mul(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_int(argv[0].u.i * argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(argv[0].u.i * argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_div(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_int(argv[0].u.i / argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(argv[0].u.i / argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_mod(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_int(argv[0].u.i % argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(fmod(argv[0].u.i, argv[1].u.f));
  }
  return NULL;
}

static KLC_int KLC_int_pow(KLC_int base, KLC_int exponent) {
  /* TODO: Consider overflow */
  /* TODO: Consider performance */
  KLC_int result = 1;
  while (exponent) {
    if (exponent % 2) {
      result *= base;
    }
    exponent /= 2;
    base *= base;
  }
  return result;
}

static KLC_Error* KLC_int_method_pow(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  /* It feels a little bad to be changing return type based on the */
  /* value of an argument (i.e. exponent >= 0), but Python */
  /* does this too. */
  if (argv[1].tag == KLC_TAG_INT && argv[1].u.i >= 0) {
    *out = KLC_var_from_int(KLC_int_pow(argv[0].u.i, argv[1].u.i));
  } else if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_float(pow(argv[0].u.i, argv[1].u.i));
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_float(pow(argv[0].u.i, argv[1].u.f));
  }
  return NULL;
}

static KLC_Error* KLC_int_method_lt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_bool(argv[0].u.i < argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_bool(argv[0].u.i < argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_gt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_bool(argv[0].u.i > argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_bool(argv[0].u.i > argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_le(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_bool(argv[0].u.i <= argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_bool(argv[0].u.i <= argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_int_method_ge(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_INT); if (e) { return e; }
  if (argv[1].tag == KLC_TAG_INT) {
    *out = KLC_var_from_bool(argv[0].u.i >= argv[1].u.i);
  } else {
    e = KLC_expect_tag(stack, argv[1], KLC_TAG_FLOAT); if (e) { return e; }
    *out = KLC_var_from_bool(argv[0].u.i >= argv[1].u.f);
  }
  return NULL;
}

static KLC_Error* KLC_float_method_add(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(argv[0].u.f + r);
  return NULL;
}

static KLC_Error* KLC_float_method_sub(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(argv[0].u.f - r);
  return NULL;
}

static KLC_Error* KLC_float_method_mul(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(argv[0].u.f * r);
  return NULL;
}

static KLC_Error* KLC_float_method_div(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(argv[0].u.f / r);
  return NULL;
}

static KLC_Error* KLC_float_method_mod(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(fmod(argv[0].u.f, r));
  return NULL;
}

static KLC_Error* KLC_float_method_pow(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_float(pow(argv[0].u.f, r));
  return NULL;
}

static KLC_Error* KLC_float_method_lt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_bool(argv[0].u.f < r);
  return NULL;
}

static KLC_Error* KLC_float_method_gt(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_bool(argv[0].u.f > r);
  return NULL;
}

static KLC_Error* KLC_float_method_le(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_bool(argv[0].u.f <= r);
  return NULL;
}

static KLC_Error* KLC_float_method_ge(KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  KLC_var ret;
  KLC_Error* e;
  KLC_float r = 0;
  e = KLC_expect_argc(stack, argc, 2); if (e) { return e; }
  e = KLC_expect_tag(stack, argv[0], KLC_TAG_FLOAT); if (e) { return e; }
  e = KLC_var_to_float(stack, &r, argv[1]); if (e) { return e; }
  *out = KLC_var_from_bool(argv[0].u.f >= r);
  return NULL;
}

static KLC_MethodEntry* KLC_find_method_in_list(
    KLC_MethodEntry* buf, size_t len, const char* name) {
  size_t lower = 0;
  size_t upper = len;
  if (len == 0) {
    return NULL;
  }
  for (;;) {
    size_t mid = (lower + upper) / 2;
    int cmp = strcmp(buf[mid].name, name);
    if (cmp == 0) {
      return buf + mid;
    } else if (lower == mid) {
      break;
    } else if (cmp < 0) {
      lower = mid;
    } else {
      upper = mid;
    }
  }
  return NULL;
}

char* KLC_CopyString(const char* s) {
  char* ret = (char*) malloc(sizeof(char) * (strlen(s) + 1));
  strcpy(ret, s);
  return ret;
}

KLC_Stack* KLC_new_stack() {
  KLC_Stack* stack = (KLC_Stack*) malloc(sizeof(KLC_Stack));
  stack->size = stack->cap = 0;
  stack->buffer = NULL;
  return stack;
}

void KLC_delete_stack(KLC_Stack* stack) {
  free(stack->buffer);
  free(stack);
}

void KLC_stack_push(
    KLC_Stack* stack,
    const char* file_name,
    const char* function_name,
    size_t ln) {
  if (stack->size == stack->cap) {
    stack->cap = stack->cap == 0 ? 32 : (stack->cap * 2);
    stack->buffer = (KLC_StackEntry*) realloc(
      stack->buffer, sizeof(KLC_StackEntry) * stack->cap);
  }
  stack->size++;
  stack->buffer[stack->size - 1].file_name = file_name;
  stack->buffer[stack->size - 1].function_name = function_name;
  stack->buffer[stack->size - 1].line_number = ln;
}

void KLC_stack_pop(KLC_Stack* stack) {
  stack->size--;
}

void KLC_panic_with_error(KLC_Error* error) {
  size_t i;
  fprintf(stderr, "ERROR: %s\n", error->message);
  for (i = 0; i < error->stack_entry_count; i++) {
    fprintf(
      stderr,
      "  File \"%s\", line %lu, in %s\n",
      error->stack[i].file_name,
      (unsigned long) error->stack[i].line_number,
      error->stack[i].function_name
    );
  }
  KLC_delete_error(error);
  exit(1);
}

KLC_Error* KLC_new_error_with_message(KLC_Stack* stack, const char* msg) {
  KLC_Error* error = malloc(sizeof(KLC_Error));
  size_t stack_entry_count = stack->size;
  size_t nbytes = sizeof(KLC_StackEntry) * stack_entry_count;
  error->message = KLC_CopyString(msg);
  error->stack_entry_count = stack_entry_count;
  error->stack = (KLC_StackEntry*) malloc(nbytes);
  memcpy(error->stack, stack->buffer, nbytes);
  return error;
}

static size_t KLC_estimate_sprintf_size(const char* format) {
  size_t len = strlen(format);
  size_t estimate = len + 1;
  size_t i;
  for (i = 0; i < len; i++) {
    if (format[i] == '%') {
      estimate += 64;
    }
  }
  return estimate * sizeof(char);
}

static KLC_Error* KLC_verrorf(
    size_t hint, KLC_Stack* stack, const char* format, va_list args) {
  size_t estimate_buffer_size = KLC_estimate_sprintf_size(format) + hint;
  char* buffer = (char*) malloc(estimate_buffer_size);
  KLC_Error* error;
  vsprintf(buffer, format, args);
  error = KLC_new_error_with_message(stack, buffer);
  free(buffer);
  return error;
}

/* The 'hint' argument is a hint for how much extra memory should be allocated */
/* for the buffer, for the error message. */
KLC_Error* KLC_errorf(size_t hint, KLC_Stack* stack, const char* format, ...) {
  KLC_Error* err;
  va_list args;
  va_start(args, format);
  err = KLC_verrorf(hint, stack, format, args);
  va_end(args);
  return err;
}

void KLC_delete_error(KLC_Error* error) {
  free(error->message);
  free(error->stack);
  free(error);
}

const char* KLC_get_error_message(KLC_Error* error) {
  return error->message;
}

void KLC_panic(const char* message) {
  int* nullp = NULL;
  fprintf(stderr, "%s\n", message);
  printf("Trigger segfault %d", *nullp);

  /* If triggering segfault fails, explicitly exit */
  exit(1);
}

KLC_bool KLC_is(KLC_var left, KLC_var right) {
  if (left.tag != right.tag) {
    return 0;
  }
  switch (left.tag) {
    case KLC_TAG_OBJECT:
      return left.u.p == right.u.p;
    case KLC_TAG_BOOL:
      return left.u.b == right.u.b;
    case KLC_TAG_INT:
      return left.u.i == right.u.i;
    case KLC_TAG_FLOAT:
      return left.u.f == right.u.f;
    case KLC_TAG_TYPE:
      return left.u.t == right.u.t;
  }
  KLC_panic("KLC_is, invalid tag");
  return 0;
}

void KLC_retain(KLC_Header* obj) {
  if (obj) {
    obj->refcnt++;
  }
}

void KLC_release(KLC_Header* obj) {
  KLC_Header* delete_queue = NULL;
  KLC_partial_release(obj, &delete_queue);
  while (delete_queue) {
    obj = delete_queue;
    delete_queue = delete_queue->next;
    obj->cls->deleter(obj, &delete_queue);
    free(obj);
  }
}

void KLC_partial_release(KLC_Header* obj, KLC_Header** delete_queue) {
  if (obj) {
    if (obj->refcnt) {
      obj->refcnt--;
    } else {
      obj->next = *delete_queue;
      *delete_queue = obj;
    }
  }
}

void KLC_retain_var(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_retain(v.u.p);
  }
}

void KLC_release_var(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_release(v.u.p);
  }
}

void KLC_partial_release_var(KLC_var v, KLC_Header** delete_queue) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_partial_release(v.u.p, delete_queue);
  }
}

void KLC_release_on_exit(KLC_Header* p) {
  if (p) {
    if (exit_release_queue.size + 1 >= exit_release_queue.cap) {
      exit_release_queue.cap = exit_release_queue.cap * 2 + 16;
      exit_release_queue.buffer =
        (KLC_Header**) realloc(
          exit_release_queue.buffer,
          sizeof(KLC_Header*) * exit_release_queue.cap
        );
    }
    exit_release_queue.buffer[exit_release_queue.size++] = p;
  }
}

void KLC_release_var_on_exit(KLC_var v) {
  if (v.tag == KLC_TAG_OBJECT) {
    KLC_release_on_exit(v.u.p);
  }
}

void KLC_release_vars_queued_for_exit() {
  size_t i;
  for (i = 0; i < exit_release_queue.size; i++) {
    KLC_release(exit_release_queue.buffer[i]);
  }
  free(exit_release_queue.buffer);
}

void* KLC_realloc_var_array(void* buffer, size_t old_cap, size_t new_cap) {
  KLC_var KLC_null = {0};
  size_t i;
  KLC_var* arr = (KLC_var*) buffer;

  if (old_cap > new_cap) {
    KLC_var_array_clear_range(buffer, new_cap, old_cap);
  }

  arr = (KLC_var*) realloc(buffer, sizeof(KLC_var) * new_cap);

  if (old_cap < new_cap) {
    for (i = old_cap; i < new_cap; i++) {
      arr[i] = KLC_null;
    }
  }

  return arr;
}

void KLC_partial_release_var_array(
    void* buffer, size_t size, size_t cap, void* delete_queue) {
  KLC_var* arr = (KLC_var*) buffer;
  size_t i;
  for (i = 0; i < size; i++) {
    KLC_partial_release_var(arr[i], delete_queue);
  }
  free(buffer);
}

void KLC_var_array_clear_range(void* buffer, size_t begin, size_t end) {
  KLC_var KLC_null = {0};
  KLC_var* arr = (KLC_var*) buffer;
  size_t i;
  for (i = begin; i < end; i++) {
    KLC_release_var(arr[i]);
    arr[i] = KLC_null;
  }
}

KLC_var KLC_var_array_get(void* buffer, size_t i) {
  KLC_var* arr = (KLC_var*) buffer;
  KLC_retain_var(arr[i]);
  return arr[i];
}

void KLC_var_array_set(void* buffer, size_t i, KLC_var value) {
  KLC_var* arr = (KLC_var*) buffer;
  KLC_retain_var(value);
  KLC_release_var(arr[i]);
  arr[i] = value;
}

KLC_Lambda_capture* KLC_new_Lambda_capture(size_t size, ...) {
  KLC_Lambda_capture* ret =
    (KLC_Lambda_capture*) malloc(sizeof(KLC_Lambda_capture));
  size_t i;
  va_list args;
  va_start(args, size);
  ret->size = size;
  ret->buffer = (KLC_var*) malloc(sizeof(KLC_var) * size);
  for (i = 0; i < size; i++) {
    ret->buffer[i] = va_arg(args, KLC_var);
    KLC_retain_var(ret->buffer[i]);
  }
  return ret;
}

KLC_var KLC_Lambda_capture_get(KLC_Lambda_capture* c, size_t i) {
  if (i >= c->size) {
    KLC_panic("Lambda_capture_get index out of bounds");
  }
  return c->buffer[i];
}

void KLC_free_lambda_capture(KLC_Lambda_capture* c) {
  if (c) {
    size_t i;
    for (i = 0; i < c->size; i++) {
      KLC_release_var(c->buffer[i]);
    }
    free(c->buffer);
    free(c);
  }
}

KLC_Error* KLC_lambda_call(
    KLC_Stack* stack, KLC_var* out, int argc, KLC_var* argv) {
  Lambda* p = (Lambda*) argv[0].u.p;
  return p->KLCCF_Ubody(stack, out, p->KLCCF_Ucapture, argc - 1, argv + 1);
}

KLC_var KLC_var_from_ptr(KLC_Header* p) {
  KLC_var ret;
  ret.tag = KLC_TAG_OBJECT;
  ret.u.p = p;
  return ret;
}

KLC_var KLC_var_from_bool(KLC_bool b) {
  KLC_var ret;
  ret.tag = KLC_TAG_BOOL;
  ret.u.b = b;
  return ret;
}

KLC_var KLC_var_from_int(KLC_int i) {
  KLC_var ret;
  ret.tag = KLC_TAG_INT;
  ret.u.i = i;
  return ret;
}

KLC_var KLC_var_from_float(KLC_float f) {
  KLC_var ret;
  ret.tag = KLC_TAG_FLOAT;
  ret.u.f = f;
  return ret;
}

KLC_var KLC_var_from_type(KLC_Class* c) {
  KLC_var ret;
  ret.tag = KLC_TAG_TYPE;
  ret.u.t = c;
  return ret;
}

KLC_Error* KLC_var_to_ptr(
    KLC_Stack* stack, KLC_Header** out, KLC_var v, KLC_Class* cls) {
  if (v.tag != KLC_TAG_OBJECT) {
    return KLC_errorf(
        0,
        stack,
        "Expected class type but got %s",
        KLC_tag_to_str(v.tag));
  }
  if (!v.u.p || !v.u.p->cls) {
    return KLC_errorf(
      strlen(cls->module_name) +
        strlen(cls->short_name),
      stack,
      "Tried to cast null instance to %s#%s",
      cls->module_name,
      cls->short_name
    );
  } else if (v.u.p->cls != cls) {
    return KLC_errorf(
        strlen(v.u.p->cls->module_name) +
          strlen(v.u.p->cls->short_name) +
          strlen(cls->module_name) +
          strlen(cls->short_name),
        stack,
        "Tried to cast %s#%s instance to %s#%s",
        v.u.p->cls->module_name,
        v.u.p->cls->short_name,
        cls->module_name,
        cls->short_name
      );
  }
  *out = v.u.p;
  return NULL;
}

KLC_Error* KLC_var_to_bool(KLC_Stack* stack, KLC_bool* out, KLC_var v) {
  if (v.tag != KLC_TAG_BOOL) {
    return KLC_errorf(0, stack, "Expected BOOL but got %s", KLC_tag_to_str(v.tag));
  }
  *out = v.u.i;
  return NULL;
}

KLC_Error* KLC_var_to_int(KLC_Stack* stack, KLC_int* out, KLC_var v) {
  if (v.tag != KLC_TAG_INT) {
    return KLC_errorf(0, stack, "Expected INT but got %s", KLC_tag_to_str(v.tag));
  }
  *out = v.u.i;
  return NULL;
}

KLC_Error* KLC_var_to_float(KLC_Stack* stack, KLC_float* out, KLC_var v) {
  if (v.tag == KLC_TAG_INT) {
    *out = (KLC_float) v.u.i;
  } else if (v.tag == KLC_TAG_FLOAT) {
    *out = v.u.f;
  } else {
    return KLC_errorf(
      0, stack, "Expected FLOAT but got %s", KLC_tag_to_str(v.tag));
  }
  return NULL;
}

KLC_Error* KLC_var_to_type(KLC_Stack* stack, KLC_Class** out, KLC_var v) {
  if (v.tag != KLC_TAG_TYPE) {
    return KLC_errorf(
      0, stack, "Expected TYPE but got %s", KLC_tag_to_str(v.tag));
  }
  *out = v.u.t;
  return NULL;
}

KLC_Class* KLC_get_class(KLC_var v) {
  switch (v.tag) {
    case KLC_TAG_OBJECT:
      if (v.u.p) {
        return v.u.p->cls;
      }
      break;
    case KLC_TAG_INT:
      return &KLC_int_class;
    case KLC_TAG_FLOAT:
      return &KLC_float_class;
    case KLC_TAG_TYPE:
      if (v.u.t) {
        return &KLC_type_class;
      }
      break;
  }
  return NULL;
}

KLC_MethodEntry* KLC_find_instance_method(KLC_Class* cls, const char* name) {
  return KLC_find_method_in_list(cls->methods, cls->number_of_methods, name);
}

KLC_MethodEntry* KLC_find_class_method(KLC_Class* cls, const char* name) {
  return KLC_find_method_in_list(
    cls->class_methods, cls->number_of_class_methods, name);
}

KLC_bool KLC_has_method(KLC_var x, const char* name) {
  if (x.tag == KLC_TAG_TYPE) {
    KLC_Class* cls = (KLC_Class*) x.u.p;
    return KLC_find_class_method(cls, name) != NULL;
  } else {
    KLC_Class* cls = KLC_get_class(x);
    return cls && KLC_find_instance_method(cls, name) != NULL;
  }
}

KLC_Error* KLC_call_method(
    KLC_Stack* stack, KLC_var* out, const char* name, int argc, KLC_var* argv) {
  KLC_Class* cls;
  KLC_MethodEntry* method_entry;
  int is_class_method_call;
  if (argc < 1) {
    return KLC_errorf(
        0, stack, "FUBAR: KLC_call_method with argc < 1, argc = %d", argc);
  }
  /* For types, we don't look up a method in its type type, but we search */
  /* among its class methods. Strictly speaking, this kind of breaks */
  /* orthogonality, but is also extremely convenient. */
  is_class_method_call = (argv[0].tag == KLC_TAG_TYPE);

  /* Determine which class to search method from */
  if (is_class_method_call) {
    cls = (KLC_Class*) argv[0].u.p;
  } else {
    cls = KLC_get_class(argv[0]);
    if (!cls) {
      return KLC_errorf(
        0,
        stack,
        "FUBAR: Could not get class for method call (tag = %s, method_name = %s)",
        KLC_tag_to_str(argv[0].tag),
        name
      );
    }
  }

  /* Find the method entry */
  if (is_class_method_call) {
    method_entry = KLC_find_class_method(cls, name);
  } else {
    method_entry = KLC_find_instance_method(cls, name);
  }

  if (!method_entry) {
    const char* method_type = is_class_method_call ? "Class method" : "Method";
    return KLC_errorf(
      strlen(name) +
        strlen(cls->module_name) +
        strlen(cls->short_name),
      stack,
      "%s '%s' not found for '%s#%s'",
      method_type,
      name,
      cls->module_name,
      cls->short_name
    );
  }
  return method_entry->method(stack, out, argc, argv);
}

KLC_int KLC_get_tag(KLC_var x) {
  return x.tag;
}

KLC_int KLC_get_obj_tag() {
  return KLC_TAG_OBJECT;
}

KLC_int KLC_get_bool_tag() {
  return KLC_TAG_BOOL;
}

KLC_int KLC_get_int_tag() {
  return KLC_TAG_INT;
}
KLC_int KLC_get_float_tag() {
  return KLC_TAG_FLOAT;
}

KLC_int KLC_get_type_tag() {
  return KLC_TAG_TYPE;
}

char const* KLC_get_type_module_name(KLC_Class* t) {
  return t->module_name;
}

char const* KLC_get_type_short_name(KLC_Class* t) {
  return t->short_name;
}


KLC_Error* KLC_new_error_from_string(
    KLC_Stack* stack, struct KLCCSbuiltins_DString* s) {
  return KLC_new_error_with_message(stack, KLC_cstr(s));
}

int main(int argc, char** argv) {
  KLC_Stack* stack = KLC_new_stack();
  KLC_Error* error = KLCFNmain_Dmain(stack, NULL);
  KLC_release_vars_queued_for_exit();
  KLC_delete_stack(stack);
  if (error) {
    KLC_panic_with_error(error);
  }
  return 0;
}
