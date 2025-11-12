/*
 * SHA256 implementation from RFC 6234
 * Adapted for standalone use
 */

#include <stdint.h>
#include <string.h>

/* Define the SHA shift, rotate left, and rotate right macros */
#define SHA256_SHR(bits,word)      ((word) >> (bits))
#define SHA256_ROTL(bits,word)                         \
  (((word) << (bits)) | ((word) >> (32-(bits))))
#define SHA256_ROTR(bits,word)                         \
  (((word) >> (bits)) | ((word) << (32-(bits))))

/* Define the SHA SIGMA and sigma macros */
#define SHA256_SIGMA0(word)   \
  (SHA256_ROTR( 2,word) ^ SHA256_ROTR(13,word) ^ SHA256_ROTR(22,word))
#define SHA256_SIGMA1(word)   \
  (SHA256_ROTR( 6,word) ^ SHA256_ROTR(11,word) ^ SHA256_ROTR(25,word))
#define SHA256_sigma0(word)   \
  (SHA256_ROTR( 7,word) ^ SHA256_ROTR(18,word) ^ SHA256_SHR( 3,word))
#define SHA256_sigma1(word)   \
  (SHA256_ROTR(17,word) ^ SHA256_ROTR(19,word) ^ SHA256_SHR(10,word))

/* SHA-256 initial hash values */
static const uint32_t SHA256_H0[8] = {
  0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
  0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

/* SHA-256 round constants */
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

/* SHA-256 context */
typedef struct {
    uint32_t Intermediate_Hash[8];
    uint32_t Length_High;
    uint32_t Length_Low;
    int_least16_t Message_Block_Index;
    uint8_t Message_Block[64];
    int Computed;
    int Corrupted;
} SHA256Context;

/* Local function prototypes */
static void SHA256ProcessMessageBlock(SHA256Context *context);
static void SHA256Finalize(SHA256Context *context, uint8_t Pad_Byte);
static void SHA256PadMessage(SHA256Context *context, uint8_t Pad_Byte);

/* Initialize SHA256 context */
static int SHA256Reset(SHA256Context *context) {
  if (!context) return 1;

  context->Length_High = context->Length_Low = 0;
  context->Message_Block_Index = 0;

  context->Intermediate_Hash[0] = SHA256_H0[0];
  context->Intermediate_Hash[1] = SHA256_H0[1];
  context->Intermediate_Hash[2] = SHA256_H0[2];
  context->Intermediate_Hash[3] = SHA256_H0[3];
  context->Intermediate_Hash[4] = SHA256_H0[4];
  context->Intermediate_Hash[5] = SHA256_H0[5];
  context->Intermediate_Hash[6] = SHA256_H0[6];
  context->Intermediate_Hash[7] = SHA256_H0[7];

  context->Computed = 0;
  context->Corrupted = 0;

  return 0;
}

/* Add length to the length */
static uint32_t addTemp;
#define SHA256AddLength(context, length)               \
  (addTemp = (context)->Length_Low,                    \
   (context)->Corrupted =                              \
    (((context)->Length_Low += (length)) < addTemp) && \
    (++(context)->Length_High == 0) ? 1 :             \
    (context)->Corrupted)

/* Input data */
static int SHA256Input(SHA256Context *context, const uint8_t *message_array, unsigned int length) {
  if (!context) return 1;
  if (!length) return 0;
  if (!message_array) return 1;
  if (context->Computed) return context->Corrupted = 1;
  if (context->Corrupted) return context->Corrupted;

  while (length--) {
    context->Message_Block[context->Message_Block_Index++] = *message_array;

    if ((SHA256AddLength(context, 8) == 0) &&
        (context->Message_Block_Index == 64))
      SHA256ProcessMessageBlock(context);

    message_array++;
  }

  return context->Corrupted;
}

/* Finalize and get result */
static int SHA256Result(SHA256Context *context, uint8_t Message_Digest[32]) {
  int i;

  if (!context) return 1;
  if (!Message_Digest) return 1;
  if (context->Corrupted) return context->Corrupted;

  if (!context->Computed)
    SHA256Finalize(context, 0x80);

  for (i = 0; i < 32; ++i)
    Message_Digest[i] = (uint8_t)
      (context->Intermediate_Hash[i>>2] >> 8 * (3 - (i & 0x03)));

  return 0;
}

/* Process message block */
static void SHA256ProcessMessageBlock(SHA256Context *context) {
  int t;
  uint32_t temp1, temp2;
  uint32_t W[64];
  uint32_t A, B, C, D, E, F, G, H;

  /* Initialize the first 16 words in the array W */
  for (t = 0; t < 16; t++) {
    W[t] = ((uint32_t)context->Message_Block[t * 4]) << 24;
    W[t] |= ((uint32_t)context->Message_Block[t * 4 + 1]) << 16;
    W[t] |= ((uint32_t)context->Message_Block[t * 4 + 2]) << 8;
    W[t] |= ((uint32_t)context->Message_Block[t * 4 + 3]);
  }

  for (t = 16; t < 64; t++)
    W[t] = SHA256_sigma1(W[t-2]) + W[t-7] +
           SHA256_sigma0(W[t-15]) + W[t-16];

  A = context->Intermediate_Hash[0];
  B = context->Intermediate_Hash[1];
  C = context->Intermediate_Hash[2];
  D = context->Intermediate_Hash[3];
  E = context->Intermediate_Hash[4];
  F = context->Intermediate_Hash[5];
  G = context->Intermediate_Hash[6];
  H = context->Intermediate_Hash[7];

  for (t = 0; t < 64; t++) {
    temp1 = H + SHA256_SIGMA1(E) + ((E & F) ^ ((~E) & G)) + K[t] + W[t];
    temp2 = SHA256_SIGMA0(A) + ((A & B) ^ (A & C) ^ (B & C));
    H = G;
    G = F;
    F = E;
    E = D + temp1;
    D = C;
    C = B;
    B = A;
    A = temp1 + temp2;
  }

  context->Intermediate_Hash[0] += A;
  context->Intermediate_Hash[1] += B;
  context->Intermediate_Hash[2] += C;
  context->Intermediate_Hash[3] += D;
  context->Intermediate_Hash[4] += E;
  context->Intermediate_Hash[5] += F;
  context->Intermediate_Hash[6] += G;
  context->Intermediate_Hash[7] += H;

  context->Message_Block_Index = 0;
}

/* Finalize */
static void SHA256Finalize(SHA256Context *context, uint8_t Pad_Byte) {
  int i;
  SHA256PadMessage(context, Pad_Byte);
  /* message may be sensitive, so clear it out */
  for (i = 0; i < 64; ++i)
    context->Message_Block[i] = 0;
  context->Length_High = 0; /* and clear length */
  context->Length_Low = 0;
  context->Computed = 1;
}

/* Pad message */
static void SHA256PadMessage(SHA256Context *context, uint8_t Pad_Byte) {
  if (context->Message_Block_Index >= 56) {
    context->Message_Block[context->Message_Block_Index++] = Pad_Byte;
    while (context->Message_Block_Index < 64)
      context->Message_Block[context->Message_Block_Index++] = 0;
    SHA256ProcessMessageBlock(context);
    while (context->Message_Block_Index < 56)
      context->Message_Block[context->Message_Block_Index++] = 0;
  } else {
    context->Message_Block[context->Message_Block_Index++] = Pad_Byte;
    while (context->Message_Block_Index < 56)
      context->Message_Block[context->Message_Block_Index++] = 0;
  }

  /* Store the message length as the last 8 octets */
  context->Message_Block[56] = (uint8_t)(context->Length_High >> 24);
  context->Message_Block[57] = (uint8_t)(context->Length_High >> 16);
  context->Message_Block[58] = (uint8_t)(context->Length_High >> 8);
  context->Message_Block[59] = (uint8_t)(context->Length_High);
  context->Message_Block[60] = (uint8_t)(context->Length_Low >> 24);
  context->Message_Block[61] = (uint8_t)(context->Length_Low >> 16);
  context->Message_Block[62] = (uint8_t)(context->Length_Low >> 8);
  context->Message_Block[63] = (uint8_t)(context->Length_Low);
  SHA256ProcessMessageBlock(context);
}

/* Public interface */
void kolibri_sha256(const unsigned char *data, size_t len, unsigned char hash[32]) {
  SHA256Context ctx;
  SHA256Reset(&ctx);
  SHA256Input(&ctx, data, len);
  SHA256Result(&ctx, hash);
}