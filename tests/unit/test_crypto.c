/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256 against their published
 *  answers, with no image.
 *
 *  Every expected value here came from somewhere other than this code:
 *  FIPS 180-4's examples and NIST's SHA-256 test vectors for the hash,
 *  RFC 4231 for HMAC, and for PBKDF2 the values every implementation
 *  quotes ("password"/"salt" at 1, 2 and 4096 rounds, the 40-byte
 *  multi-block case) plus RFC 7914's 64-byte example.  Each was then
 *  recomputed with an independent implementation -- Python's hashlib,
 *  which is OpenSSL -- before it was written down, because a vector
 *  remembered wrongly is a test that passes for the wrong reason.  One
 *  of them was, and the recomputation caught it.
 *
 *  The last PBKDF2 check is the one that matters to the system: the hash
 *  Kiss's demo database stores for the user `kiss', made by Java's
 *  PBKDF2WithHmacSHA256, verified here byte for byte.  That is the proof
 *  that a database Kiss made logs in on this server unchanged.  It costs
 *  a fifth of a second, which is what six hundred thousand rounds cost,
 *  and is the reason the arithmetic is in C.
 *
 *  Padding boundaries are exercised on purpose: 55, 56 and 64 bytes are
 *  the three cases FIPS 180-4's padding rule has (the length fits after
 *  the 1 bit; it does not; the data is exactly a block), and a million
 *  bytes fed seven at a time is the buffered path with every phase of
 *  fill.
 */

#include "st_test.h"
#include "st_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Whether n bytes read as this lowercase hex string.  */
static int
bytes_are(const unsigned char *bytes, size_t n, const char *hex)
{
    static const char   digits[] = "0123456789abcdef";
    size_t              i;

    if (strlen(hex) != 2 * n)
        return 0;
    for (i = 0; i < n; ++i)
        if (hex[2 * i] != digits[bytes[i] >> 4]
         || hex[2 * i + 1] != digits[bytes[i] & 15])
            return 0;
    return 1;
}

/*  Two hex digits to a byte, for the salt Kiss stored.  */
static void
bytes_from_hex(const char *hex, unsigned char *out, size_t n)
{
    size_t  i;

    for (i = 0; i < n; ++i) {
        unsigned    v;

        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (unsigned char) v;
    }
}

static void
check_sha256(const char *label, const void *data, size_t n, const char *hex)
{
    unsigned char   digest[32];

    ST_sha256(data, n, digest);
    if (!bytes_are(digest, sizeof digest, hex))
        printf("  sha256 %s\n", label);
    CHECK(bytes_are(digest, sizeof digest, hex));
}

static void
test_sha256(void)
{
    static const char   s448[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    unsigned char      *a;
    unsigned char       digest[32];
    st_sha256           ctx;
    size_t              i;

    check_sha256("empty", "", 0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha256("abc", "abc", 3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_sha256("448 bits", s448, strlen(s448),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    a = (unsigned char *) malloc(1000000);
    CHECK(a != NULL);
    if (a == NULL)
        return;
    memset(a, 'a', 1000000);

    /*  The three padding cases.  */
    check_sha256("55 a", a, 55,
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    check_sha256("56 a", a, 56,
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    check_sha256("64 a", a, 64,
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");

    /*  A million, in one piece and then seven bytes at a time.  */
    check_sha256("million a", a, 1000000,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    ST_sha256_init(&ctx);
    for (i = 0; i < 1000000; i += 7)
        ST_sha256_update(&ctx, a + i, 1000000 - i < 7 ? 1000000 - i : 7);
    ST_sha256_final(&ctx, digest);
    CHECK(bytes_are(digest, sizeof digest,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    /*  Split anywhere, same answer: across the block boundary here.  */
    ST_sha256_init(&ctx);
    ST_sha256_update(&ctx, s448, 30);
    ST_sha256_update(&ctx, s448 + 30, strlen(s448) - 30);
    ST_sha256_final(&ctx, digest);
    CHECK(bytes_are(digest, sizeof digest,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    free(a);
}

static void
check_hmac(const char *label, const void *key, size_t kn,
           const void *msg, size_t mn, const char *hex)
{
    unsigned char   mac[32];

    ST_hmac_sha256(key, kn, msg, mn, mac);
    if (!bytes_are(mac, sizeof mac, hex))
        printf("  hmac %s\n", label);
    CHECK(bytes_are(mac, sizeof mac, hex));
}

static void
test_hmac(void)
{
    unsigned char   key[131];

    /*  RFC 4231, test cases 1, 2, 3 and 6.  */
    memset(key, 0x0b, 20);
    check_hmac("case 1", key, 20, "Hi There", 8,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    check_hmac("case 2", "Jefe", 4, "what do ya want for nothing?", 28,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    {
        unsigned char   data[50];

        memset(key, 0xaa, 20);
        memset(data, 0xdd, sizeof data);
        check_hmac("case 3", key, 20, data, sizeof data,
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    }
    /*  A key longer than a block is hashed first: case 6.  */
    memset(key, 0xaa, sizeof key);
    check_hmac("case 6", key, sizeof key,
        "Test Using Larger Than Block-Size Key - Hash Key First", 54,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

static void
check_pbkdf2(const char *label, const char *pw, const char *salt,
             size_t salt_n, uint32_t rounds, size_t n, const char *hex)
{
    unsigned char   key[64];

    CHECK(n <= sizeof key);
    CHECK_EQ_INT(ST_pbkdf2_hmac_sha256(pw, strlen(pw), salt, salt_n,
                                       rounds, key, n), 0);
    if (!bytes_are(key, n, hex))
        printf("  pbkdf2 %s\n", label);
    CHECK(bytes_are(key, n, hex));
}

static void
test_pbkdf2(void)
{
    unsigned char   key[32];
    unsigned char   salt[16];

    check_pbkdf2("1 round", "password", "salt", 4, 1, 32,
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    check_pbkdf2("2 rounds", "password", "salt", 4, 2, 32,
        "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    check_pbkdf2("4096 rounds", "password", "salt", 4, 4096, 32,
        "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    /*  Two blocks of output, the second cut short.  */
    check_pbkdf2("40 bytes", "passwordPASSWORDpassword",
        "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096, 40,
        "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1"
        "c635518c7dac47e9");
    /*  RFC 7914, section 11: 64 bytes at one round.  */
    check_pbkdf2("rfc 7914", "passwd", "salt", 4, 1, 64,
        "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
        "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");

    /*
     *  The one Kiss made.  schema.sqlite in the Kiss tree inserts user
     *  `kiss' with
     *
     *      pbkdf2$600000$XXxhRHyyeLvk3AfmOhTYhA$ZX/GcXFZJaj94VBbxu3zTVTwdVy7CxfxXfK/irpetUI
     *
     *  for the password `password'; the salt and key below are those two
     *  Base64 fields decoded.  Java made it; this must agree with Java.
     */
    bytes_from_hex("5d7c61447cb278bbe4dc07e63a14d884", salt, sizeof salt);
    CHECK_EQ_INT(ST_pbkdf2_hmac_sha256("password", 8, salt, sizeof salt,
                                       600000, key, sizeof key), 0);
    CHECK(bytes_are(key, sizeof key,
        "657fc671715925a8fde1505bc6edf34d54f0755cbb0b17f15df2bf8aba5eb542"));

    /*  The two arguments nobody means.  */
    CHECK_EQ_INT(ST_pbkdf2_hmac_sha256("x", 1, "y", 1, 0, key, 32), -1);
    CHECK_EQ_INT(ST_pbkdf2_hmac_sha256("x", 1, "y", 1, 1, key, 0), -1);
}

static void
test_equal(void)
{
    unsigned char   a[32];
    unsigned char   b[32];

    memset(a, 0x5a, sizeof a);
    memcpy(b, a, sizeof b);
    CHECK(ST_constant_time_equal(a, b, sizeof a));
    b[31] ^= 1;
    CHECK(!ST_constant_time_equal(a, b, sizeof a));
    b[31] ^= 1;
    b[0] ^= 0x80;
    CHECK(!ST_constant_time_equal(a, b, sizeof a));
    CHECK(ST_constant_time_equal(a, b, 0));

    ST_crypto_wipe(a, sizeof a);
    CHECK(a[0] == 0 && a[31] == 0);
}

int
main(void)
{
    ST_TEST_BEGIN("crypto: sha-256, hmac, pbkdf2");
    test_sha256();
    test_hmac();
    test_pbkdf2();
    test_equal();
    return ST_TEST_END();
}
