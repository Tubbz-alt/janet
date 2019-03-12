/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_AMALG
#include <janet.h>
#include "compile.h"
#include "state.h"
#include "util.h"
#endif

/* Generated bytes */
#ifdef JANET_BOOTSTRAP
extern const unsigned char *janet_gen_core;
extern int32_t janet_gen_core_size;
#else
extern const unsigned char *janet_core_image;
extern size_t janet_core_image_size;
#endif

/* Use LoadLibrary on windows or dlopen on posix to load dynamic libaries
 * with native code. */
#if defined(JANET_NO_DYNAMIC_MODULES)
typedef int Clib;
#define load_clib(name) ((void) name, 0)
#define symbol_clib(lib, sym) ((void) lib, (void) sym, 0)
#define error_clib() "dynamic libraries not supported"
#elif defined(JANET_WINDOWS)
#include <windows.h>
typedef HINSTANCE Clib;
#define load_clib(name) LoadLibrary((name))
#define symbol_clib(lib, sym) GetProcAddress((lib), (sym))
#define error_clib() "could not load dynamic library"
#else
#include <dlfcn.h>
typedef void *Clib;
#define load_clib(name) dlopen((name), RTLD_NOW)
#define symbol_clib(lib, sym) dlsym((lib), (sym))
#define error_clib() dlerror()
#endif

JanetModule janet_native(const char *name, const uint8_t **error) {
    Clib lib = load_clib(name);
    JanetModule init;
    if (!lib) {
        *error = janet_cstring(error_clib());
        return NULL;
    }
    init = (JanetModule) symbol_clib(lib, "_janet_init");
    if (!init) {
        *error = janet_cstring("could not find _janet_init symbol");
        return NULL;
    }
    return init;
}

static Janet janet_core_native(int32_t argc, Janet *argv) {
    JanetModule init;
    janet_arity(argc, 1, 2);
    const uint8_t *path = janet_getstring(argv, 0);
    const uint8_t *error = NULL;
    JanetTable *env;
    if (argc == 2) {
        env = janet_gettable(argv, 1);
    } else {
        env = janet_table(0);
    }
    init = janet_native((const char *)path, &error);
    if (!init) {
        janet_panicf("could not load native %S: %S", path, error);
    }
    init(env);
    return janet_wrap_table(env);
}

static Janet janet_core_print(int32_t argc, Janet *argv) {
    for (int32_t i = 0; i < argc; ++i) {
        int32_t j, len;
        const uint8_t *vstr = janet_to_string(argv[i]);
        len = janet_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    return janet_wrap_nil();
}

static Janet janet_core_describe(int32_t argc, Janet *argv) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < argc; ++i)
        janet_description_b(b, argv[i]);
    return janet_stringv(b->data, b->count);
}

static Janet janet_core_string(int32_t argc, Janet *argv) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < argc; ++i)
        janet_to_string_b(b, argv[i]);
    return janet_stringv(b->data, b->count);
}

static Janet janet_core_symbol(int32_t argc, Janet *argv) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < argc; ++i)
        janet_to_string_b(b, argv[i]);
    return janet_symbolv(b->data, b->count);
}

static Janet janet_core_keyword(int32_t argc, Janet *argv) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < argc; ++i)
        janet_to_string_b(b, argv[i]);
    return janet_keywordv(b->data, b->count);
}

static Janet janet_core_buffer(int32_t argc, Janet *argv) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < argc; ++i)
        janet_to_string_b(b, argv[i]);
    return janet_wrap_buffer(b);
}

static Janet janet_core_is_abstract(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return janet_wrap_boolean(janet_checktype(argv[0], JANET_ABSTRACT));
}

static Janet janet_core_scannumber(int32_t argc, Janet *argv) {
    double number;
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    if (janet_scan_number(view.bytes, view.len, &number))
        return janet_wrap_nil();
    return janet_wrap_number(number);
}

static Janet janet_core_tuple(int32_t argc, Janet *argv) {
    return janet_wrap_tuple(janet_tuple_n(argv, argc));
}

static Janet janet_core_array(int32_t argc, Janet *argv) {
    JanetArray *array = janet_array(argc);
    array->count = argc;
    memcpy(array->data, argv, argc * sizeof(Janet));
    return janet_wrap_array(array);
}

static Janet janet_core_table(int32_t argc, Janet *argv) {
    int32_t i;
    if (argc & 1)
        janet_panic("expected even number of arguments");
    JanetTable *table = janet_table(argc >> 1);
    for (i = 0; i < argc; i += 2) {
        janet_table_put(table, argv[i], argv[i + 1]);
    }
    return janet_wrap_table(table);
}

static Janet janet_core_struct(int32_t argc, Janet *argv) {
    int32_t i;
    if (argc & 1)
        janet_panic("expected even number of arguments");
    JanetKV *st = janet_struct_begin(argc >> 1);
    for (i = 0; i < argc; i += 2) {
        janet_struct_put(st, argv[i], argv[i + 1]);
    }
    return janet_wrap_struct(janet_struct_end(st));
}

static Janet janet_core_gensym(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_symbol(janet_symbol_gen());
}

static Janet janet_core_gccollect(int32_t argc, Janet *argv) {
    (void) argv;
    (void) argc;
    janet_collect();
    return janet_wrap_nil();
}

static Janet janet_core_gcsetinterval(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t val = janet_getinteger(argv, 0);
    if (val < 0)
        janet_panic("expected non-negative integer");
    janet_vm_gc_interval = val;
    return janet_wrap_nil();
}

static Janet janet_core_gcinterval(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_number(janet_vm_gc_interval);
}

static Janet janet_core_type(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetType t = janet_type(argv[0]);
    if (t == JANET_ABSTRACT) {
        return janet_ckeywordv(janet_abstract_type(janet_unwrap_abstract(argv[0]))->name);
    } else {
        return janet_ckeywordv(janet_type_names[t]);
    }
}

static Janet janet_core_next(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetDictView view = janet_getdictionary(argv, 0);
    const JanetKV *end = view.kvs + view.cap;
    const JanetKV *kv = janet_checktype(argv[1], JANET_NIL)
                        ? view.kvs
                        : janet_dict_find(view.kvs, view.cap, argv[1]) + 1;
    while (kv < end) {
        if (!janet_checktype(kv->key, JANET_NIL)) return kv->key;
        kv++;
    }
    return janet_wrap_nil();
}

static Janet janet_core_hash(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return janet_wrap_number(janet_hash(argv[0]));
}

static Janet janet_core_getline(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 2);
    JanetBuffer *buf = (argc >= 2) ? janet_getbuffer(argv, 1) : janet_buffer(10);
    if (argc >= 1) {
        const char *prompt = (const char *) janet_getstring(argv, 0);
        printf("%s", prompt);
        fflush(stdout);
    }
    {
        buf->count = 0;
        int c;
        for (;;) {
            c = fgetc(stdin);
            if (feof(stdin) || c < 0) {
                break;
            }
            janet_buffer_push_u8(buf, (uint8_t) c);
            if (c == '\n') break;
        }
    }
    return janet_wrap_buffer(buf);
}

static const JanetReg corelib_cfuns[] = {
    {
        "native", janet_core_native,
        JDOC("(native path [,env])\n\n"
             "Load a native module from the given path. The path "
             "must be an absolute or relative path on the file system, and is "
             "usually a .so file on Unix systems, and a .dll file on Windows. "
             "Returns an environment table that contains functions and other values "
             "from the native module.")
    },
    {
        "print", janet_core_print,
        JDOC("(print & xs)\n\n"
             "Print values to the console (standard out). Value are converted "
             "to strings if they are not already. After printing all values, a "
             "newline character is printed. Returns nil.")
    },
    {
        "describe", janet_core_describe,
        JDOC("(describe x)\n\n"
             "Returns a string that is a human readable description of a value x.")
    },
    {
        "string", janet_core_string,
        JDOC("(string & parts)\n\n"
             "Creates a string by concatenating values together. Values are "
             "converted to bytes via describe if they are not byte sequences. "
             "Returns the new string.")
    },
    {
        "symbol", janet_core_symbol,
        JDOC("(symbol & xs)\n\n"
             "Creates a symbol by concatenating values together. Values are "
             "converted to bytes via describe if they are not byte sequences. Returns "
             "the new symbol.")
    },
    {
        "keyword", janet_core_keyword,
        JDOC("(keyword & xs)\n\n"
             "Creates a keyword by concatenating values together. Values are "
             "converted to bytes via describe if they are not byte sequences. Returns "
             "the new keyword.")
    },
    {
        "buffer", janet_core_buffer,
        JDOC("(buffer & xs)\n\n"
             "Creates a new buffer by concatenating values together. Values are "
             "converted to bytes via describe if they are not byte sequences. Returns "
             "the new buffer.")
    },
    {
        "abstract?", janet_core_is_abstract,
        JDOC("(abstract? x)\n\n"
             "Check if x is an abstract type.")
    },
    {
        "table", janet_core_table,
        JDOC("(table & kvs)\n\n"
             "Creates a new table from a variadic number of keys and values. "
             "kvs is a sequence k1, v1, k2, v2, k3, v3, ... If kvs has "
             "an odd number of elements, an error will be thrown. Returns the "
             "new table.")
    },
    {
        "array", janet_core_array,
        JDOC("(array & items)\n\n"
             "Create a new array that contains items. Returns the new array.")
    },
    {
        "scan-number", janet_core_scannumber,
        JDOC("(scan-number str)\n\n"
             "Parse a number from a byte sequence an return that number, either and integer "
             "or a real. The number "
             "must be in the same format as numbers in janet source code. Will return nil "
             "on an invalid number.")
    },
    {
        "tuple", janet_core_tuple,
        JDOC("(tuple & items)\n\n"
             "Creates a new tuple that contains items. Returns the new tuple.")
    },
    {
        "struct", janet_core_struct,
        JDOC("(struct & kvs)\n\n"
             "Create a new struct from a sequence of key value pairs. "
             "kvs is a sequence k1, v1, k2, v2, k3, v3, ... If kvs has "
             "an odd number of elements, an error will be thrown. Returns the "
             "new struct.")
    },
    {
        "gensym", janet_core_gensym,
        JDOC("(gensym)\n\n"
             "Returns a new symbol that is unique across the runtime. This means it "
             "will not collide with any already created symbols during compilation, so "
             "it can be used in macros to generate automatic bindings.")
    },
    {
        "gccollect", janet_core_gccollect,
        JDOC("(gccollect)\n\n"
             "Run garbage collection. You should probably not call this manually.")
    },
    {
        "gcsetinterval", janet_core_gcsetinterval,
        JDOC("(gcsetinterval interval)\n\n"
             "Set an integer number of bytes to allocate before running garbage collection. "
             "Low valuesi for interval will be slower but use less memory. "
             "High values will be faster but use more memory.")
    },
    {
        "gcinterval", janet_core_gcinterval,
        JDOC("(gcinterval)\n\n"
             "Returns the integer number of bytes to allocate before running an iteration "
             "of garbage collection.")
    },
    {
        "type", janet_core_type,
        JDOC("(type x)\n\n"
             "Returns the type of x as a keyword symbol. x is one of\n"
             "\t:nil\n"
             "\t:boolean\n"
             "\t:integer\n"
             "\t:real\n"
             "\t:array\n"
             "\t:tuple\n"
             "\t:table\n"
             "\t:struct\n"
             "\t:string\n"
             "\t:buffer\n"
             "\t:symbol\n"
             "\t:keyword\n"
             "\t:function\n"
             "\t:cfunction\n\n"
             "or another symbol for an abstract type.")
    },
    {
        "next", janet_core_next,
        JDOC("(next dict key)\n\n"
             "Gets the next key in a struct or table. Can be used to iterate through "
             "the keys of a data structure in an unspecified order. Keys are guaranteed "
             "to be seen only once per iteration if they data structure is not mutated "
             "during iteration. If key is nil, next returns the first key. If next "
             "returns nil, there are no more keys to iterate through. ")
    },
    {
        "hash", janet_core_hash,
        JDOC("(hash value)\n\n"
             "Gets a hash value for any janet value. The hash is an integer can be used "
             "as a cheap hash function for all janet objects. If two values are strictly equal, "
             "then they will have the same hash value.")
    },
    {
        "getline", janet_core_getline,
        JDOC("(getline [, prompt=\"\" [, buffer=@\"\"]])\n\n"
             "Reads a line of input into a buffer, including the newline character, using a prompt. Returns the modified buffer. "
             "Use this function to implement a simple interface for a terminal program.")
    },
    {NULL, NULL, NULL}
};

#ifdef JANET_BOOTSTRAP

/* Utility for inline assembly */
static void janet_quick_asm(
    JanetTable *env,
    int32_t flags,
    const char *name,
    int32_t arity,
    int32_t min_arity,
    int32_t max_arity,
    int32_t slots,
    const uint32_t *bytecode,
    size_t bytecode_size,
    const char *doc) {
    JanetFuncDef *def = janet_funcdef_alloc();
    def->arity = arity;
    def->min_arity = min_arity;
    def->max_arity = max_arity;
    def->flags = flags;
    def->slotcount = slots;
    def->bytecode = malloc(bytecode_size);
    def->bytecode_length = (int32_t)(bytecode_size / sizeof(uint32_t));
    def->name = janet_cstring(name);
    if (!def->bytecode) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(def->bytecode, bytecode, bytecode_size);
    janet_def(env, name, janet_wrap_function(janet_thunk(def)), doc);
}

/* Macros for easier inline janet assembly */
#define SSS(op, a, b, c) ((op) | ((a) << 8) | ((b) << 16) | ((c) << 24))
#define SS(op, a, b) ((op) | ((a) << 8) | ((b) << 16))
#define SSI(op, a, b, I) ((op) | ((a) << 8) | ((b) << 16) | ((uint32_t)(I) << 24))
#define S(op, a) ((op) | ((a) << 8))
#define SI(op, a, I) ((op) | ((a) << 8) | ((uint32_t)(I) << 16))

/* Templatize a varop */
static void templatize_varop(
    JanetTable *env,
    int32_t flags,
    const char *name,
    int32_t nullary,
    int32_t unary,
    uint32_t op,
    const char *doc) {

    /* Variadic operator assembly. Must be templatized for each different opcode. */
    /* Reg 0: Argument tuple (args) */
    /* Reg 1: Argument count (argn) */
    /* Reg 2: Jump flag (jump?) */
    /* Reg 3: Accumulator (accum) */
    /* Reg 4: Next operand (operand) */
    /* Reg 5: Loop iterator (i) */
    uint32_t varop_asm[] = {
        SS(JOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */

        /* Check nullary */
        SSS(JOP_EQUALS_IMMEDIATE, 2, 1, 0), /* Check if numargs equal to 0 */
        SI(JOP_JUMP_IF_NOT, 2, 3), /* If not 0, jump to next check */
        /* Nullary */
        SI(JOP_LOAD_INTEGER, 3, nullary),  /* accum = nullary value */
        S(JOP_RETURN, 3), /* return accum */

        /* Check unary */
        SSI(JOP_EQUALS_IMMEDIATE, 2, 1, 1), /* Check if numargs equal to 1 */
        SI(JOP_JUMP_IF_NOT, 2, 5), /* If not 1, jump to next check */
        /* Unary */
        SI(JOP_LOAD_INTEGER, 3, unary), /* accum = unary value */
        SSI(JOP_GET_INDEX, 4, 0, 0), /* operand = args[0] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        S(JOP_RETURN, 3), /* return accum */

        /* Mutli (2 or more) arity */
        /* Prime loop */
        SSI(JOP_GET_INDEX, 3, 0, 0), /* accum = args[0] */
        SI(JOP_LOAD_INTEGER, 5, 1), /* i = 1 */
        /* Main loop */
        SSS(JOP_GET, 4, 0, 5), /* operand = args[i] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        SSI(JOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SSI(JOP_EQUALS, 2, 5, 1), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF_NOT, 2, -4), /* if not jump? go back 4 */

        /* Done, do last and return accumulator */
        S(JOP_RETURN, 3) /* return accum */
    };

    janet_quick_asm(
        env,
        flags | JANET_FUNCDEF_FLAG_VARARG,
        name,
        0,
        0,
        INT32_MAX,
        6,
        varop_asm,
        sizeof(varop_asm),
        doc);
}

/* Templatize variadic comparators */
static void templatize_comparator(
    JanetTable *env,
    int32_t flags,
    const char *name,
    int invert,
    uint32_t op,
    const char *doc) {

    /* Reg 0: Argument tuple (args) */
    /* Reg 1: Argument count (argn) */
    /* Reg 2: Jump flag (jump?) */
    /* Reg 3: Last value (last) */
    /* Reg 4: Next operand (next) */
    /* Reg 5: Loop iterator (i) */
    uint32_t comparator_asm[] = {
        SS(JOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */
        SSS(JOP_LESS_THAN_IMMEDIATE, 2, 1, 2), /* Check if numargs less than 2 */
        SI(JOP_JUMP_IF, 2, 10), /* If numargs < 2, jump to done */

        /* Prime loop */
        SSI(JOP_GET_INDEX, 3, 0, 0), /* last = args[0] */
        SI(JOP_LOAD_INTEGER, 5, 1), /* i = 1 */

        /* Main loop */
        SSS(JOP_GET, 4, 0, 5), /* next = args[i] */
        SSS(op, 2, 3, 4), /* jump? = last compare next */
        SI(JOP_JUMP_IF_NOT, 2, 7), /* if not jump? goto fail (return false) */
        SSI(JOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SS(JOP_MOVE_NEAR, 3, 4), /* last = next */
        SSI(JOP_EQUALS, 2, 5, 1), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF_NOT, 2, -6), /* if not jump? go back 6 */

        /* Done, return true */
        S(invert ? JOP_LOAD_FALSE : JOP_LOAD_TRUE, 3),
        S(JOP_RETURN, 3),

        /* Failed, return false */
        S(invert ? JOP_LOAD_TRUE : JOP_LOAD_FALSE, 3),
        S(JOP_RETURN, 3)
    };

    janet_quick_asm(
        env,
        flags | JANET_FUNCDEF_FLAG_VARARG,
        name,
        0,
        0,
        INT32_MAX,
        6,
        comparator_asm,
        sizeof(comparator_asm),
        doc);
}

/* Make the apply function */
static void make_apply(JanetTable *env) {
    /* Reg 0: Function (fun) */
    /* Reg 1: Argument tuple (args) */
    /* Reg 2: Argument count (argn) */
    /* Reg 3: Jump flag (jump?) */
    /* Reg 4: Loop iterator (i) */
    /* Reg 5: Loop values (x) */
    uint32_t apply_asm[] = {
        SS(JOP_LENGTH, 2, 1),
        SSS(JOP_EQUALS_IMMEDIATE, 3, 2, 0), /* Immediate tail call if no args */
        SI(JOP_JUMP_IF, 3, 9),

        /* Prime loop */
        SI(JOP_LOAD_INTEGER, 4, 0), /* i = 0 */

        /* Main loop */
        SSS(JOP_GET, 5, 1, 4), /* x = args[i] */
        SSI(JOP_ADD_IMMEDIATE, 4, 4, 1), /* i++ */
        SSI(JOP_EQUALS, 3, 4, 2), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF, 3, 3), /* if jump? go forward 3 */
        S(JOP_PUSH, 5),
        (JOP_JUMP | ((uint32_t)(-5) << 8)),

        /* Push the array */
        S(JOP_PUSH_ARRAY, 5),

        /* Call the funciton */
        S(JOP_TAILCALL, 0)
    };
    janet_quick_asm(env, JANET_FUN_APPLY | JANET_FUNCDEF_FLAG_VARARG,
                    "apply", 1, 1, INT32_MAX, 6, apply_asm, sizeof(apply_asm),
                    JDOC("(apply f & args)\n\n"
                         "Applies a function to a variable number of arguments. Each element in args "
                         "is used as an argument to f, except the last element in args, which is expected to "
                         "be an array-like. Each element in this last argument is then also pushed as an argument to "
                         "f. For example:\n\n"
                         "\t(apply + 1000 (range 10))\n\n"
                         "sums the first 10 integers and 1000.)"));
}

static const uint32_t error_asm[] = {
    JOP_ERROR
};
static const uint32_t debug_asm[] = {
    JOP_SIGNAL | (2 << 24),
    JOP_RETURN_NIL
};
static const uint32_t yield_asm[] = {
    JOP_SIGNAL | (3 << 24),
    JOP_RETURN
};
static const uint32_t resume_asm[] = {
    JOP_RESUME | (1 << 24),
    JOP_RETURN
};
static const uint32_t get_asm[] = {
    JOP_GET | (1 << 24),
    JOP_RETURN
};
static const uint32_t put_asm[] = {
    JOP_PUT | (1 << 16) | (2 << 24),
    JOP_RETURN
};
static const uint32_t length_asm[] = {
    JOP_LENGTH,
    JOP_RETURN
};
static const uint32_t bnot_asm[] = {
    JOP_BNOT,
    JOP_RETURN
};
#endif /* ifndef JANET_NO_BOOTSTRAP */

JanetTable *janet_core_env(JanetTable *replacements) {
    JanetTable *env = (NULL != replacements) ? replacements : janet_table(0);
    janet_core_cfuns(env, NULL, corelib_cfuns);

#ifdef JANET_BOOTSTRAP
    janet_quick_asm(env, JANET_FUN_DEBUG,
                    "debug", 0, 0, 0, 1, debug_asm, sizeof(debug_asm),
                    JDOC("(debug)\n\n"
                         "Throws a debug signal that can be caught by a parent fiber and used to inspect "
                         "the running state of the current fiber. Returns nil."));
    janet_quick_asm(env, JANET_FUN_ERROR,
                    "error", 1, 1, 1, 1, error_asm, sizeof(error_asm),
                    JDOC("(error e)\n\n"
                         "Throws an error e that can be caught and handled by a parent fiber."));
    janet_quick_asm(env, JANET_FUN_YIELD,
                    "yield", 1, 0, 1, 2, yield_asm, sizeof(yield_asm),
                    JDOC("(yield x)\n\n"
                         "Yield a value to a parent fiber. When a fiber yields, its execution is paused until "
                         "another thread resumes it. The fiber will then resume, and the last yield call will "
                         "return the value that was passed to resume."));
    janet_quick_asm(env, JANET_FUN_RESUME,
                    "resume", 2, 1, 2, 2, resume_asm, sizeof(resume_asm),
                    JDOC("(resume fiber &opt x)\n\n"
                         "Resume a new or suspended fiber and optionally pass in a value to the fiber that "
                         "will be returned to the last yield in the case of a pending fiber, or the argument to "
                         "the dispatch function in the case of a new fiber. Returns either the return result of "
                         "the fiber's dispatch function, or the value from the next yield call in fiber."));
    janet_quick_asm(env, JANET_FUN_GET,
                    "get", 2, 2, 2, 2, get_asm, sizeof(get_asm),
                    JDOC("(get ds key)\n\n"
                         "Get a value from any associative data structure. Arrays, tuples, tables, structs, strings, "
                         "symbols, and buffers are all associative and can be used with get. Order structures, name "
                         "arrays, tuples, strings, buffers, and symbols must use integer keys. Structs and tables can "
                         "take any value as a key except nil and return a value except nil. Byte sequences will return "
                         "integer representations of bytes as result of a get call."));
    janet_quick_asm(env, JANET_FUN_PUT,
                    "put", 3, 3, 3, 3, put_asm, sizeof(put_asm),
                    JDOC("(put ds key value)\n\n"
                         "Associate a key with a value in any mutable associative data structure. Indexed data structures "
                         "(arrays and buffers) only accept non-negative integer keys, and will expand if an out of bounds "
                         "value is provided. In an array, extra space will be filled with nils, and in a buffer, extra "
                         "space will be filled with 0 bytes. In a table, putting a key that is contained in the table prototype "
                         "will hide the association defined by the prototype, but will not mutate the prototype table. Putting "
                         "a value nil into a table will remove the key from the table. Returns the data structure ds."));
    janet_quick_asm(env, JANET_FUN_LENGTH,
                    "length", 1, 1, 1, 1, length_asm, sizeof(length_asm),
                    JDOC("(length ds)\n\n"
                         "Returns the length or count of a data structure in constant time as an integer. For "
                         "structs and tables, returns the number of key-value pairs in the data structure."));
    janet_quick_asm(env, JANET_FUN_BNOT,
                    "bnot", 1, 1, 1, 1, bnot_asm, sizeof(bnot_asm),
                    JDOC("(bnot x)\n\nReturns the bit-wise inverse of integer x."));
    make_apply(env);

    /* Variadic ops */
    templatize_varop(env, JANET_FUN_ADD, "+", 0, 0, JOP_ADD,
                     JDOC("(+ & xs)\n\n"
                          "Returns the sum of all xs. xs must be integers or real numbers only. If xs is empty, return 0."));
    templatize_varop(env, JANET_FUN_SUBTRACT, "-", 0, 0, JOP_SUBTRACT,
                     JDOC("(- & xs)\n\n"
                          "Returns the difference of xs. If xs is empty, returns 0. If xs has one element, returns the "
                          "negative value of that element. Otherwise, returns the first element in xs minus the sum of "
                          "the rest of the elements."));
    templatize_varop(env, JANET_FUN_MULTIPLY, "*", 1, 1, JOP_MULTIPLY,
                     JDOC("(* & xs)\n\n"
                          "Returns the product of all elements in xs. If xs is empty, returns 1."));
    templatize_varop(env, JANET_FUN_DIVIDE, "/", 1, 1, JOP_DIVIDE,
                     JDOC("(/ & xs)\n\n"
                          "Returns the quotient of xs. If xs is empty, returns 1. If xs has one value x, returns "
                          "the reciprocal of x. Otherwise return the first value of xs repeatedly divided by the remaining "
                          "values. Division by two integers uses truncating division."));
    templatize_varop(env, JANET_FUN_BAND, "band", -1, -1, JOP_BAND,
                     JDOC("(band & xs)\n\n"
                          "Returns the bit-wise and of all values in xs. Each x in xs must be an integer."));
    templatize_varop(env, JANET_FUN_BOR, "bor", 0, 0, JOP_BOR,
                     JDOC("(bor & xs)\n\n"
                          "Returns the bit-wise or of all values in xs. Each x in xs must be an integer."));
    templatize_varop(env, JANET_FUN_BXOR, "bxor", 0, 0, JOP_BXOR,
                     JDOC("(bxor & xs)\n\n"
                          "Returns the bit-wise xor of all values in xs. Each in xs must be an integer."));
    templatize_varop(env, JANET_FUN_LSHIFT, "blshift", 1, 1, JOP_SHIFT_LEFT,
                     JDOC("(blshift x & shifts)\n\n"
                          "Returns the value of x bit shifted left by the sum of all values in shifts. x "
                          "and each element in shift must be an integer."));
    templatize_varop(env, JANET_FUN_RSHIFT, "brshift", 1, 1, JOP_SHIFT_RIGHT,
                     JDOC("(brshift x & shifts)\n\n"
                          "Returns the value of x bit shifted right by the sum of all values in shifts. x "
                          "and each element in shift must be an integer."));
    templatize_varop(env, JANET_FUN_RSHIFTU, "brushift", 1, 1, JOP_SHIFT_RIGHT_UNSIGNED,
                     JDOC("(brushift x & shifts)\n\n"
                          "Returns the value of x bit shifted right by the sum of all values in shifts. x "
                          "and each element in shift must be an integer. The sign of x is not preserved, so "
                          "for positive shifts the return value will always be positive."));

    /* Variadic comparators */
    templatize_comparator(env, JANET_FUN_ORDER_GT, "order>", 0, JOP_GREATER_THAN,
                          JDOC("(order> & xs)\n\n"
                               "Check if xs is strictly descending according to a total order "
                               "over all values. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_ORDER_LT, "order<", 0, JOP_LESS_THAN,
                          JDOC("(order< & xs)\n\n"
                               "Check if xs is strictly increasing according to a total order "
                               "over all values. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_ORDER_GTE, "order>=", 1, JOP_LESS_THAN,
                          JDOC("(order>= & xs)\n\n"
                               "Check if xs is not increasing according to a total order "
                               "over all values. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_ORDER_LTE, "order<=", 1, JOP_GREATER_THAN,
                          JDOC("(order<= & xs)\n\n"
                               "Check if xs is not decreasing according to a total order "
                               "over all values. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_ORDER_EQ, "=", 0, JOP_EQUALS,
                          JDOC("(= & xs)\n\n"
                               "Returns true if all values in xs are the same, false otherwise."));
    templatize_comparator(env, JANET_FUN_ORDER_NEQ, "not=", 1, JOP_EQUALS,
                          JDOC("(not= & xs)\n\n"
                               "Return true if any values in xs are not equal, otherwise false."));
    templatize_comparator(env, JANET_FUN_GT, ">", 0, JOP_NUMERIC_GREATER_THAN,
                          JDOC("(> & xs)\n\n"
                               "Check if xs is in numerically descending order. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_LT, "<", 0, JOP_NUMERIC_LESS_THAN,
                          JDOC("(< & xs)\n\n"
                               "Check if xs is in numerically ascending order. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_GTE, ">=", 0, JOP_NUMERIC_GREATER_THAN_EQUAL,
                          JDOC("(>= & xs)\n\n"
                               "Check if xs is in numerically non-ascending order. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_LTE, "<=", 0, JOP_NUMERIC_LESS_THAN_EQUAL,
                          JDOC("(<= & xs)\n\n"
                               "Check if xs is in numerically non-descending order. Returns a boolean."));
    templatize_comparator(env, JANET_FUN_EQ, "==", 0, JOP_NUMERIC_EQUAL,
                          JDOC("(== & xs)\n\n"
                               "Check if all values in xs are numerically equal (4.0 == 4). Returns a boolean."));
    templatize_comparator(env, JANET_FUN_NEQ, "not==", 1, JOP_NUMERIC_EQUAL,
                          JDOC("(not== & xs)\n\n"
                               "Check if any values in xs are not numerically equal (3.0 not== 4). Returns a boolean."));

    /* Platform detection */
    janet_def(env, "janet/version", janet_cstringv(JANET_VERSION),
              JDOC("The version number of the running janet program."));
    janet_def(env, "janet/build", janet_cstringv(JANET_BUILD),
              JDOC("The build identifier of the running janet program."));

    /* Allow references to the environment */
    janet_def(env, "_env", janet_wrap_table(env), JDOC("The environment table for the current scope."));

    /* Set as gc root */
    janet_gcroot(janet_wrap_table(env));
#endif

    /* Load auxiliary envs */
    janet_lib_io(env);
    janet_lib_math(env);
    janet_lib_array(env);
    janet_lib_tuple(env);
    janet_lib_buffer(env);
    janet_lib_table(env);
    janet_lib_fiber(env);
    janet_lib_os(env);
    janet_lib_parse(env);
    janet_lib_compile(env);
    janet_lib_debug(env);
    janet_lib_string(env);
    janet_lib_marsh(env);
#ifdef JANET_PEG
    janet_lib_peg(env);
#endif
#ifdef JANET_ASSEMBLER
    janet_lib_asm(env);
#endif
#ifdef JANET_TYPED_ARRAY
    janet_lib_typed_array(env);
#endif
#ifdef JANET_BIGINT
    janet_lib_bigint(env);
#endif

    
#ifdef JANET_BOOTSTRAP
    /* Run bootstrap source */
    janet_dobytes(env, janet_gen_core, janet_gen_core_size, "core.janet", NULL);
#else

    /* Unmarshal from core image */
    Janet marsh_out = janet_unmarshal(
                          janet_core_image,
                          janet_core_image_size,
                          0,
                          env,
                          NULL);
    janet_gcroot(marsh_out);
    env = janet_unwrap_table(marsh_out);
#endif

    return env;
}
