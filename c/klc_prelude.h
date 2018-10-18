#ifndef klc_prelude_h
#define klc_prelude_h
#include <stdlib.h>
#include <stdio.h>
#include "klc_plat.h"

/* TODO: Figure out what to do if <stdint.h> isn't available
 * This is particularly necessary for:
 *   - figuring out what type to use for KLC_int
 *     some operations depend on KLC_int being
 *     able to fit any 'id' sort of value (e.g. ino_t)
 *
 *   - 16-bit and 32-bit string character types
 *
 * Additionally, I depend on exact fixed width integer
 * types to be available.
 */
#if KLC_POSIX
#include <inttypes.h>
#else
#include <stdint.h>
#endif

typedef uint32_t KLC_char32;

#if KLC_OS_WINDOWS
typedef wchar_t KLC_char16;
#else
typedef uint16_t KLC_char16;
#endif

/* Try to get KLC_int to be as large as possible
 * This is important in particular for POSIX.
 * If we don't know enough about the environment,
 * just choose long.
 */
#if KLC_OS_WINDOWS
typedef long long KLC_int; /* TODO: Check if this is always ok on Windows */
#define KLC_INT_FMT "%lld"
#elif KLC_POSIX
typedef intmax_t KLC_int;
#define KLC_INT_FMT "%" PRIiMAX
#else
typedef long KLC_int;
#define KLC_INT_FMT "%ld"
#endif

#if KLC_OS_WINDOWS
#include <windows.h>
#elif KLC_OS_APPLE
#include <CoreFoundation/CoreFoundation.h>
#include <objc/objc.h>
#include <objc/objc-runtime.h>
#endif

#define KLC_TAG_BOOL 0
#define KLC_TAG_INT 1
#define KLC_TAG_DOUBLE 2
#define KLC_TAG_FUNCTION 3
#define KLC_TAG_TYPE 4
#define KLC_TAG_OBJECT 5

typedef struct KLC_stack_frame KLC_stack_frame;
typedef char KLC_bool;


typedef struct KLC_header KLC_header;
typedef struct KLC_methodinfo KLC_methodinfo;
typedef struct KLC_methodlist KLC_methodlist;
typedef struct KLC_typeinfo KLC_typeinfo;
typedef struct KLC_var KLC_var;
typedef KLC_var (*KLC_fp)(int, KLC_var*); /* untyped function pointer */
typedef struct KLC_functioninfo KLC_functioninfo;
typedef KLC_functioninfo* KLC_function;
typedef KLC_typeinfo* KLC_type;
typedef struct KLCNWeakReference KLCNWeakReference;
typedef struct KLCNClosure KLCNClosure;
typedef struct KLCNString KLCNString;
typedef struct KLCNStringBuilder KLCNStringBuilder;
typedef struct KLCNList KLCNList;
typedef struct KLCNFile KLCNFile;

struct KLC_stack_frame {
  const char* filename;
  const char* function;
  long lineno;
};

struct KLC_header {
  KLC_typeinfo* type;
  size_t refcnt;
  KLC_header* next;
  KLCNWeakReference* weakref;
};

struct KLC_methodinfo {
  const char* const name;
  const KLC_fp body;
};

struct KLC_methodlist {
  const size_t size;
  const KLC_methodinfo* const methods;
};

struct KLC_functioninfo {
  const char* const name;
  const KLC_fp body;
};

struct KLC_typeinfo {
  const char* const name;
  void (*const deleter)(KLC_header*, KLC_header**);
  const KLC_methodlist* const methods;
};

struct KLC_var {
  int tag;
  union {
    KLC_header* obj;
    KLC_int i;
    double d;
    KLC_function f;
    KLC_type t;
  } u;
};

struct KLCNWeakReference {
  KLC_header header;
  KLC_header* obj;
};

struct KLCNStringBuilder {
  KLC_header header;
  size_t size, cap;
  char* buffer;
};

struct KLCNString {
  /* TODO: Also keep a UTF32 representation for random access */
  KLC_header header;
  size_t bytesize; /* number of actual bytes in buffer (utf-8 representation) */
  size_t nchars;   /* number of unicode code points */
  char* utf8;
  KLC_char16* utf16;
  KLC_char32* utf32;
    /* In C89, there's no way to get an integer type that guarantees
     * exactly 32-bits. As such, I want to error on side of correctness and
     * use chars instead for measuring out the utf-32 representation.
     */
  #if KLC_OS_WINDOWS
    LPWSTR wstr;
  #endif
  int is_ascii;
};

struct KLCNList {
  KLC_header header;
  size_t size, cap;
  KLC_var* buffer;
};

struct KLCNClosure {
  KLC_header header;
  KLCNList* captures;
  KLC_function f;
};

struct KLCNFile {
  KLC_header header;
  FILE* cfile;
  char* name;
  char mode[4];
  KLC_bool should_close;
};

#if KLC_OS_WINDOWS
extern HINSTANCE KLC_hInstance;
#endif

extern KLC_typeinfo KLC_typenull;
extern KLC_typeinfo KLC_typebool;
extern KLC_typeinfo KLC_typeint;
extern KLC_typeinfo KLC_typedouble;
extern KLC_typeinfo KLC_typefunction;
extern KLC_typeinfo KLC_typetype;

extern KLC_typeinfo KLC_typeWeakReference;
extern KLC_typeinfo KLC_typeString;
extern KLC_typeinfo KLC_typeStringBuilder;
extern KLC_typeinfo KLC_typeList;
extern KLC_typeinfo KLC_typeClosure;
extern KLC_typeinfo KLC_typeFile;

extern const KLC_var KLC_null;

void KLC_errorf(const char* fmt, ...);
int KLC_check_ascii(const char* str);
void KLC_retain(KLC_header *obj);
void KLC_retain_var(KLC_var v);
void KLC_partial_release(KLC_header* obj, KLC_header** delete_queue);
void KLC_partial_release_var(KLC_var v, KLC_header** delete_queue);
void KLC_release(KLC_header *obj);
void KLC_release_var(KLC_var v);
void KLC_init_header(KLC_header* header, KLC_type type);
KLCNString* KLC_mkstr_with_buffer(size_t bytesize, char* str, int is_ascii);
KLCNString* KLC_mkstr(const char *str);
KLC_var KLC_mcall(const char* name, int argc, KLC_var* argv);
KLC_bool KLC_truthy(KLC_var v);
void KLC_release(KLC_header *obj);
KLC_bool KLC_var_to_bool(KLC_var v);
KLC_var KLC_int_to_var(KLC_int i);
KLC_var KLC_object_to_var(KLC_header* obj);
void KLCNFile_mclose(KLCNFile* file);
KLCNList* KLC_mklist(size_t cap);
void KLCNList_mpush(KLCNList* list, KLC_var v);
KLCNString* KLCNString_mAdd(KLCNString* a, KLCNString* b);

#if KLC_OS_WINDOWS
LPCWSTR KLC_windows_get_wstr(KLCNString* s);
KLCNString* KLC_windows_string_from_wstr_buffer(LPWSTR s);
KLCNString* KLC_windows_string_from_wstr(LPCWSTR s);
#endif

#endif/*klc_prelude_h*/
