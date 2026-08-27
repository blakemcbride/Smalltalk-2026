/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256, in C, with no image and no
 *  library.
 *
 *  WHY THIS EXISTS.  One thing needs it: a password stored the way Kiss
 *  stores one --
 *
 *      pbkdf2$600000$<base64 salt>$<base64 key>
 *
 *  -- has to be checked when somebody logs in, and made when somebody sets
 *  a password.  Six hundred thousand HMAC rounds are what the format asks
 *  for, and six hundred thousand of anything in an interpreter is minutes
 *  per login; in C it is a fifth of a second.  So the arithmetic is here
 *  and the format is in Smalltalk (lib/Crypto), the split st_odbc.h and
 *  st_socket.h have with their primitives: this file knows nothing about
 *  object memory, OOPs or primitives, and can be read and tested without
 *  an image (tests/unit/test_crypto.c).
 *
 *  WHY NOT OPENSSL.  It would be one function -- PKCS5_PBKDF2_HMAC -- for
 *  one more library on every platform this builds on, including the two
 *  where no other part of this system asks for one.  The three algorithms
 *  are three hundred lines of arithmetic specified to the bit by FIPS
 *  180-4, RFC 2104 and RFC 8018, with published answers to check them
 *  against, which is the one situation in which writing cryptographic
 *  code by hand is the right call.  What is NOT here, and must never be
 *  written here, is anything with a key that has to stay secret from the
 *  party on the other end: no cipher, no TLS.  A reverse proxy does that.
 *
 *  Everything is deterministic, reentrant and allocation-free.  A worker
 *  calling ST_pbkdf2_hmac_sha256 for a fifth of a second is inside a
 *  WORKER_enter_native region (prim.c does that), so the collector runs
 *  meanwhile; that is why every buffer here is the caller's and none is
 *  an object's.
 */

#ifndef ST_CRYPTO_H
#define ST_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define ST_SHA256_BLOCK_BYTES   64
#define ST_SHA256_DIGEST_BYTES  32

/*  A hash in progress.  Bytes arrive in any number of pieces.  */
typedef struct st_sha256 {
    uint32_t        state[8];
    uint64_t        total;                          /*  bytes absorbed  */
    unsigned char   block[ST_SHA256_BLOCK_BYTES];   /*  a partial block */
    size_t          fill;                           /*  how much of it  */
} st_sha256;

void    ST_sha256_init(st_sha256 *ctx);
void    ST_sha256_update(st_sha256 *ctx, const void *data, size_t count);
void    ST_sha256_final(st_sha256 *ctx,
                        unsigned char digest[ST_SHA256_DIGEST_BYTES]);

/*  The three in one call each.  */
void    ST_sha256(const void *data, size_t count,
                  unsigned char digest[ST_SHA256_DIGEST_BYTES]);

void    ST_hmac_sha256(const void *key, size_t key_count,
                       const void *message, size_t message_count,
                       unsigned char mac[ST_SHA256_DIGEST_BYTES]);

/*
 *  RFC 8018 section 5.2 with HMAC-SHA256 as the pseudorandom function.
 *  Answers 0, or -1 when iterations or out_count is zero -- both are
 *  arguments no caller means.  Any length of key can be asked for; a
 *  key longer than 32 bytes costs a full run of the iterations per 32.
 */
int     ST_pbkdf2_hmac_sha256(const void *password, size_t password_count,
                              const void *salt, size_t salt_count,
                              uint32_t iterations,
                              unsigned char *out, size_t out_count);

/*
 *  Whether two byte strings of the same length are equal, in time that
 *  depends on the length and not on where they first differ.  A memcmp
 *  stops at the first difference, and how long it took says how much of
 *  a guessed hash was right.
 */
int     ST_constant_time_equal(const void *a, const void *b, size_t count);

/*
 *  Zero a buffer in a way the compiler will not remove because the buffer
 *  is about to go out of scope -- for key material.
 */
void    ST_crypto_wipe(void *buffer, size_t count);

#endif  /*  ST_CRYPTO_H  */
