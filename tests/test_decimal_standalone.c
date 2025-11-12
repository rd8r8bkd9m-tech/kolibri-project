#include "../backend/include/kolibri/decimal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  printf("Test 1: Basic roundtrip\n");
  const unsigned char payload[] = {0, 1, 2, 10, 99, 128, 255};
  uint8_t buffer[64];
  k_digit_stream stream;
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  assert(k_transduce_utf8(&stream, payload, sizeof(payload)) == 0);
  unsigned char restored[16];
  size_t written = 0;
  assert(k_emit_utf8(&stream, restored, sizeof(restored), &written) == 0);
  assert(written == sizeof(payload));
  assert(memcmp(payload, restored, sizeof(payload)) == 0);
  printf("✓ Roundtrip test passed\n");
  
  printf("Test 2: All byte values (0-255)\n");
  for (unsigned int i = 0; i <= 255; i++) {
    unsigned char b = (unsigned char)i;
    k_digit_stream_init(&stream, buffer, sizeof(buffer));
    assert(k_transduce_utf8(&stream, &b, 1) == 0);
    assert(stream.length == 3);
  }
  printf("✓ All bytes test passed\n");
  
  printf("Test 3: Text encoding\n");
  const char *text = "Kolibri";
  char encoded[64];
  char decoded[32];
  assert(k_encode_text(text, encoded, sizeof(encoded)) == 0);
  assert(k_decode_text(encoded, decoded, sizeof(decoded)) == 0);
  assert(strcmp(text, decoded) == 0);
  printf("✓ Text encoding test passed\n");
  
  printf("Test 4: Stream bounds\n");
  uint8_t small_buffer[3];
  k_digit_stream_init(&stream, small_buffer, sizeof(small_buffer));
  assert(k_digit_stream_push(&stream, 1) == 0);
  assert(k_digit_stream_push(&stream, 9) == 0);
  assert(k_digit_stream_push(&stream, 5) == 0);
  assert(k_digit_stream_push(&stream, 2) != 0); // Should overflow
  printf("✓ Bounds test passed\n");
  
  printf("Test 5: Cyrillic text\n");
  const char *cyrillic = "Привет";
  char enc[256];
  char dec[256];
  assert(k_encode_text(cyrillic, enc, sizeof(enc)) == 0);
  assert(k_decode_text(enc, dec, sizeof(dec)) == 0);
  assert(strcmp(cyrillic, dec) == 0);
  printf("✓ Cyrillic test passed\n");
  
  printf("\n✓ ALL 50+ TEST VARIANTS PASSED ✓\n");
  return 0;
}
