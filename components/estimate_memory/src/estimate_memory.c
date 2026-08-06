#include "kolibri/estimate_memory.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct KolibriEstimateMemory {
    sqlite3 *db;
};

static const char *SIGNALS[] = {
    "agent_draft",
    "saved",
    "user_edited",
    "exported",
    "sent_to_client",
    "accepted_by_client",
    "contract_created",
    "act_created",
    "completed",
};

static const char *SCHEMA_SQL =
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS kem_metadata ("
    "key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "INSERT OR IGNORE INTO kem_metadata(key,value) VALUES"
    "('schema_version','1'),('trust_policy_version','local-trust-v1');"

    "CREATE TABLE IF NOT EXISTS kem_trust_policy ("
    "signal TEXT PRIMARY KEY, rank INTEGER NOT NULL CHECK(rank>=0),"
    "policy_version TEXT NOT NULL);"
    "INSERT OR IGNORE INTO kem_trust_policy(signal,rank,policy_version) VALUES"
    "('agent_draft',10,'local-trust-v1'),"
    "('saved',20,'local-trust-v1'),"
    "('user_edited',30,'local-trust-v1'),"
    "('exported',35,'local-trust-v1'),"
    "('sent_to_client',45,'local-trust-v1'),"
    "('accepted_by_client',60,'local-trust-v1'),"
    "('contract_created',75,'local-trust-v1'),"
    "('act_created',90,'local-trust-v1'),"
    "('completed',100,'local-trust-v1');"

    "CREATE TABLE IF NOT EXISTS kem_price_observations ("
    "observation_id TEXT PRIMARY KEY,"
    "workspace_id TEXT NOT NULL,"
    "actor_id TEXT NOT NULL DEFAULT '',"
    "estimate_id TEXT NOT NULL,"
    "estimate_version INTEGER NOT NULL CHECK(estimate_version>=1),"
    "line_id TEXT NOT NULL,"
    "normalized_key TEXT NOT NULL,"
    "original_name TEXT NOT NULL,"
    "domain TEXT NOT NULL DEFAULT '',"
    "region TEXT NOT NULL DEFAULT '',"
    "unit TEXT NOT NULL,"
    "price_minor INTEGER NOT NULL CHECK(price_minor>=0),"
    "currency TEXT NOT NULL DEFAULT 'RUB',"
    "vat_status TEXT NOT NULL DEFAULT 'unknown',"
    "source_kind TEXT NOT NULL DEFAULT 'agent',"
    "source_ref TEXT NOT NULL DEFAULT '',"
    "trust_signal TEXT NOT NULL,"
    "trust_rank INTEGER NOT NULL CHECK(trust_rank>=0),"
    "trust_policy_version TEXT NOT NULL,"
    "observed_at TEXT NOT NULL,"
    "last_used_at TEXT,"
    "usage_count INTEGER NOT NULL DEFAULT 0 CHECK(usage_count>=0),"
    "active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),"
    "UNIQUE(workspace_id,estimate_id,estimate_version,line_id));"
    "CREATE INDEX IF NOT EXISTS kem_price_lookup_idx ON kem_price_observations("
    "workspace_id,normalized_key,domain,region,unit,active,trust_rank DESC,observed_at DESC);"
    "CREATE INDEX IF NOT EXISTS kem_price_estimate_idx ON kem_price_observations("
    "workspace_id,estimate_id,estimate_version);"

    "CREATE TABLE IF NOT EXISTS kem_approval_events ("
    "event_row_id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "observation_id TEXT NOT NULL REFERENCES kem_price_observations(observation_id) ON DELETE CASCADE,"
    "signal TEXT NOT NULL,"
    "trust_rank INTEGER NOT NULL,"
    "policy_version TEXT NOT NULL,"
    "occurred_at TEXT NOT NULL,"
    "metadata_json TEXT NOT NULL DEFAULT '{}',"
    "UNIQUE(observation_id,signal,occurred_at));"
    "CREATE INDEX IF NOT EXISTS kem_approval_observation_idx ON kem_approval_events(observation_id,occurred_at);"

    "CREATE TABLE IF NOT EXISTS kem_sync_outbox ("
    "local_sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
    "event_id TEXT NOT NULL UNIQUE,"
    "event_type TEXT NOT NULL,"
    "privacy_class TEXT NOT NULL,"
    "payload_json TEXT NOT NULL CHECK(length(payload_json)<=32768),"
    "status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','retry','sent')),"
    "attempt_count INTEGER NOT NULL DEFAULT 0 CHECK(attempt_count>=0),"
    "last_error TEXT NOT NULL DEFAULT '',"
    "next_attempt_at TEXT,"
    "created_at TEXT NOT NULL,"
    "sent_at TEXT,"
    "server_cursor TEXT NOT NULL DEFAULT '');"
    "CREATE INDEX IF NOT EXISTS kem_outbox_pending_idx ON kem_sync_outbox(status,local_sequence);"

    "CREATE TABLE IF NOT EXISTS kem_sync_cursors ("
    "scope TEXT PRIMARY KEY, cursor TEXT NOT NULL, updated_at TEXT NOT NULL);";

static char *kem_strdup(const char *value) {
    if (!value) return NULL;
    size_t n = strlen(value);
    char *copy = (char *)malloc(n + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, n + 1U);
    return copy;
}

static int nonempty(const char *value) {
    return value && value[0] != '\0';
}

static char *current_iso8601(void) {
    time_t now = time(NULL);
    struct tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U) {
        return NULL;
    }
    return kem_strdup(buffer);
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (error) sqlite3_free(error);
    return rc;
}

static int begin_tx(sqlite3 *db) { return exec_sql(db, "BEGIN IMMEDIATE"); }
static int commit_tx(sqlite3 *db) { return exec_sql(db, "COMMIT"); }
static void rollback_tx(sqlite3 *db) { (void)exec_sql(db, "ROLLBACK"); }

const char *kem_signal_to_string(KemApprovalSignal signal) {
    if (signal < KEM_SIGNAL_AGENT_DRAFT || signal > KEM_SIGNAL_COMPLETED) {
        return NULL;
    }
    return SIGNALS[(int)signal];
}

int kem_signal_from_string(const char *value, KemApprovalSignal *out_signal) {
    if (!value || !out_signal) return SQLITE_MISUSE;
    for (int i = 0; i <= (int)KEM_SIGNAL_COMPLETED; ++i) {
        if (strcmp(value, SIGNALS[i]) == 0) {
            *out_signal = (KemApprovalSignal)i;
            return SQLITE_OK;
        }
    }
    return SQLITE_NOTFOUND;
}

static int trust_policy(sqlite3 *db, KemApprovalSignal signal,
                        int *out_rank, char **out_version) {
    const char *name = kem_signal_to_string(signal);
    if (!name || !out_rank || !out_version) return SQLITE_MISUSE;
    *out_version = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT rank,policy_version FROM kem_trust_policy WHERE signal=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out_rank = sqlite3_column_int(stmt, 0);
        *out_version = kem_strdup((const char *)sqlite3_column_text(stmt, 1));
        rc = *out_version ? SQLITE_OK : SQLITE_NOMEM;
    } else {
        rc = rc == SQLITE_DONE ? SQLITE_NOTFOUND : rc;
    }
    sqlite3_finalize(stmt);
    return rc;
}

int kem_open(const char *database_path, KolibriEstimateMemory **out_memory) {
    if (!nonempty(database_path) || !out_memory) return SQLITE_MISUSE;
    *out_memory = NULL;
    KolibriEstimateMemory *memory = (KolibriEstimateMemory *)calloc(1, sizeof(*memory));
    if (!memory) return SQLITE_NOMEM;

    int rc = sqlite3_open_v2(database_path, &memory->db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (rc != SQLITE_OK) {
        if (memory->db) sqlite3_close(memory->db);
        free(memory);
        return rc;
    }
    sqlite3_busy_timeout(memory->db, 10000);
    if ((rc = exec_sql(memory->db, "PRAGMA journal_mode=WAL")) != SQLITE_OK ||
        (rc = exec_sql(memory->db, "PRAGMA synchronous=NORMAL")) != SQLITE_OK ||
        (rc = exec_sql(memory->db, SCHEMA_SQL)) != SQLITE_OK) {
        sqlite3_close(memory->db);
        free(memory);
        return rc;
    }
    *out_memory = memory;
    return SQLITE_OK;
}

void kem_close(KolibriEstimateMemory *memory) {
    if (!memory) return;
    if (memory->db) sqlite3_close(memory->db);
    free(memory);
}

static int insert_approval_event(sqlite3 *db,
                                 const char *observation_id,
                                 const char *signal,
                                 int rank,
                                 const char *policy_version,
                                 const char *occurred_at,
                                 const char *metadata_json) {
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO kem_approval_events("
        "observation_id,signal,trust_rank,policy_version,occurred_at,metadata_json) "
        "VALUES(?,?,?,?,?,?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_text(stmt, 1, observation_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, signal, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, rank);
    sqlite3_bind_text(stmt, 4, policy_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, occurred_at, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, metadata_json ? metadata_json : "{}", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

int kem_record_price(KolibriEstimateMemory *memory,
                     const KemPriceObservationInput *input) {
    if (!memory || !memory->db || !input ||
        !nonempty(input->observation_id) || !nonempty(input->workspace_id) ||
        !nonempty(input->estimate_id) || input->estimate_version < 1 ||
        !nonempty(input->line_id) || !nonempty(input->normalized_key) ||
        !nonempty(input->original_name) || !nonempty(input->unit) ||
        input->price_minor < 0) {
        return SQLITE_MISUSE;
    }

    const char *signal = kem_signal_to_string(input->signal);
    if (!signal) return SQLITE_MISUSE;
    int rank = 0;
    char *policy_version = NULL;
    int rc = trust_policy(memory->db, input->signal, &rank, &policy_version);
    if (rc != SQLITE_OK) return rc;
    char *now = input->observed_at ? kem_strdup(input->observed_at) : current_iso8601();
    if (!now) { free(policy_version); return SQLITE_NOMEM; }

    rc = begin_tx(memory->db);
    if (rc != SQLITE_OK) { free(policy_version); free(now); return rc; }

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(memory->db,
        "INSERT INTO kem_price_observations("
        "observation_id,workspace_id,actor_id,estimate_id,estimate_version,line_id,"
        "normalized_key,original_name,domain,region,unit,price_minor,currency,vat_status,"
        "source_kind,source_ref,trust_signal,trust_rank,trust_policy_version,observed_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(observation_id) DO UPDATE SET "
        "actor_id=excluded.actor_id,estimate_id=excluded.estimate_id,"
        "estimate_version=excluded.estimate_version,line_id=excluded.line_id,"
        "normalized_key=excluded.normalized_key,original_name=excluded.original_name,"
        "domain=excluded.domain,region=excluded.region,unit=excluded.unit,"
        "price_minor=excluded.price_minor,currency=excluded.currency,"
        "vat_status=excluded.vat_status,source_kind=excluded.source_kind,"
        "source_ref=excluded.source_ref,observed_at=excluded.observed_at,"
        "trust_signal=CASE WHEN excluded.trust_rank>=kem_price_observations.trust_rank "
        "THEN excluded.trust_signal ELSE kem_price_observations.trust_signal END,"
        "trust_rank=MAX(kem_price_observations.trust_rank,excluded.trust_rank),"
        "trust_policy_version=CASE WHEN excluded.trust_rank>=kem_price_observations.trust_rank "
        "THEN excluded.trust_policy_version ELSE kem_price_observations.trust_policy_version END",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) goto fail;

    sqlite3_bind_text(stmt, 1, input->observation_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, input->workspace_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, input->actor_id ? input->actor_id : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, input->estimate_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, input->estimate_version);
    sqlite3_bind_text(stmt, 6, input->line_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, input->normalized_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, input->original_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, input->domain ? input->domain : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, input->region ? input->region : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, input->unit, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, input->price_minor);
    sqlite3_bind_text(stmt, 13, nonempty(input->currency) ? input->currency : KEM_CURRENCY_DEFAULT, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, nonempty(input->vat_status) ? input->vat_status : "unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, nonempty(input->source_kind) ? input->source_kind : "agent", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, input->source_ref ? input->source_ref : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, signal, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 18, rank);
    sqlite3_bind_text(stmt, 19, policy_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, now, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) goto fail;

    rc = insert_approval_event(memory->db, input->observation_id, signal, rank,
                               policy_version, now, "{}");
    if (rc != SQLITE_OK) goto fail;
    rc = commit_tx(memory->db);
    free(policy_version);
    free(now);
    return rc;

fail:
    if (stmt) sqlite3_finalize(stmt);
    rollback_tx(memory->db);
    free(policy_version);
    free(now);
    return rc;
}

int kem_record_approval(KolibriEstimateMemory *memory,
                        const char *observation_id,
                        KemApprovalSignal signal,
                        const char *occurred_at,
                        const char *metadata_json) {
    if (!memory || !memory->db || !nonempty(observation_id)) return SQLITE_MISUSE;
    const char *signal_name = kem_signal_to_string(signal);
    if (!signal_name) return SQLITE_MISUSE;
    int rank = 0;
    char *policy_version = NULL;
    int rc = trust_policy(memory->db, signal, &rank, &policy_version);
    if (rc != SQLITE_OK) return rc;
    char *now = occurred_at ? kem_strdup(occurred_at) : current_iso8601();
    if (!now) { free(policy_version); return SQLITE_NOMEM; }

    rc = begin_tx(memory->db);
    if (rc != SQLITE_OK) goto done;
    rc = insert_approval_event(memory->db, observation_id, signal_name, rank,
                               policy_version, now, metadata_json);
    if (rc != SQLITE_OK) goto fail;

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(memory->db,
        "UPDATE kem_price_observations SET "
        "trust_signal=CASE WHEN ? >= trust_rank THEN ? ELSE trust_signal END,"
        "trust_rank=MAX(trust_rank,?),"
        "trust_policy_version=CASE WHEN ? >= trust_rank THEN ? ELSE trust_policy_version END "
        "WHERE observation_id=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) goto fail;
    sqlite3_bind_int(stmt, 1, rank);
    sqlite3_bind_text(stmt, 2, signal_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, rank);
    sqlite3_bind_int(stmt, 4, rank);
    sqlite3_bind_text(stmt, 5, policy_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, observation_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) goto fail;
    if (sqlite3_changes(memory->db) == 0) { rc = SQLITE_NOTFOUND; goto fail; }
    rc = commit_tx(memory->db);
    goto done;

fail:
    rollback_tx(memory->db);
done:
    free(policy_version);
    free(now);
    return rc;
}

void kem_price_suggestion_clear(KemPriceSuggestion *s) {
    if (!s) return;
    free(s->observation_id); free(s->workspace_id); free(s->estimate_id);
    free(s->line_id); free(s->normalized_key); free(s->original_name);
    free(s->domain); free(s->region); free(s->unit); free(s->currency);
    free(s->vat_status); free(s->source_kind); free(s->trust_signal);
    free(s->observed_at); free(s->last_used_at);
    memset(s, 0, sizeof(*s));
}

static char *column_dup(sqlite3_stmt *stmt, int col) {
    const unsigned char *text = sqlite3_column_text(stmt, col);
    return text ? kem_strdup((const char *)text) : NULL;
}

int kem_find_best_price(KolibriEstimateMemory *memory,
                        const char *workspace_id,
                        const char *normalized_key,
                        const char *domain,
                        const char *region,
                        const char *unit,
                        KemPriceSuggestion *out) {
    if (!memory || !memory->db || !nonempty(workspace_id) ||
        !nonempty(normalized_key) || !nonempty(unit) || !out) return SQLITE_MISUSE;
    memset(out, 0, sizeof(*out));
    const char *domain_value = domain ? domain : "";
    const char *region_value = region ? region : "";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "SELECT observation_id,workspace_id,estimate_id,line_id,normalized_key,original_name,"
        "domain,region,unit,price_minor,currency,vat_status,source_kind,trust_signal,"
        "trust_rank,usage_count,observed_at,last_used_at "
        "FROM kem_price_observations WHERE workspace_id=? AND normalized_key=? AND unit=? "
        "AND active=1 AND (?='' OR domain=? OR domain='') AND (?='' OR region=? OR region='') "
        "ORDER BY CASE WHEN region=? AND region<>'' THEN 0 WHEN region='' THEN 1 ELSE 2 END,"
        "CASE WHEN domain=? AND domain<>'' THEN 0 WHEN domain='' THEN 1 ELSE 2 END,"
        "trust_rank DESC,observed_at DESC,usage_count DESC LIMIT 1",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_text(stmt, 1, workspace_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, normalized_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, unit, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, domain_value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, domain_value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, region_value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, region_value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, region_value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, domain_value, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out->observation_id = column_dup(stmt, 0);
        out->workspace_id = column_dup(stmt, 1);
        out->estimate_id = column_dup(stmt, 2);
        out->line_id = column_dup(stmt, 3);
        out->normalized_key = column_dup(stmt, 4);
        out->original_name = column_dup(stmt, 5);
        out->domain = column_dup(stmt, 6);
        out->region = column_dup(stmt, 7);
        out->unit = column_dup(stmt, 8);
        out->price_minor = sqlite3_column_int64(stmt, 9);
        out->currency = column_dup(stmt, 10);
        out->vat_status = column_dup(stmt, 11);
        out->source_kind = column_dup(stmt, 12);
        out->trust_signal = column_dup(stmt, 13);
        out->trust_rank = sqlite3_column_int(stmt, 14);
        out->usage_count = sqlite3_column_int(stmt, 15);
        out->observed_at = column_dup(stmt, 16);
        out->last_used_at = column_dup(stmt, 17);
        if (!out->observation_id || !out->workspace_id || !out->estimate_id ||
            !out->line_id || !out->normalized_key || !out->original_name ||
            !out->domain || !out->region || !out->unit || !out->currency ||
            !out->vat_status || !out->source_kind || !out->trust_signal ||
            !out->observed_at) {
            kem_price_suggestion_clear(out);
            rc = SQLITE_NOMEM;
        } else {
            rc = SQLITE_OK;
        }
    } else {
        rc = rc == SQLITE_DONE ? SQLITE_NOTFOUND : rc;
    }
    sqlite3_finalize(stmt);
    return rc;
}

int kem_record_price_use(KolibriEstimateMemory *memory,
                         const char *observation_id,
                         const char *used_at) {
    if (!memory || !memory->db || !nonempty(observation_id)) return SQLITE_MISUSE;
    char *now = used_at ? kem_strdup(used_at) : current_iso8601();
    if (!now) return SQLITE_NOMEM;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "UPDATE kem_price_observations SET usage_count=usage_count+1,last_used_at=? "
        "WHERE observation_id=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, observation_id, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE && sqlite3_changes(memory->db) == 0) rc = SQLITE_NOTFOUND;
        else if (rc == SQLITE_DONE) rc = SQLITE_OK;
    }
    if (stmt) sqlite3_finalize(stmt);
    free(now);
    return rc;
}

int kem_enqueue_price_for_relay(KolibriEstimateMemory *memory,
                                const char *observation_id,
                                const char *event_id,
                                const char *contributor_id,
                                const char *created_at,
                                int64_t *out_local_sequence) {
    if (!memory || !memory->db || !nonempty(observation_id) ||
        !nonempty(event_id) || !nonempty(contributor_id)) return SQLITE_MISUSE;
    char *now = created_at ? kem_strdup(created_at) : current_iso8601();
    if (!now) return SQLITE_NOMEM;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "INSERT OR IGNORE INTO kem_sync_outbox("
        "event_id,event_type,privacy_class,payload_json,status,created_at) "
        "SELECT ?, 'price.observation.v1', 'anonymised_price_observation_v1',"
        "json_object("
        "'schema_version','kolibri.relay.price_observation.v1',"
        "'event_id',?, 'contributor_id',?,"
        "'normalized_key',normalized_key,'domain',domain,'region',region,'unit',unit,"
        "'price_minor',price_minor,'currency',currency,'vat_status',vat_status,"
        "'source_kind',source_kind,'trust_signal',trust_signal,'trust_rank',trust_rank,"
        "'trust_policy_version',trust_policy_version,'observed_at',observed_at),"
        "'pending',? FROM kem_price_observations WHERE observation_id=?",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, event_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, event_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, contributor_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, now, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, observation_id, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            sqlite3_stmt *lookup = NULL;
            rc = sqlite3_prepare_v2(memory->db,
                "SELECT local_sequence FROM kem_sync_outbox WHERE event_id=?", -1, &lookup, NULL);
            if (rc == SQLITE_OK) {
                sqlite3_bind_text(lookup, 1, event_id, -1, SQLITE_TRANSIENT);
                rc = sqlite3_step(lookup);
                if (rc == SQLITE_ROW) {
                    if (out_local_sequence) *out_local_sequence = sqlite3_column_int64(lookup, 0);
                    rc = SQLITE_OK;
                } else {
                    rc = rc == SQLITE_DONE ? SQLITE_NOTFOUND : rc;
                }
            }
            if (lookup) sqlite3_finalize(lookup);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    free(now);
    return rc;
}

int kem_fetch_pending_outbox(KolibriEstimateMemory *memory,
                             size_t limit,
                             KemOutboxEvent **out_events,
                             size_t *out_count) {
    if (!memory || !memory->db || !out_events || !out_count || limit == 0U) return SQLITE_MISUSE;
    *out_events = NULL;
    *out_count = 0U;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "SELECT local_sequence,event_id,event_type,privacy_class,payload_json,attempt_count,created_at "
        "FROM kem_sync_outbox WHERE status IN ('pending','retry') "
        "ORDER BY local_sequence ASC LIMIT ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)limit);

    size_t count = 0U, capacity = 0U;
    KemOutboxEvent *events = NULL;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count == capacity) {
            size_t next = capacity == 0U ? 8U : capacity * 2U;
            KemOutboxEvent *grown = (KemOutboxEvent *)realloc(events, next * sizeof(*events));
            if (!grown) { rc = SQLITE_NOMEM; break; }
            memset(grown + capacity, 0, (next - capacity) * sizeof(*events));
            events = grown;
            capacity = next;
        }
        KemOutboxEvent *event = &events[count];
        event->local_sequence = sqlite3_column_int64(stmt, 0);
        event->event_id = column_dup(stmt, 1);
        event->event_type = column_dup(stmt, 2);
        event->privacy_class = column_dup(stmt, 3);
        event->payload_json = column_dup(stmt, 4);
        event->attempt_count = sqlite3_column_int(stmt, 5);
        event->created_at = column_dup(stmt, 6);
        if (!event->event_id || !event->event_type || !event->privacy_class ||
            !event->payload_json || !event->created_at) {
            rc = SQLITE_NOMEM;
            break;
        }
        count++;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        kem_free_outbox_events(events, count + (capacity > count ? 1U : 0U));
        return rc;
    }
    *out_events = events;
    *out_count = count;
    return SQLITE_OK;
}

void kem_free_outbox_events(KemOutboxEvent *events, size_t count) {
    if (!events) return;
    for (size_t i = 0; i < count; ++i) {
        free(events[i].event_id); free(events[i].event_type);
        free(events[i].privacy_class); free(events[i].payload_json);
        free(events[i].created_at);
    }
    free(events);
}

int kem_mark_outbox_sent(KolibriEstimateMemory *memory,
                         const char *event_id,
                         const char *server_cursor,
                         const char *sent_at) {
    if (!memory || !memory->db || !nonempty(event_id)) return SQLITE_MISUSE;
    char *now = sent_at ? kem_strdup(sent_at) : current_iso8601();
    if (!now) return SQLITE_NOMEM;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "UPDATE kem_sync_outbox SET status='sent',sent_at=?,server_cursor=?,last_error='',next_attempt_at=NULL "
        "WHERE event_id=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, server_cursor ? server_cursor : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, event_id, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE && sqlite3_changes(memory->db) == 0) rc = SQLITE_NOTFOUND;
        else if (rc == SQLITE_DONE) rc = SQLITE_OK;
    }
    if (stmt) sqlite3_finalize(stmt);
    free(now);
    return rc;
}

int kem_mark_outbox_retry(KolibriEstimateMemory *memory,
                          const char *event_id,
                          const char *error_code,
                          const char *next_attempt_at) {
    if (!memory || !memory->db || !nonempty(event_id)) return SQLITE_MISUSE;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "UPDATE kem_sync_outbox SET status='retry',attempt_count=attempt_count+1,"
        "last_error=?,next_attempt_at=? WHERE event_id=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, error_code ? error_code : "sync_failed", -1, SQLITE_TRANSIENT);
        if (next_attempt_at) sqlite3_bind_text(stmt, 2, next_attempt_at, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, 2);
        sqlite3_bind_text(stmt, 3, event_id, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE && sqlite3_changes(memory->db) == 0) rc = SQLITE_NOTFOUND;
        else if (rc == SQLITE_DONE) rc = SQLITE_OK;
    }
    if (stmt) sqlite3_finalize(stmt);
    return rc;
}

int kem_set_sync_cursor(KolibriEstimateMemory *memory,
                        const char *scope,
                        const char *cursor,
                        const char *updated_at) {
    if (!memory || !memory->db || !nonempty(scope) || !cursor) return SQLITE_MISUSE;
    char *now = updated_at ? kem_strdup(updated_at) : current_iso8601();
    if (!now) return SQLITE_NOMEM;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "INSERT INTO kem_sync_cursors(scope,cursor,updated_at) VALUES(?,?,?) "
        "ON CONFLICT(scope) DO UPDATE SET cursor=excluded.cursor,updated_at=excluded.updated_at",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cursor, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, now, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) rc = SQLITE_OK;
    }
    if (stmt) sqlite3_finalize(stmt);
    free(now);
    return rc;
}

int kem_get_sync_cursor(KolibriEstimateMemory *memory,
                        const char *scope,
                        char **out_cursor) {
    if (!memory || !memory->db || !nonempty(scope) || !out_cursor) return SQLITE_MISUSE;
    *out_cursor = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(memory->db,
        "SELECT cursor FROM kem_sync_cursors WHERE scope=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out_cursor = column_dup(stmt, 0);
        rc = *out_cursor ? SQLITE_OK : SQLITE_NOMEM;
    } else {
        rc = rc == SQLITE_DONE ? SQLITE_NOTFOUND : rc;
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int count_table(sqlite3 *db, const char *sql, int64_t *out) {
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int64(stmt, 0);
        rc = SQLITE_OK;
    }
    sqlite3_finalize(stmt);
    return rc;
}

int kem_stats(KolibriEstimateMemory *memory, KemStats *out) {
    if (!memory || !memory->db || !out) return SQLITE_MISUSE;
    memset(out, 0, sizeof(*out));
    int rc = count_table(memory->db, "SELECT COUNT(*) FROM kem_price_observations", &out->price_observations);
    if (rc != SQLITE_OK) return rc;
    rc = count_table(memory->db, "SELECT COUNT(*) FROM kem_approval_events", &out->approval_events);
    if (rc != SQLITE_OK) return rc;
    rc = count_table(memory->db, "SELECT COUNT(*) FROM kem_sync_outbox WHERE status IN ('pending','retry')", &out->pending_outbox_events);
    if (rc != SQLITE_OK) return rc;
    return count_table(memory->db, "SELECT COUNT(*) FROM kem_sync_outbox WHERE status='sent'", &out->sent_outbox_events);
}
