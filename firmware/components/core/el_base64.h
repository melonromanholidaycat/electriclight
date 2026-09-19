#pragma once
#include <stddef.h>
#include <stdint.h>

// Decoder for the base64 web/src/lang/serialize.js encodes with.
//
// In components/core rather than beside the HTTP handler that uses it, for one
// reason: it has to invert the browser's encoder exactly, and here it can be
// tested against the same golden vectors the evaluator is held to.

// Returns the number of bytes written, or -1 if the input holds a character
// that is neither in the alphabet nor padding, or if the output would overflow.
int el_base64_decode(const char *in, uint8_t *out, size_t out_max);
