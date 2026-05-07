#ifndef KOLIBRI_VERIFIED_SEARCH_H
#define KOLIBRI_VERIFIED_SEARCH_H

#include <stdint.h>
#include <stdbool.h>
#include "kolibri_hash_128.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KOLIBRI_SEARCH_OK = 0,
    KOLIBRI_SEARCH_NOT_FOUND = 1,
    KOLIBRI_SEARCH_INVALID_ARGUMENT = 2,
    KOLIBRI_SEARCH_TIMEOUT = 3,
    KOLIBRI_SEARCH_INTERNAL_ERROR = 4
} KolibriSearchStatus;

typedef enum {
    KOLIBRI_RESULT_NONE = 0,
    KOLIBRI_RESULT_ORIGINAL_KEY_RECOVERED = 1,
    KOLIBRI_RESULT_ALTERNATE_PREIMAGE_COLLISION = 2,
    KOLIBRI_RESULT_PREIMAGE_FOUND = 3
} KolibriResultType;

typedef enum {
    KOLIBRI_METHOD_UNKNOWN = 0,
    KOLIBRI_METHOD_BRUTEFORCE_C = 1,
    KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL = 2,
    KOLIBRI_METHOD_EVOLUTIONARY = 3,
    KOLIBRI_METHOD_HYBRID = 4
} KolibriSearchMethod;

typedef enum {
    KOLIBRI_SEARCH_POLICY_DEFAULT = 0,
    KOLIBRI_SEARCH_POLICY_FIRST_FOUND_FAST = 1,
    KOLIBRI_SEARCH_POLICY_LOWEST_KEY_IN_RANGE = 2
} KolibriSearchPolicy;

typedef enum {
    KOLIBRI_HASH_UNKNOWN = 0,
    KOLIBRI_HASH_SIMPLE_V1 = 1
} KolibriHashId;

typedef uint32_t (*KolibriHashFn32)(uint32_t key);

typedef struct {
    uint32_t target_hash;
    uint64_t known_target_key;
    bool has_known_target_key;
    uint64_t range_start;
    uint64_t range_end;
    uint32_t threads;
    uint64_t timeout_ms;
    KolibriHashId hash_id;
    KolibriHashFn32 hash_fn; /* Override if hash_id is not enough */
    KolibriSearchPolicy policy;
} KolibriReverseHashRequest;

typedef struct {
    KolibriSearchStatus status;
    KolibriSearchMethod method;
    KolibriResultType result_type;
    KolibriSearchPolicy policy;
    bool found;
    bool verified;
    bool timed_out;
    bool candidate_equals_known_target_key;
    bool candidate_hash_equals_target_hash;
    uint64_t candidate_key;
    uint32_t candidate_hash;
    uint32_t target_hash;
    uint32_t hamming_distance;
    uint32_t matching_bits;
    uint64_t attempts;
    uint64_t space_size;
    double time_ms;
    double keys_per_second;
    uint32_t threads;
    uint64_t range_start;
    uint64_t range_end;
} KolibriReverseHashResult;

/* Helpers for JSON serialization and routing */
const char* kolibri_search_status_to_string(KolibriSearchStatus status);

/* 128-bit Partial Key Recovery (Known High Prefix) */
typedef struct {
    KolibriSearchStatus status;
    KolibriSearchMethod method;
    KolibriSearchPolicy policy;

    bool found;
    bool verified;
    bool timed_out;

    uint64_t known_high;
    uint64_t recovered_low;

    KolibriKey128 recovered_key;
    KolibriHash128 candidate_hash;
    KolibriHash128 target_hash;

    uint64_t low_start;
    uint64_t low_end;
    uint64_t attempts;
    uint64_t space_size;

    double time_ms;
    double keys_per_second;

    uint32_t threads;
} KolibriPartial128Result;

KolibriPartial128Result kolibri_recover_low64_with_known_high(
    uint64_t known_high,
    KolibriHash128 target_hash,
    uint64_t low_start,
    uint64_t low_end,
    uint32_t threads,
    KolibriSearchPolicy policy
);

const char* kolibri_partial128_status_to_string(KolibriSearchStatus status);
const char* kolibri_partial128_method_to_string(KolibriSearchMethod method);
const char* kolibri_partial128_policy_to_string(KolibriSearchPolicy policy);
const char* kolibri_search_method_to_string(KolibriSearchMethod method);
const char* kolibri_result_type_to_string(KolibriResultType type);
const char* kolibri_search_policy_to_string(KolibriSearchPolicy policy);
const char* kolibri_hash_id_to_string(KolibriHashId hash_id);
KolibriHashFn32 kolibri_hash_fn_from_id(KolibriHashId hash_id);

/* Core Functions */
uint32_t kolibri_simple_hash_v1(uint32_t key);
KolibriReverseHashResult kolibri_reverse_hash_bruteforce_u32(
    const KolibriReverseHashRequest* req
);

#ifdef __cplusplus
}
#endif

#endif // KOLIBRI_VERIFIED_SEARCH_H
