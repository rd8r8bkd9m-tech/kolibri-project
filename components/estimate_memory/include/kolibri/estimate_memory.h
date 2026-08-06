/*
 * Kolibri Estimate Memory — local-first SQLite memory for estimates and prices.
 *
 * The module stores personal/workspace observations locally, promotes them from
 * real workflow signals (saved/exported/sent/contract/act), and emits a
 * privacy-bounded outbox event for relay aggregation. It never stores money in
 * binary floating point.
 */
#ifndef KOLIBRI_ESTIMATE_MEMORY_H
#define KOLIBRI_ESTIMATE_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEM_SCHEMA_VERSION 1
#define KEM_TRUST_POLICY_VERSION "local-trust-v1"
#define KEM_CURRENCY_DEFAULT "RUB"

typedef struct KolibriEstimateMemory KolibriEstimateMemory;

typedef enum {
    KEM_SIGNAL_AGENT_DRAFT = 0,
    KEM_SIGNAL_SAVED = 1,
    KEM_SIGNAL_USER_EDITED = 2,
    KEM_SIGNAL_EXPORTED = 3,
    KEM_SIGNAL_SENT_TO_CLIENT = 4,
    KEM_SIGNAL_ACCEPTED_BY_CLIENT = 5,
    KEM_SIGNAL_CONTRACT_CREATED = 6,
    KEM_SIGNAL_ACT_CREATED = 7,
    KEM_SIGNAL_COMPLETED = 8,
} KemApprovalSignal;

typedef struct {
    const char *observation_id;
    const char *workspace_id;
    const char *actor_id;
    const char *estimate_id;
    int64_t estimate_version;
    const char *line_id;
    const char *normalized_key;
    const char *original_name;
    const char *domain;
    const char *region;
    const char *unit;
    int64_t price_minor;
    const char *currency;
    const char *vat_status;
    const char *source_kind;
    const char *source_ref;
    KemApprovalSignal signal;
    const char *observed_at;
} KemPriceObservationInput;

typedef struct {
    char *observation_id;
    char *workspace_id;
    char *estimate_id;
    char *line_id;
    char *normalized_key;
    char *original_name;
    char *domain;
    char *region;
    char *unit;
    int64_t price_minor;
    char *currency;
    char *vat_status;
    char *source_kind;
    char *trust_signal;
    int trust_rank;
    int usage_count;
    char *observed_at;
    char *last_used_at;
} KemPriceSuggestion;

typedef struct {
    int64_t local_sequence;
    char *event_id;
    char *event_type;
    char *privacy_class;
    char *payload_json;
    int attempt_count;
    char *created_at;
} KemOutboxEvent;

typedef struct {
    int64_t price_observations;
    int64_t approval_events;
    int64_t pending_outbox_events;
    int64_t sent_outbox_events;
} KemStats;

int kem_open(const char *database_path, KolibriEstimateMemory **out_memory);
void kem_close(KolibriEstimateMemory *memory);

const char *kem_signal_to_string(KemApprovalSignal signal);
int kem_signal_from_string(const char *value, KemApprovalSignal *out_signal);

int kem_record_price(KolibriEstimateMemory *memory,
                     const KemPriceObservationInput *input);

int kem_record_approval(KolibriEstimateMemory *memory,
                        const char *observation_id,
                        KemApprovalSignal signal,
                        const char *occurred_at,
                        const char *metadata_json);

int kem_find_best_price(KolibriEstimateMemory *memory,
                        const char *workspace_id,
                        const char *normalized_key,
                        const char *domain,
                        const char *region,
                        const char *unit,
                        KemPriceSuggestion *out_suggestion);

void kem_price_suggestion_clear(KemPriceSuggestion *suggestion);

int kem_record_price_use(KolibriEstimateMemory *memory,
                         const char *observation_id,
                         const char *used_at);

/*
 * Enqueue an anonymised relay event generated from a local observation.
 * contributor_id must be a pseudonymous relay identity, not an email, phone,
 * customer identifier or exact address. The payload intentionally excludes
 * estimate IDs, actor IDs, source references and original free text.
 */
int kem_enqueue_price_for_relay(KolibriEstimateMemory *memory,
                                const char *observation_id,
                                const char *event_id,
                                const char *contributor_id,
                                const char *created_at,
                                int64_t *out_local_sequence);

int kem_fetch_pending_outbox(KolibriEstimateMemory *memory,
                             size_t limit,
                             KemOutboxEvent **out_events,
                             size_t *out_count);

void kem_free_outbox_events(KemOutboxEvent *events, size_t count);

int kem_mark_outbox_sent(KolibriEstimateMemory *memory,
                         const char *event_id,
                         const char *server_cursor,
                         const char *sent_at);

int kem_mark_outbox_retry(KolibriEstimateMemory *memory,
                          const char *event_id,
                          const char *error_code,
                          const char *next_attempt_at);

int kem_set_sync_cursor(KolibriEstimateMemory *memory,
                        const char *scope,
                        const char *cursor,
                        const char *updated_at);

int kem_get_sync_cursor(KolibriEstimateMemory *memory,
                        const char *scope,
                        char **out_cursor);

int kem_stats(KolibriEstimateMemory *memory, KemStats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ESTIMATE_MEMORY_H */
