#include "kolibri_verified_search.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* --- Helpers for JSON/Routing --- */

const char* kolibri_search_status_to_string(KolibriSearchStatus status) {
    switch (status) {
        case KOLIBRI_SEARCH_OK: return "solved";
        case KOLIBRI_SEARCH_NOT_FOUND: return "not_found";
        case KOLIBRI_SEARCH_INVALID_ARGUMENT: return "invalid_argument";
        case KOLIBRI_SEARCH_TIMEOUT: return "timeout";
        default: return "internal_error";
    }
}

const char* kolibri_search_method_to_string(KolibriSearchMethod method) {
    switch (method) {
        case KOLIBRI_METHOD_BRUTEFORCE_C: return "bruteforce_c";
        case KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL: return "bruteforce_c_parallel";
        case KOLIBRI_METHOD_EVOLUTIONARY: return "evolutionary";
        case KOLIBRI_METHOD_HYBRID: return "hybrid";
        default: return "unknown";
    }
}

const char* kolibri_result_type_to_string(KolibriResultType type) {
    switch (type) {
        case KOLIBRI_RESULT_ORIGINAL_KEY_RECOVERED: return "original_key_recovered";
        case KOLIBRI_RESULT_ALTERNATE_PREIMAGE_COLLISION: return "alternate_preimage_collision";
        case KOLIBRI_RESULT_PREIMAGE_FOUND: return "preimage_found";
        default: return "none";
    }
}

const char* kolibri_search_policy_to_string(KolibriSearchPolicy policy) {
    switch (policy) {
        case KOLIBRI_SEARCH_POLICY_FIRST_FOUND_FAST: return "first_found_fast";
        case KOLIBRI_SEARCH_POLICY_LOWEST_KEY_IN_RANGE: return "lowest_key_in_range";
        default: return "default";
    }
}

const char* kolibri_hash_id_to_string(KolibriHashId hash_id) {
    switch (hash_id) {
        case KOLIBRI_HASH_SIMPLE_V1: return "simple_hash_v1";
        default: return "unknown";
    }
}

/* --- Core Hash Implementation --- */

uint32_t kolibri_simple_hash_v1(uint32_t key) {
    uint32_t h = 0;
    uint32_t k = key;
    for (int i = 0; i < 8; i++) {
        h = (uint32_t)(((h ^ k) * 0x5BD1E995u) & 0xFFFFFFFFu);
        k = (uint32_t)((k >> 13) | (k << 19));
    }
    return h;
}

KolibriHashFn32 kolibri_hash_fn_from_id(KolibriHashId hash_id) {
    if (hash_id == KOLIBRI_HASH_SIMPLE_V1) {
        return kolibri_simple_hash_v1;
    }
    return NULL;
}

/* --- Internal Utils --- */

static uint32_t popcount32(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_popcount(x);
#else
    uint32_t c = 0;
    while (x) {
        x &= x - 1;
        c++;
    }
    return c;
#endif
}

static double get_time_ms() {
#ifdef _OPENMP
    return omp_get_wtime() * 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
#endif
}

/* --- Main Search Logic --- */

KolibriReverseHashResult kolibri_reverse_hash_bruteforce_u32(
    const KolibriReverseHashRequest* req
) {
    KolibriReverseHashResult result = {0};
    
    // Validation
    if (!req) {
        result.status = KOLIBRI_SEARCH_INVALID_ARGUMENT;
        return result;
    }

    KolibriHashFn32 hash_fn = req->hash_fn ? req->hash_fn : kolibri_hash_fn_from_id(req->hash_id);
    if (!hash_fn) {
        result.status = KOLIBRI_SEARCH_INVALID_ARGUMENT;
        return result;
    }

    if (req->range_end < req->range_start) {
        result.status = KOLIBRI_SEARCH_INVALID_ARGUMENT;
        return result;
    }

    // Init Result Fields
    result.status = KOLIBRI_SEARCH_NOT_FOUND;
    result.method = KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL;
    result.target_hash = req->target_hash;
    result.range_start = req->range_start;
    result.range_end = req->range_end;
    result.space_size = ((uint64_t)req->range_end - (uint64_t)req->range_start) + 1ULL;
    result.policy = (req->policy == KOLIBRI_SEARCH_POLICY_DEFAULT) 
                    ? KOLIBRI_SEARCH_POLICY_FIRST_FOUND_FAST 
                    : req->policy;

    volatile bool found = false;
    uint32_t best_key = 0;
    uint64_t attempts = 0;
    double t0 = get_time_ms();

#ifdef _OPENMP
    if (req->threads > 0) {
        omp_set_num_threads((int)req->threads);
    }
    
    uint64_t start = (uint64_t)req->range_start;
    uint64_t end = (uint64_t)req->range_end;

    if (result.policy == KOLIBRI_SEARCH_POLICY_LOWEST_KEY_IN_RANGE) {
        // Deterministic Policy: Find absolute minimum key in range
        uint32_t global_min_key = UINT32_MAX;
        bool global_found = false;

        #pragma omp parallel
        {
            uint32_t local_min_key = UINT32_MAX;
            bool local_found = false;
            uint64_t local_attempts = 0;

            #pragma omp for schedule(static)
            for (uint64_t x = start; x <= end; x++) {
                uint32_t h = hash_fn((uint32_t)x);
                local_attempts++;
                
                if (h == req->target_hash) {
                    if (!local_found || x < local_min_key) {
                        local_min_key = x;
                        local_found = true;
                    }
                }
            }

            #pragma omp atomic
            attempts += local_attempts;

            #pragma omp critical
            {
                if (local_found) {
                    if (!global_found || local_min_key < global_min_key) {
                        global_min_key = local_min_key;
                        global_found = true;
                    }
                }
            }
        }

        if (global_found) {
            found = true;
            best_key = global_min_key;
        }
    } else {
        // Fast Policy: First found wins (Early Exit)
        #pragma omp parallel shared(found, best_key, attempts)
        {
            #pragma omp for schedule(dynamic, 10000) nowait
            for (uint64_t x = start; x <= end; x++) {
                if (found) continue;

                uint32_t h = hash_fn((uint32_t)x);
                
                #pragma omp atomic
                attempts++;

                if (h == req->target_hash) {
                    #pragma omp critical
                    {
                        if (!found) {
                            found = true;
                            best_key = x;
                        }
                    }
                }
            }
        }
    }
#else
    // Fallback for single-thread execution
    for (uint64_t x = start; x <= end; x++) {
        uint32_t key = (uint32_t)x;
        uint32_t h = hash_fn(key);
        attempts++;
        
        if (h == req->target_hash) {
            found = true;
            best_key = key;
            if (result.policy == KOLIBRI_SEARCH_POLICY_FIRST_FOUND_FAST) break;
            // For LOWEST_KEY we continue to find the smallest, but in single thread simple loop is already ordered
            if (result.policy == KOLIBRI_SEARCH_POLICY_LOWEST_KEY_IN_RANGE) break; 
        }
        if (x == UINT64_MAX) break;
    }
#endif

    double t1 = get_time_ms();
    result.time_ms = t1 - t0;
    result.attempts = attempts;
    result.keys_per_second = (result.time_ms > 0.0) ? ((double)attempts / (result.time_ms / 1000.0)) : 0.0;
    result.threads = (req->threads > 0) ? req->threads : 1; // Simplified thread reporting

    if (found) {
        uint32_t candidate_hash = hash_fn(best_key);
        result.status = KOLIBRI_SEARCH_OK;
        result.found = true;
        result.verified = (candidate_hash == req->target_hash);
        result.candidate_key = best_key;
        result.candidate_hash = candidate_hash;
        result.candidate_hash_equals_target_hash = result.verified;
        
        result.hamming_distance = popcount32(candidate_hash ^ req->target_hash);
        result.matching_bits = 32u - result.hamming_distance;

        // Classification
        if (req->has_known_target_key) {
            if (best_key == req->known_target_key) {
                result.result_type = KOLIBRI_RESULT_ORIGINAL_KEY_RECOVERED;
                result.candidate_equals_known_target_key = true;
            } else {
                result.result_type = KOLIBRI_RESULT_ALTERNATE_PREIMAGE_COLLISION;
                result.candidate_equals_known_target_key = false;
            }
        } else {
            result.result_type = KOLIBRI_RESULT_PREIMAGE_FOUND;
            result.candidate_equals_known_target_key = false;
        }
    } else {
        result.status = KOLIBRI_SEARCH_NOT_FOUND;
        result.found = false;
        result.verified = false;
        result.result_type = KOLIBRI_RESULT_NONE;
        result.candidate_key = 0;
        result.candidate_hash = 0;
        result.hamming_distance = 32;
        result.matching_bits = 0;
    }

    return result;
}

/* --- 128-bit Partial Key Recovery Helpers --- */

const char* kolibri_partial128_status_to_string(KolibriSearchStatus status) {
    return kolibri_search_status_to_string(status);
}

const char* kolibri_partial128_method_to_string(KolibriSearchMethod method) {
    switch (method) {
        case KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL: return "bruteforce_c_parallel";
        default: return "unknown";
    }
}

const char* kolibri_partial128_policy_to_string(KolibriSearchPolicy policy) {
    return kolibri_search_policy_to_string(policy);
}

/* --- Infeasible Guard Constants --- */
#define KOLIBRI_MAX_SEARCH_SPACE_BITS 40
#define KOLIBRI_MAX_SEARCH_SPACE_SIZE (1ULL << KOLIBRI_MAX_SEARCH_SPACE_BITS)

/* --- 128-bit Partial Key Recovery Implementation --- */

KolibriPartial128Result kolibri_recover_low64_with_known_high(
    uint64_t known_high,
    KolibriHash128 target_hash,
    uint64_t low_start,
    uint64_t low_end,
    uint32_t threads,
    KolibriSearchPolicy policy
) {
    KolibriPartial128Result result = {0};

    // Validation
    if (low_end < low_start) {
        result.status = KOLIBRI_SEARCH_INVALID_ARGUMENT;
        result.method = KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL;
        result.policy = policy;
        return result;
    }

    // Infeasible guard: reject searches with space > 2^40
    uint64_t space_size = ((uint64_t)low_end - (uint64_t)low_start) + 1ULL;
    if (space_size > KOLIBRI_MAX_SEARCH_SPACE_SIZE) {
        result.status = KOLIBRI_SEARCH_INVALID_ARGUMENT;
        result.method = KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL;
        result.policy = policy;
        result.space_size = space_size;
        return result;
    }

    // Init Result Fields
    result.status = KOLIBRI_SEARCH_NOT_FOUND;
    result.method = KOLIBRI_METHOD_BRUTEFORCE_C_PARALLEL;
    result.policy = (policy == KOLIBRI_SEARCH_POLICY_DEFAULT)
                    ? KOLIBRI_SEARCH_POLICY_FIRST_FOUND_FAST
                    : policy;
    result.known_high = known_high;
    result.low_start = low_start;
    result.low_end = low_end;
    result.target_hash = target_hash;
    result.space_size = space_size;

    volatile bool found = false;
    uint64_t best_low = 0;
    uint64_t attempts = 0;
    double t0 = get_time_ms();

#ifdef _OPENMP
    if (threads > 0) {
        omp_set_num_threads((int)threads);
    }

    if (result.policy == KOLIBRI_SEARCH_POLICY_LOWEST_KEY_IN_RANGE) {
        // Deterministic Policy: Find absolute minimum low in range
        uint64_t global_min_low = UINT64_MAX;
        bool global_found = false;

        #pragma omp parallel
        {
            uint64_t local_min_low = UINT64_MAX;
            bool local_found = false;
            uint64_t local_attempts = 0;

            #pragma omp for schedule(static)
            for (uint64_t x = low_start; x <= low_end; x++) {
                KolibriKey128 candidate;
                candidate.low = x;
                candidate.high = known_high;

                KolibriHash128 h = kolibri_hash_128(candidate);
                local_attempts++;

                if (h.low == target_hash.low && h.high == target_hash.high) {
                    if (!local_found || x < local_min_low) {
                        local_min_low = x;
                        local_found = true;
                    }
                }
            }

            #pragma omp atomic
            attempts += local_attempts;

            #pragma omp critical
            {
                if (local_found) {
                    if (!global_found || local_min_low < global_min_low) {
                        global_min_low = local_min_low;
                        global_found = true;
                    }
                }
            }
        }

        if (global_found) {
            found = true;
            best_low = global_min_low;
        }
    } else {
        // Fast Policy: First found wins (Early Exit)
        #pragma omp parallel shared(found, best_low, attempts)
        {
            #pragma omp for schedule(dynamic, 10000) nowait
            for (uint64_t x = low_start; x <= low_end; x++) {
                if (found) continue;

                KolibriKey128 candidate;
                candidate.low = x;
                candidate.high = known_high;

                KolibriHash128 h = kolibri_hash_128(candidate);

                #pragma omp atomic
                attempts++;

                if (h.low == target_hash.low && h.high == target_hash.high) {
                    #pragma omp critical
                    {
                        if (!found) {
                            found = true;
                            best_low = x;
                        }
                    }
                }
            }
        }
    }
#else
    // Fallback for single-thread execution
    for (uint64_t x = low_start; x <= low_end; x++) {
        KolibriKey128 candidate;
        candidate.low = x;
        candidate.high = known_high;

        KolibriHash128 h = kolibri_hash_128(candidate);
        attempts++;

        if (h.low == target_hash.low && h.high == target_hash.high) {
            found = true;
            best_low = x;
            break;
        }
        if (x == UINT64_MAX) break;
    }
#endif

    double t1 = get_time_ms();
    result.time_ms = t1 - t0;
    result.attempts = attempts;
    result.keys_per_second = (result.time_ms > 0.0) ? ((double)attempts / (result.time_ms / 1000.0)) : 0.0;
    result.threads = (threads > 0) ? threads : 1;

    if (found) {
        KolibriKey128 recovered_key;
        recovered_key.low = best_low;
        recovered_key.high = known_high;

        KolibriHash128 candidate_hash = kolibri_hash_128(recovered_key);

        result.status = KOLIBRI_SEARCH_OK;
        result.found = true;
        result.verified = (candidate_hash.low == target_hash.low && candidate_hash.high == target_hash.high);
        result.recovered_low = best_low;
        result.recovered_key = recovered_key;
        result.candidate_hash = candidate_hash;
    } else {
        result.status = KOLIBRI_SEARCH_NOT_FOUND;
        result.found = false;
        result.verified = false;
        result.recovered_low = 0;
        memset(&result.recovered_key, 0, sizeof(result.recovered_key));
        memset(&result.candidate_hash, 0, sizeof(result.candidate_hash));
    }

    return result;
}
