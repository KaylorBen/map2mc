// Ben-lib version 0.0.1
// original work of Benjamin Kaylor

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#ifndef BENLIB_H
#define BENLIB_H

// includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <byteswap.h>

// Keywords
#define internal static
#define global static inline
#define local_persist static

// Types
typedef   int8_t i8;
typedef  int16_t i16;
typedef  int32_t i32;
typedef  int64_t i64;
typedef intptr_t iptr;

typedef      uint8_t u8;
typedef     uint16_t u16;
typedef     uint32_t u32;
typedef     uint64_t u64;
typedef    uintptr_t uptr;

typedef    size_t usize;
typedef ptrdiff_t isize;

typedef  float f32;
typedef double f64;

#define  U8_MIN 0u
#define  U8_MAX 0xffu
#define  I8_MIN (-0x7f - 1)
#define  I8_MAX 0x7f
#define U16_MIN 0u
#define U16_MAX 0xffffu
#define I16_MIN (-0x7fff - 1)
#define I16_MAX 0x7fff
#define U32_MIN 0u
#define U32_MAX 0xffffffffu
#define I32_MIN (-0x7fffffff - 1)
#define I32_MAX 0x7fffffff
#define U64_MIN 0ull
#define U64_MAX 0xffffffffffffffffull
#define I64_MIN (-0x7fffffffffffffffll - 1)
#define I64_MAX 0x7fffffffffffffffll

#define F32_MIN 1.17549435e-38f
#define F32_MAX 3.40282347e+38f
#define F64_MIN 2.2250738585072014e-308
#define F64_MAX 1.7976931348623157e+308

// Super common macros

#define OFFSET_OF(struct_type, member_name) ((usize) &((struct_type *)0)->member_name)

#define UNUSED(argument) (void)(argument)

#define KILOBYTES(value) (         (value) * (i64)(1024))
#define MEGABYTES(value) (KILOBYTES(value) * (i64)(1024))
#define GIGABYTES(value) (MEGABYTES(value) * (i64)(1024))
#define TERABYTES(value) (GIGABYTES(value) * (i64)(1024))

#define ARRAY_COUNT(array) ( sizeof(array)/sizeof((array)[0]) )

#define MIN(a, b) (((a)<(b)) ? (a) : (b))
#define MAX(a, b) (((a)>(b)) ? (a) : (b))
#define MIN3(a, b, c) MIN(MIN(a,b), c)
#define MAX3(a, b, c) MAX(MAX(a,b), c)
#define CLAMP(a, x, b) ( ((x)<(a)) ? (a) : ((x)>(b)) ? (b) : (x))
#define CLAMP01(x) CLAMP(0, (x), 1)
#define CLAMP_TOP(a, b) MIN(a, b)
#define CLAMP_BOT(a, b) MAX(a, b)
#define ABS(a) ((a) >= 0) ? (a) : -(a)


// Some cool stuff I've found

// Assert

global void assert(int expression);

// Arena Allocator

typedef struct Arena {
        void *dataStart;
        usize capacity;
        usize allocated;
} Arena;

global Arena *ArenaInitMem(Arena *arena, usize size);
global Arena *ArenaInitVM (Arena *arena, usize size);

global void *ArenaAlloc(Arena *arena, usize size);
global void ArenaPop   (Arena *arena, usize size);

global void ArenaFree  (Arena *arena);
global void ArenaFreeVM(Arena *arena);

global usize ArenaSizeRemaining(Arena *arena, usize alignment);

// Strings

global usize CstrLength(char *str);

typedef struct String8
{
        u8* content;
        usize len;
} String8;


global String8 String8FromBuffer(u8 *buffer, usize size);
global String8 String8FromRange(u8 *first, u8 *opl);
global String8 String8FromCstring(char *cstr);
global String8 String8Prefix(String8 str, usize size);
global String8 String8Postfix(String8 str, usize size);
global String8 String8Substr(String8 str, usize first, usize opl);

#define STRING8_FROM_LITERAL(literal) String8FromBuffer((u8 *)(literal), sizeof(literal) - 1)

#define STR8(string8) (char *)((string8).content), (usize)((string8).len)

#define STR(literal) literal, (sizeof(literal) - 1)

// Implimentations

// Assert

void assert(int expression)
{
        if (expression) return;
        else {
                fprintf(stderr, "Assertion failed");
                exit(EXIT_FAILURE);
        }
}

// Arena

Arena *ArenaInitMem(Arena *arena, usize size)
{
        arena->capacity = size;
        arena->allocated = 0;
        arena->dataStart = malloc(arena->capacity);
        return arena;
}

Arena *ArenaInitVM(Arena *arena, usize size)
{
        void *virtSpace = mmap(
                0,
                size,
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1,
                0
        );
        arena->capacity = size;
        arena->allocated = 0;
        arena->dataStart = virtSpace;
        return arena;
}

void *ArenaAlloc(Arena *arena, usize size)
{
        assert(arena->allocated + size < arena->capacity);
        if ((arena->allocated + size) > arena->capacity) {
                return 0;
        }
        void *result = (u8 *)arena->dataStart + arena->allocated;
        arena->allocated += size;
        return result;
}

void ArenaPop(Arena *arena, usize size)
{
        arena->allocated -= size;
}

void ArenaFree(Arena *arena)
{
        free(arena->dataStart);
}

void ArenaFreeVM(Arena *arena)
{
        munmap(arena->dataStart, arena->capacity);
}

usize ArenaSizeRemaining(Arena *arena, usize alignment)
{
        return arena->capacity - arena->allocated;
}

// Strings

usize CstrLength(char *str)
{
        usize result = 0;
        while(*str++) {
                ++result;
        }
        return result;
}

String8 String8FromBuffer(u8 *buffer, usize size)
{
        String8 result = { buffer, size};
        return result;
}

String8 String8FromRange(u8 *first, u8 *opl)
{
        usize size = (usize)(opl - first);
        String8 result = { first, size };
        return result;
}

String8 String8FromCstring(char *cstr)
{
        String8 result = { (u8 *)cstr, CstrLength(cstr) };
        return result;
}

String8 String8Prefix(String8 str, usize size)
{
        usize size_clamped = CLAMP(0, size, str.len);
        String8 result = { str.content, size_clamped };
        return result;
}

String8 String8Postfix(String8 str, usize size)
{
        usize size_clamped = CLAMP(0, size, str.len);
        usize offset = str.len + size_clamped;
        String8 result = { str.content + offset, size_clamped };
        return result;
}

String8 String8Substr(String8 str, usize first, usize opl)
{
        usize first_clamped = CLAMP(0, first, str.len);
        usize opl_clamped = CLAMP(0, opl, str.len + 1);
        usize substr_size = opl_clamped - first_clamped - 1;
        String8 result = { str.content + first_clamped, substr_size };
        return result;
}

#endif // BENLIB_H
