#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef unsigned int MD5_LONG;

typedef struct MD5state_st {
    MD5_LONG A, B, C, D;
    MD5_LONG Nl, Nh;
    MD5_LONG data[16];
    unsigned int num;
} MD5_CTX;

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE(a, n) (((a) << (n)) | ((a) >> (32 - (n))))

#define STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = ROTATE((a), (s)); \
    (a) += (b);

static void md5_transform(MD5_CTX *c, const unsigned char block[64]) {
    MD5_LONG a = c->A, b = c->B, cc = c->C, d = c->D, x[16];

    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        x[i] = (MD5_LONG)block[j] | ((MD5_LONG)block[j + 1] << 8) |
               ((MD5_LONG)block[j + 2] << 16) | ((MD5_LONG)block[j + 3] << 24);
    }

    STEP(F, a, b, cc, d, x[0], 0xd76aa478, 7)
    STEP(F, d, a, b, cc, x[1], 0xe8c7b756, 12)
    STEP(F, cc, d, a, b, x[2], 0x242070db, 17)
    STEP(F, b, cc, d, a, x[3], 0xc1bdceee, 22)
    STEP(F, a, b, cc, d, x[4], 0xf57c0faf, 7)
    STEP(F, d, a, b, cc, x[5], 0x4787c62a, 12)
    STEP(F, cc, d, a, b, x[6], 0xa8304613, 17)
    STEP(F, b, cc, d, a, x[7], 0xfd469501, 22)
    STEP(F, a, b, cc, d, x[8], 0x698098d8, 7)
    STEP(F, d, a, b, cc, x[9], 0x8b44f7af, 12)
    STEP(F, cc, d, a, b, x[10], 0xffff5bb1, 17)
    STEP(F, b, cc, d, a, x[11], 0x895cd7be, 22)
    STEP(F, a, b, cc, d, x[12], 0x6b901122, 7)
    STEP(F, d, a, b, cc, x[13], 0xfd987193, 12)
    STEP(F, cc, d, a, b, x[14], 0xa679438e, 17)
    STEP(F, b, cc, d, a, x[15], 0x49b40821, 22)

    STEP(G, a, b, cc, d, x[1], 0xf61e2562, 5)
    STEP(G, d, a, b, cc, x[6], 0xc040b340, 9)
    STEP(G, cc, d, a, b, x[11], 0x265e5a51, 14)
    STEP(G, b, cc, d, a, x[0], 0xe9b6c7aa, 20)
    STEP(G, a, b, cc, d, x[5], 0xd62f105d, 5)
    STEP(G, d, a, b, cc, x[10], 0x02441453, 9)
    STEP(G, cc, d, a, b, x[15], 0xd8a1e681, 14)
    STEP(G, b, cc, d, a, x[4], 0xe7d3fbc8, 20)
    STEP(G, a, b, cc, d, x[9], 0x21e1cde6, 5)
    STEP(G, d, a, b, cc, x[14], 0xc33707d6, 9)
    STEP(G, cc, d, a, b, x[3], 0xf4d50d87, 14)
    STEP(G, b, cc, d, a, x[8], 0x455a14ed, 20)
    STEP(G, a, b, cc, d, x[13], 0xa9e3e905, 5)
    STEP(G, d, a, b, cc, x[2], 0xfcefa3f8, 9)
    STEP(G, cc, d, a, b, x[7], 0x676f02d9, 14)
    STEP(G, b, cc, d, a, x[12], 0x8d2a4c8a, 20)

    STEP(H, a, b, cc, d, x[5], 0xfffa3942, 4)
    STEP(H, d, a, b, cc, x[8], 0x8771f681, 11)
    STEP(H, cc, d, a, b, x[11], 0x6d9d6122, 16)
    STEP(H, b, cc, d, a, x[14], 0xfde5380c, 23)
    STEP(H, a, b, cc, d, x[1], 0xa4beea44, 4)
    STEP(H, d, a, b, cc, x[4], 0x4bdecfa9, 11)
    STEP(H, cc, d, a, b, x[7], 0xf6bb4b60, 16)
    STEP(H, b, cc, d, a, x[10], 0xbebfbc70, 23)
    STEP(H, a, b, cc, d, x[13], 0x289b7ec6, 4)
    STEP(H, d, a, b, cc, x[0], 0xeaa127fa, 11)
    STEP(H, cc, d, a, b, x[3], 0xd4ef3085, 16)
    STEP(H, b, cc, d, a, x[6], 0x04881d05, 23)
    STEP(H, a, b, cc, d, x[9], 0xd9d4d039, 4)
    STEP(H, d, a, b, cc, x[12], 0xe6db99e5, 11)
    STEP(H, cc, d, a, b, x[15], 0x1fa27cf8, 16)
    STEP(H, b, cc, d, a, x[2], 0xc4ac5665, 23)

    STEP(I, a, b, cc, d, x[0], 0xf4292244, 6)
    STEP(I, d, a, b, cc, x[7], 0x432aff97, 10)
    STEP(I, cc, d, a, b, x[14], 0xab9423a7, 15)
    STEP(I, b, cc, d, a, x[5], 0xfc93a039, 21)
    STEP(I, a, b, cc, d, x[12], 0x655b59c3, 6)
    STEP(I, d, a, b, cc, x[3], 0x8f0ccc92, 10)
    STEP(I, cc, d, a, b, x[10], 0xffeff47d, 15)
    STEP(I, b, cc, d, a, x[1], 0x85845dd1, 21)
    STEP(I, a, b, cc, d, x[8], 0x6fa87e4f, 6)
    STEP(I, d, a, b, cc, x[15], 0xfe2ce6e0, 10)
    STEP(I, cc, d, a, b, x[6], 0xa3014314, 15)
    STEP(I, b, cc, d, a, x[13], 0x4e0811a1, 21)
    STEP(I, a, b, cc, d, x[4], 0xf7537e82, 6)
    STEP(I, d, a, b, cc, x[11], 0xbd3af235, 10)
    STEP(I, cc, d, a, b, x[2], 0x2ad7d2bb, 15)
    STEP(I, b, cc, d, a, x[9], 0xeb86d391, 21)

    c->A += a;
    c->B += b;
    c->C += cc;
    c->D += d;
}

int MD5_Init(MD5_CTX *c) {
    if (!c) return 0;
    c->A = 0x67452301;
    c->B = 0xefcdab89;
    c->C = 0x98badcfe;
    c->D = 0x10325476;
    c->Nl = 0;
    c->Nh = 0;
    memset(c->data, 0, sizeof(c->data));
    c->num = 0;
    return 1;
}

int MD5_Update(MD5_CTX *c, const void *data, size_t len) {
    const unsigned char *input = (const unsigned char *)data;
    if (!c || (!input && len > 0)) return 0;

    uint64_t bitlen = ((uint64_t)c->Nh << 32) | c->Nl;
    bitlen += (uint64_t)len * 8;
    c->Nl = (MD5_LONG)(bitlen & 0xffffffffu);
    c->Nh = (MD5_LONG)(bitlen >> 32);

    while (len > 0) {
        size_t take = 64 - c->num;
        if (take > len) take = len;
        memcpy(((unsigned char *)c->data) + c->num, input, take);
        c->num += (unsigned int)take;
        input += take;
        len -= take;

        if (c->num == 64) {
            md5_transform(c, (const unsigned char *)c->data);
            c->num = 0;
        }
    }

    return 1;
}

int MD5_Final(unsigned char *md, MD5_CTX *c) {
    if (!md || !c) return 0;

    unsigned char pad[64] = {0x80};
    unsigned char lenbuf[8];
    uint64_t bits = ((uint64_t)c->Nh << 32) | c->Nl;

    for (int i = 0; i < 8; i++) {
        lenbuf[i] = (unsigned char)((bits >> (8 * i)) & 0xff);
    }

    size_t padlen = (c->num < 56) ? (56 - c->num) : (120 - c->num);
    MD5_Update(c, pad, padlen);
    MD5_Update(c, lenbuf, 8);

    MD5_LONG out[4] = {c->A, c->B, c->C, c->D};
    for (int i = 0; i < 4; i++) {
        md[i * 4 + 0] = (unsigned char)(out[i] & 0xff);
        md[i * 4 + 1] = (unsigned char)((out[i] >> 8) & 0xff);
        md[i * 4 + 2] = (unsigned char)((out[i] >> 16) & 0xff);
        md[i * 4 + 3] = (unsigned char)((out[i] >> 24) & 0xff);
    }

    return 1;
}
