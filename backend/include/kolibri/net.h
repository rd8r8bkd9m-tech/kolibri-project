/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_NET_H
#define KOLIBRI_NET_H

#include "kolibri/formula.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KOLIBRI_MSG_HELLO = 1,
    KOLIBRI_MSG_MIGRATE_RULE = 2,
    KOLIBRI_MSG_ACK = 3,
    KOLIBRI_MSG_SWARM_KNOWLEDGE = 4,
    KOLIBRI_MSG_FITNESS_REQ = 5,
    KOLIBRI_MSG_FITNESS_RESP = 6,
    KOLIBRI_MSG_MUTATION_REQ = 7,
    KOLIBRI_MSG_MUTATION_RESP = 8,
    KOLIBRI_MSG_DIVERGENCE = 9
} KolibriNetMessageType;

typedef struct {
    KolibriNetMessageType type;
    union {
        struct {
            uint32_t node_id;
        } hello;
        struct {
            uint32_t node_id;
            uint16_t length;
            uint8_t digits[1024];
            double fitness;
        } formula;
        struct {
            char question[64];
            char answer[128];
            int input_hash;
            int output_hash;
        } knowledge;
        struct {
            uint8_t status;
        } ack;
        /* Swarm Protocol Second Order */
        struct {
            uint32_t node_id;
        } fitness_req;
        struct {
            KolibriEvolutionMetrics metrics;
        } fitness_resp;
        struct {
            uint32_t node_id;
            uint8_t mutation_type;
        } mutation_req;
        struct {
            uint32_t node_id;
            uint8_t digits[1024];
            uint16_t length;
        } mutation_resp;
        struct {
            uint32_t node_id;
            uint64_t expected_hash;
            uint64_t actual_hash;
        } divergence;
    } data;
} KolibriNetMessage;

size_t kn_message_encode_hello(uint8_t *buffer, size_t buffer_len, uint32_t node_id);
size_t kn_message_encode_formula(uint8_t *buffer, size_t buffer_len, uint32_t node_id, const KolibriFormula *formula);
size_t kn_message_encode_knowledge(uint8_t *buffer, size_t buffer_len, const char *q, const char *a);
size_t kn_message_encode_ack(uint8_t *buffer, size_t buffer_len, uint8_t status);

/* Second Order Encoders */
size_t kn_message_encode_fitness_req(uint8_t *buffer, size_t buffer_len, uint32_t node_id);
size_t kn_message_encode_fitness_resp(uint8_t *buffer, size_t buffer_len, const KolibriEvolutionMetrics *metrics);
size_t kn_message_encode_mutation_req(uint8_t *buffer, size_t buffer_len, uint32_t node_id, uint8_t mutation_type);
size_t kn_message_encode_mutation_resp(uint8_t *buffer, size_t buffer_len, uint32_t node_id, const uint8_t *digits, uint8_t length);
size_t kn_message_encode_divergence(uint8_t *buffer, size_t buffer_len, uint32_t node_id, uint64_t expected_hash, uint64_t actual_hash);

int kn_message_decode(const uint8_t *buffer, size_t buffer_len, KolibriNetMessage *out_message);

int kn_share_formula(const char *host, uint16_t port, uint32_t node_id, const KolibriFormula *formula);
int kn_share_knowledge(const char *host, uint16_t port, const char *q, const char *a);

int kn_request_fitness(const char *host, uint16_t port, uint32_t node_id, KolibriEvolutionMetrics *out_metrics);
int kn_send_divergence_report(const char *host, uint16_t port, uint32_t node_id, uint64_t expected_hash, uint64_t actual_hash);

typedef struct {
    int socket_fd;
    uint16_t port;
} KolibriNetListener;

int kn_listener_start(KolibriNetListener *listener, uint16_t port);
int kn_listener_poll(KolibriNetListener *listener, uint32_t timeout_ms, KolibriNetMessage *out_message);
void kn_listener_close(KolibriNetListener *listener);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_NET_H */
