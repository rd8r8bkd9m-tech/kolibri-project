/*
 * Kolibri Knowledge Relay: replicate TEACH/USER_FEEDBACK from knowledge genome
 * to node genomes, re-signing with node HMAC keys.
 */

#include "kolibri/genome.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  KOLIBRI_RELAY_MODE_BROADCAST = 0,
  KOLIBRI_RELAY_MODE_SHARD = 1,
} KolibriRelayMode;

static int token_equals(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  return strcmp(a, b) == 0;
}

static int event_allowed(const char *event_type, const char *allow_csv) {
  if (!event_type || !allow_csv || allow_csv[0] == '\0') {
    return 0;
  }
  /* allow_csv example: "TEACH,USER_FEEDBACK,DEEP_L" */
  char buf[256];
  strncpy(buf, allow_csv, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';
  char *save = NULL;
  char *tok = strtok_r(buf, ",", &save);
  while (tok) {
    while (*tok == ' ' || *tok == '\t') {
      ++tok;
    }
    char *end = tok + strlen(tok);
    while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
      end[-1] = '\0';
      --end;
    }
    if (tok[0] != '\0' && token_equals(event_type, tok)) {
      return 1;
    }
    tok = strtok_r(NULL, ",", &save);
  }
  return 0;
}

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lsf = strlen(suffix);
  return ls >= lsf && strcmp(s + (ls - lsf), suffix) == 0;
}

static int is_genome_file(const char *name) { return ends_with(name, ".dat"); }

static int compare_strings(const void *a, const void *b) {
  const char *const *pa = (const char *const *)a;
  const char *const *pb = (const char *const *)b;
  return strcmp(*pa, *pb);
}

static int list_targets(const char *targets_dir, char ***out_paths,
                        size_t *out_count) {
  if (!targets_dir || !out_paths || !out_count) {
    return -1;
  }
  *out_paths = NULL;
  *out_count = 0U;

  DIR *dir = opendir(targets_dir);
  if (!dir) {
    return -1;
  }

  size_t capacity = 256U;
  size_t count = 0U;
  char **paths = (char **)calloc(capacity, sizeof(char *));
  if (!paths) {
    closedir(dir);
    return -1;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    if (!is_genome_file(ent->d_name)) {
      continue;
    }
    char full[512];
    snprintf(full, sizeof(full), "%s/%s", targets_dir, ent->d_name);
    char *dup = strdup(full);
    if (!dup) {
      continue;
    }
    if (count >= capacity) {
      size_t next = capacity * 2U;
      char **grown = (char **)realloc(paths, next * sizeof(char *));
      if (!grown) {
        free(dup);
        break;
      }
      memset(grown + capacity, 0, (next - capacity) * sizeof(char *));
      paths = grown;
      capacity = next;
    }
    paths[count++] = dup;
  }
  closedir(dir);

  if (count > 1U) {
    qsort(paths, count, sizeof(char *), compare_strings);
  }

  *out_paths = paths;
  *out_count = count;
  return 0;
}

static void free_targets(char **paths, size_t count) {
  if (!paths) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    free(paths[i]);
  }
  free(paths);
}

static void relay_event_to_target(const char *target_path, const unsigned char *key,
                                  size_t key_len, const char *event_type,
                                  const char *payload) {
  KolibriGenome g;
  if (kg_open(&g, target_path, key, key_len) != 0) {
    fprintf(stderr, "[relay] open target failed: %s\n", target_path);
    return;
  }
  if (kg_append(&g, event_type, payload, NULL) != 0) {
    fprintf(stderr, "[relay] append failed: %s\n", target_path);
  }
  kg_close(&g);
}

static int load_key_from_file(const char *path, unsigned char *out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  size_t total = fread(out, 1, KOLIBRI_HMAC_KEY_SIZE, f);
  fclose(f);
  if (total == 0) return -1;
  *out_len = total;
  return 0;
}

int main(int argc, char **argv) {
  const char *source_path = ".kolibri/knowledge_genome.dat";
  const char *targets_dir = "build/cluster";
  const char *target_key_path = "build/cluster/swarm.key";
  const char *target_key_inline = NULL;
  const char *offset_path = ".kolibri/knowledge_relay.offset";
  KolibriRelayMode mode = KOLIBRI_RELAY_MODE_BROADCAST;
  unsigned long long max_events = 0ULL;
  const char *allow_events = "TEACH,USER_FEEDBACK";

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
      source_path = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--targets-dir") == 0 && i + 1 < argc) {
      targets_dir = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--target-key") == 0 && i + 1 < argc) {
      target_key_path = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--target-key-inline") == 0 && i + 1 < argc) {
      target_key_inline = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
      offset_path = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
      const char *value = argv[++i];
      if (strcmp(value, "broadcast") == 0) {
        mode = KOLIBRI_RELAY_MODE_BROADCAST;
      } else if (strcmp(value, "shard") == 0) {
        mode = KOLIBRI_RELAY_MODE_SHARD;
      } else {
        fprintf(stderr, "[relay] unknown mode: %s\n", value);
        return 2;
      }
      continue;
    }
    if (strcmp(argv[i], "--max-events") == 0 && i + 1 < argc) {
      max_events = strtoull(argv[++i], NULL, 10);
      continue;
    }
    if (strcmp(argv[i], "--allow") == 0 && i + 1 < argc) {
      allow_events = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [--source PATH] [--targets-dir DIR] [--target-key FILE] [--offset FILE] [--mode broadcast|shard] [--max-events N] [--allow CSV]\n", argv[0]);
      return 0;
    }
  }

  char **targets = NULL;
  size_t targets_count = 0U;
  if (list_targets(targets_dir, &targets, &targets_count) != 0 || targets_count == 0U) {
    fprintf(stderr, "[relay] cannot list targets in %s\n", targets_dir);
    free_targets(targets, targets_count);
    return 1;
  }

  unsigned char target_key[KOLIBRI_HMAC_KEY_SIZE];
  size_t target_key_len = 0U;
  if (target_key_inline && target_key_inline[0] != '\0') {
    size_t len = strlen(target_key_inline);
    if (len > sizeof(target_key)) len = sizeof(target_key);
    memcpy(target_key, target_key_inline, len);
    target_key_len = len;
  } else {
    if (load_key_from_file(target_key_path, target_key, &target_key_len) != 0) {
      fprintf(stderr, "[relay] failed to load target key: %s\n", target_key_path);
      return 1;
    }
  }

  FILE *src = fopen(source_path, "rb");
  if (!src) {
    fprintf(stderr, "[relay] cannot open source %s: %s\n", source_path, strerror(errno));
    return 1;
  }

  unsigned long long start_index = 0ULL;
  FILE *ofs = fopen(offset_path, "r");
  if (ofs) {
    if (fscanf(ofs, "%llu", &start_index) != 1) {
      start_index = 0ULL;
    }
    fclose(ofs);
  }

  /* Iterate over fixed-size blocks */
  unsigned char bytes[KOLIBRI_BLOCK_SIZE];
  unsigned long long processed = 0ULL;

  /* Skip blocks below start_index by reading and counting */
  while (fread(bytes, 1, KOLIBRI_BLOCK_SIZE, src) == KOLIBRI_BLOCK_SIZE) {
    /* Deserialize minimal fields: index, event_type, payload */
    unsigned long long idx = 0ULL;
    for (int i = 0; i < 8; ++i) {
      idx = (idx << 8) | (unsigned long long)bytes[i];
    }
    if (idx < start_index) {
      continue;
    }

    char event_type[KOLIBRI_EVENT_TYPE_SIZE + 1];
    char payload[KOLIBRI_PAYLOAD_SIZE + 1];
    memset(event_type, 0, sizeof(event_type));
    memset(payload, 0, sizeof(payload));
    memcpy(event_type, bytes + 16 + KOLIBRI_HASH_SIZE * 2, KOLIBRI_EVENT_TYPE_SIZE);
    memcpy(payload, bytes + 16 + KOLIBRI_HASH_SIZE * 2 + KOLIBRI_EVENT_TYPE_SIZE, KOLIBRI_PAYLOAD_SIZE);
    event_type[KOLIBRI_EVENT_TYPE_SIZE] = '\0';
    payload[KOLIBRI_PAYLOAD_SIZE] = '\0';

    /* Filter events */
    if (!event_allowed(event_type, allow_events)) {
      start_index = idx + 1ULL;
      continue;
    }

    if (mode == KOLIBRI_RELAY_MODE_SHARD) {
      /* распределяем события по узлам: idx % targets_count */
      size_t target_index = (size_t)(idx % (unsigned long long)targets_count);
      relay_event_to_target(targets[target_index], target_key, target_key_len,
                            event_type, payload);
    } else {
      /* Broadcast to all node genomes */
      for (size_t ti = 0; ti < targets_count; ++ti) {
        relay_event_to_target(targets[ti], target_key, target_key_len, event_type,
                              payload);
      }
    }

    start_index = idx + 1ULL;
    processed += 1ULL;

    if (max_events > 0ULL && processed >= max_events) {
      break;
    }
  }

  fclose(src);

  ofs = fopen(offset_path, "w");
  if (ofs) {
    fprintf(ofs, "%llu\n", start_index);
    fclose(ofs);
  }

  free_targets(targets, targets_count);

    printf("[relay] processed %llu events (mode=%s, targets=%zu, allow=%s)\n",
         processed,
         mode == KOLIBRI_RELAY_MODE_SHARD ? "shard" : "broadcast",
      targets_count,
      allow_events ? allow_events : "");
  return 0;
}
