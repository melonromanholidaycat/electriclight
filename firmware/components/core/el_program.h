#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "el_eval.h"

// Decoder for the ELFX wire format written by web/src/lang/serialize.js:
//
//   magic  "ELFX"        4 bytes
//   u8     version
//   u8     nLocals
//   u8     stack
//   u8     nParams
//   u16LE  nConsts
//   u16LE  codeLen
//   f32LE  consts[nConsts]
//   u8     code[codeLen]
//
// This is the only thing the firmware parses. Effect source text travels
// alongside as an opaque blob for the editor and is never read here - the
// expression compiler lives in the browser and stays there.

#define EL_FORMAT_VERSION 1

// Bounded so a program can be held in a fixed buffer with no allocation. A
// compiled effect is a few hundred bytes; 4 KB is room to be wrong about that.
#define EL_MAX_PROGRAM_BYTES 4096
#define EL_MAX_CONSTS        256
#define EL_MAX_PARAMS        32

typedef enum {
    EL_DECODE_OK = 0,
    EL_DECODE_SHORT,        // fewer bytes than the header claims
    EL_DECODE_MAGIC,        // not an effect program
    EL_DECODE_VERSION,      // a format this firmware predates
    EL_DECODE_LIMITS,       // needs more stack, locals, consts or params than we have
    EL_DECODE_BAD_CODE,     // an opcode, operand or stack depth that cannot be right
} el_decode_err_t;

// Everything a decoded program owns, so callers hold one struct and no pointers
// into a buffer that might be reused.
typedef struct {
    el_program_t program;
    float consts[EL_MAX_CONSTS];
    uint8_t code[EL_MAX_PROGRAM_BYTES];
} el_effect_t;

el_decode_err_t el_program_decode(el_effect_t *out, const uint8_t *bytes, size_t len);

const char *el_decode_error(el_decode_err_t err);
