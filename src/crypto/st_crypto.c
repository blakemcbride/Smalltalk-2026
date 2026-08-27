/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  SHA-256 (FIPS 180-4), HMAC (RFC 2104) and PBKDF2 (RFC 8018), written
 *  from the specifications and checked against their published answers
 *  and against an independent implementation (tests/unit/test_crypto.c
 *  says which).  See st_crypto.h for why they are here at all.
 *
 *  The one thing in this file that is not a transcription of a standard
 *  is the shape of the PBKDF2 loop.  Each iteration is an HMAC of a
 *  32-byte value under the same key, and an HMAC is two hashes whose first
 *  block is the padded key both times.  So the two states after absorbing
 *  that block are computed once and copied per iteration, and an iteration
 *  costs two compressions instead of four -- which is the difference
 *  between a fifth of a second and two fifths for the six hundred thousand
 *  the stored format asks for.  It is the optimisation every library
 *  makes, and the vectors confirm it changes nothing but the time.
 */

#include "st_crypto.h"

#include <string.h>

/*  ----------  SHA-256  ----------  */

/*  The first 32 bits of the fractional parts of the cube roots of the
 *  first 64 primes (FIPS 180-4, 4.2.2).  */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/*  The first 32 bits of the fractional parts of the square roots of the
 *  first 8 primes (FIPS 180-4, 5.3.3).  */
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)    (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSIG1(x)    (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSIG0(x)    (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SSIG1(x)    (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static uint32_t
load_be32(const unsigned char *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8)  |  (uint32_t) p[3];
}

static void
store_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

static void
store_be64(unsigned char *p, uint64_t v)
{
    store_be32(p, (uint32_t) (v >> 32));
    store_be32(p + 4, (uint32_t) v);
}

/*  One 64-byte block into the state: FIPS 180-4, 6.2.2.  */
static void
sha256_compress(uint32_t state[8], const unsigned char block[64])
{
    uint32_t    w[64];
    uint32_t    a, b, c, d, e, f, g, h;
    uint32_t    t1, t2;
    int         i;

    for (i = 0; i < 16; ++i)
        w[i] = load_be32(block + 4 * i);
    for (; i < 64; ++i)
        w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + BSIG1(e) + CH(e, f, g) + K[i] + w[i];
        t2 = BSIG0(a) + MAJ(a, b, c);
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void
ST_sha256_init(st_sha256 *ctx)
{
    memcpy(ctx->state, H0, sizeof H0);
    ctx->total = 0;
    ctx->fill  = 0;
}

void
ST_sha256_update(st_sha256 *ctx, const void *data, size_t count)
{
    const unsigned char    *p = (const unsigned char *) data;

    ctx->total += count;

    /*  Finish a partial block first, if there is one and this fills it.  */
    if (ctx->fill > 0) {
        size_t  room = ST_SHA256_BLOCK_BYTES - ctx->fill;

        if (count < room) {
            memcpy(ctx->block + ctx->fill, p, count);
            ctx->fill += count;
            return;
        }
        memcpy(ctx->block + ctx->fill, p, room);
        sha256_compress(ctx->state, ctx->block);
        p     += room;
        count -= room;
        ctx->fill = 0;
    }

    /*  Whole blocks straight from the caller's memory.  */
    while (count >= ST_SHA256_BLOCK_BYTES) {
        sha256_compress(ctx->state, p);
        p     += ST_SHA256_BLOCK_BYTES;
        count -= ST_SHA256_BLOCK_BYTES;
    }

    /*  What is left waits for more, or for final.  */
    if (count > 0) {
        memcpy(ctx->block, p, count);
        ctx->fill = count;
    }
}

void
ST_sha256_final(st_sha256 *ctx, unsigned char digest[ST_SHA256_DIGEST_BYTES])
{
    uint64_t    bits = ctx->total * 8;
    size_t      i;

    /*
     *  A 1 bit, zeros to 56 mod 64, the length in bits as 64 big-endian
     *  bits (FIPS 180-4, 5.1.1).  If the 1 bit lands past byte 55 the
     *  length needs a block of its own.
     */
    ctx->block[ctx->fill++] = 0x80;
    if (ctx->fill > 56) {
        memset(ctx->block + ctx->fill, 0, ST_SHA256_BLOCK_BYTES - ctx->fill);
        sha256_compress(ctx->state, ctx->block);
        ctx->fill = 0;
    }
    memset(ctx->block + ctx->fill, 0, 56 - ctx->fill);
    store_be64(ctx->block + 56, bits);
    sha256_compress(ctx->state, ctx->block);

    for (i = 0; i < 8; ++i)
        store_be32(digest + 4 * i, ctx->state[i]);

    ST_crypto_wipe(ctx, sizeof *ctx);
}

void
ST_sha256(const void *data, size_t count,
          unsigned char digest[ST_SHA256_DIGEST_BYTES])
{
    st_sha256   ctx;

    ST_sha256_init(&ctx);
    ST_sha256_update(&ctx, data, count);
    ST_sha256_final(&ctx, digest);
}

/*  ----------  HMAC  ----------  */

/*
 *  The two states an HMAC under one key always begins from: the hash after
 *  absorbing the key XOR 0x36 (inner) and the key XOR 0x5c (outer).  A key
 *  longer than a block is hashed first, shorter is zero-padded: RFC 2104.
 */
static void
hmac_prepare(const void *key, size_t key_count,
             st_sha256 *inner, st_sha256 *outer)
{
    unsigned char   k[ST_SHA256_BLOCK_BYTES];
    unsigned char   pad[ST_SHA256_BLOCK_BYTES];
    size_t          i;

    memset(k, 0, sizeof k);
    if (key_count > ST_SHA256_BLOCK_BYTES)
        ST_sha256(key, key_count, k);
    else
        memcpy(k, key, key_count);

    for (i = 0; i < ST_SHA256_BLOCK_BYTES; ++i)
        pad[i] = k[i] ^ 0x36;
    ST_sha256_init(inner);
    ST_sha256_update(inner, pad, sizeof pad);

    for (i = 0; i < ST_SHA256_BLOCK_BYTES; ++i)
        pad[i] = k[i] ^ 0x5c;
    ST_sha256_init(outer);
    ST_sha256_update(outer, pad, sizeof pad);

    ST_crypto_wipe(k, sizeof k);
    ST_crypto_wipe(pad, sizeof pad);
}

/*  HMAC of one message from prepared states; the states are not consumed.  */
static void
hmac_from_prepared(const st_sha256 *inner, const st_sha256 *outer,
                   const void *message, size_t message_count,
                   unsigned char mac[ST_SHA256_DIGEST_BYTES])
{
    st_sha256       ctx;
    unsigned char   inner_digest[ST_SHA256_DIGEST_BYTES];

    ctx = *inner;
    ST_sha256_update(&ctx, message, message_count);
    ST_sha256_final(&ctx, inner_digest);

    ctx = *outer;
    ST_sha256_update(&ctx, inner_digest, sizeof inner_digest);
    ST_sha256_final(&ctx, mac);

    ST_crypto_wipe(inner_digest, sizeof inner_digest);
}

void
ST_hmac_sha256(const void *key, size_t key_count,
               const void *message, size_t message_count,
               unsigned char mac[ST_SHA256_DIGEST_BYTES])
{
    st_sha256   inner;
    st_sha256   outer;

    hmac_prepare(key, key_count, &inner, &outer);
    hmac_from_prepared(&inner, &outer, message, message_count, mac);
    ST_crypto_wipe(&inner, sizeof inner);
    ST_crypto_wipe(&outer, sizeof outer);
}

/*  ----------  PBKDF2  ----------  */

int
ST_pbkdf2_hmac_sha256(const void *password, size_t password_count,
                      const void *salt, size_t salt_count,
                      uint32_t iterations,
                      unsigned char *out, size_t out_count)
{
    st_sha256       inner;
    st_sha256       outer;
    st_sha256       ctx;
    unsigned char   u[ST_SHA256_DIGEST_BYTES];
    unsigned char   t[ST_SHA256_DIGEST_BYTES];
    unsigned char   index_be[4];
    uint32_t        block_index = 1;
    size_t          produced = 0;
    uint32_t        i;
    size_t          j;

    if (iterations == 0 || out_count == 0)
        return -1;

    hmac_prepare(password, password_count, &inner, &outer);

    /*
     *  RFC 8018, 5.2: T_i = U_1 xor U_2 xor ... xor U_c, where
     *  U_1 = PRF(P, S || INT(i)) and U_j = PRF(P, U_{j-1}); the key is
     *  T_1 || T_2 || ... as many as out_count needs.
     */
    while (produced < out_count) {
        size_t  take;

        store_be32(index_be, block_index);
        ctx = inner;
        ST_sha256_update(&ctx, salt, salt_count);
        ST_sha256_update(&ctx, index_be, sizeof index_be);
        ST_sha256_final(&ctx, u);
        ctx = outer;
        ST_sha256_update(&ctx, u, sizeof u);
        ST_sha256_final(&ctx, u);
        memcpy(t, u, sizeof t);

        for (i = 1; i < iterations; ++i) {
            hmac_from_prepared(&inner, &outer, u, sizeof u, u);
            for (j = 0; j < sizeof t; ++j)
                t[j] ^= u[j];
        }

        take = out_count - produced;
        if (take > sizeof t)
            take = sizeof t;
        memcpy(out + produced, t, take);
        produced += take;
        ++block_index;
    }

    ST_crypto_wipe(&inner, sizeof inner);
    ST_crypto_wipe(&outer, sizeof outer);
    ST_crypto_wipe(&ctx, sizeof ctx);
    ST_crypto_wipe(u, sizeof u);
    ST_crypto_wipe(t, sizeof t);
    return 0;
}

/*  ----------  The rest  ----------  */

int
ST_constant_time_equal(const void *a, const void *b, size_t count)
{
    const unsigned char    *x = (const unsigned char *) a;
    const unsigned char    *y = (const unsigned char *) b;
    unsigned char           diff = 0;
    size_t                  i;

    for (i = 0; i < count; ++i)
        diff |= (unsigned char) (x[i] ^ y[i]);
    return diff == 0;
}

void
ST_crypto_wipe(void *buffer, size_t count)
{
    /*
     *  A memset of a buffer that is never read again is exactly what a
     *  compiler is entitled to remove.  Writing through a volatile pointer
     *  is the portable way to say the stores are wanted anyway.
     */
    volatile unsigned char *p = (volatile unsigned char *) buffer;

    while (count-- > 0)
        *p++ = 0;
}
