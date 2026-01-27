/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define KOLIBRI_HEADER_SIZE 3U
#define KOLIBRI_MAX_PAYLOAD 256U

static uint64_t kolibri_htonll(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (((uint64_t)htonl((uint32_t)(value & 0xFFFFFFFFULL))) << 32) |
         htonl((uint32_t)(value >> 32));
#else
  return value;
#endif
}

static uint64_t kolibri_ntohll(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (((uint64_t)ntohl((uint32_t)(value & 0xFFFFFFFFULL))) << 32) |
         ntohl((uint32_t)(value >> 32));
#else
  return value;
#endif
}

static size_t kolibri_write_header(uint8_t *buffer, size_t buffer_len,
                                   KolibriNetMessageType type,
                                   uint16_t payload_length) {
  if (!buffer || buffer_len < KOLIBRI_HEADER_SIZE) {
    return 0;
  }
  buffer[0] = (uint8_t)type;
  uint16_t be_len = htons(payload_length);
  memcpy(&buffer[1], &be_len, sizeof(be_len));
  return KOLIBRI_HEADER_SIZE;
}

static int kolibri_send_all(int sockfd, const uint8_t *data, size_t len) {
  size_t sent_total = 0;
  while (sent_total < len) {
    ssize_t sent = send(sockfd, data + sent_total, len - sent_total, 0);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (sent == 0) {
      return -1;
    }
    sent_total += (size_t)sent;
  }
  return 0;
}

static int kolibri_recv_all(int sockfd, uint8_t *data, size_t len) {
  size_t received_total = 0;
  while (received_total < len) {
    ssize_t received = recv(sockfd, data + received_total,
                            len - received_total, 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (received == 0) {
      return -1;
    }
    received_total += (size_t)received;
  }
  return 0;
}

size_t kn_message_encode_hello(uint8_t *buffer, size_t buffer_len,
                               uint32_t node_id) {
  if (!buffer) {
    return 0;
  }
  uint8_t payload[sizeof(uint32_t)];
  uint32_t be_id = htonl(node_id);
  memcpy(payload, &be_id, sizeof(be_id));

  size_t header = kolibri_write_header(buffer, buffer_len, KOLIBRI_MSG_HELLO,
                                       sizeof(payload));
  if (header == 0 || buffer_len < header + sizeof(payload)) {
    return 0;
  }
  memcpy(buffer + header, payload, sizeof(payload));
  return header + sizeof(payload);
}

size_t kn_message_encode_formula(uint8_t *buffer, size_t buffer_len,
                                 uint32_t node_id,
                                 const KolibriFormula *formula) {
  if (!buffer || !formula) {
    return 0;
  }

  uint8_t digits[32];
  size_t digit_len = kf_formula_digits(formula, digits, sizeof(digits));
  if (digit_len == 0 || digit_len > sizeof(digits)) {
    return 0;
  }

  uint8_t payload[KOLIBRI_MAX_PAYLOAD];
  uint32_t be_node = htonl(node_id);
  uint64_t fitness_bits;
  memcpy(&fitness_bits, &formula->fitness, sizeof(fitness_bits));
  fitness_bits = kolibri_htonll(fitness_bits);

  size_t offset = 0;
  memcpy(payload + offset, &be_node, sizeof(be_node));
  offset += sizeof(be_node);
  payload[offset++] = (uint8_t)digit_len;
  memcpy(payload + offset, digits, digit_len);
  offset += digit_len;
  memcpy(payload + offset, &fitness_bits, sizeof(fitness_bits));
  offset += sizeof(fitness_bits);

  size_t header =
      kolibri_write_header(buffer, buffer_len, KOLIBRI_MSG_MIGRATE_RULE, offset);
  if (header == 0 || buffer_len < header + offset) {
    return 0;
  }
  memcpy(buffer + header, payload, offset);
  return header + offset;
}

size_t kn_message_encode_knowledge(uint8_t *buffer, size_t buffer_len, const char *q, const char *a) {
  if (!buffer || !q || !a) return 0;
  uint8_t payload[256];
  memset(payload, 0, sizeof(payload));
  size_t qlen = strlen(q);
  size_t alen = strlen(a);
  if (qlen > 60) qlen = 60;
  if (alen > 120) alen = 120;
  
  uint32_t ih = (uint32_t)kf_hash_from_text(q);
  uint32_t oh = (uint32_t)kf_hash_from_text(a);
  uint32_t be_ih = htonl(ih);
  uint32_t be_oh = htonl(oh);

  size_t offset = 0;
  memcpy(payload + offset, &be_ih, 4); offset += 4;
  memcpy(payload + offset, &be_oh, 4); offset += 4;
  payload[offset++] = (uint8_t)qlen;
  memcpy(payload + offset, q, qlen); offset += qlen;
  payload[offset++] = (uint8_t)alen;
  memcpy(payload + offset, a, alen); offset += alen;

  size_t header = kolibri_write_header(buffer, buffer_len, KOLIBRI_MSG_SWARM_KNOWLEDGE, (uint16_t)offset);
  if (header == 0 || buffer_len < header + offset) return 0;
  memcpy(buffer + header, payload, offset);
  return header + offset;
}

size_t kn_message_encode_ack(uint8_t *buffer, size_t buffer_len, uint8_t status) {
  size_t header = kolibri_write_header(buffer, buffer_len, KOLIBRI_MSG_ACK, 1);
  if (header == 0 || buffer_len < header + 1) return 0;
  buffer[header] = status;
  return header + 1;
}

int kn_message_decode(const uint8_t *buffer, size_t buffer_len,
                      KolibriNetMessage *out_message) {
  if (!buffer || buffer_len < KOLIBRI_HEADER_SIZE || !out_message) {
    return -1;
  }

  uint8_t type_byte = buffer[0];
  uint16_t payload_len;
  memcpy(&payload_len, &buffer[1], sizeof(payload_len));
  payload_len = ntohs(payload_len);

  if (buffer_len < KOLIBRI_HEADER_SIZE + payload_len) {
    return -1;
  }

  out_message->type = (KolibriNetMessageType)type_byte;
  const uint8_t *payload = buffer + KOLIBRI_HEADER_SIZE;

  switch (out_message->type) {
  case KOLIBRI_MSG_HELLO: {
    if (payload_len != sizeof(uint32_t)) {
      return -1;
    }
    uint32_t node_id;
    memcpy(&node_id, payload, sizeof(node_id));
    out_message->data.hello.node_id = ntohl(node_id);
    break;
  }
  case KOLIBRI_MSG_MIGRATE_RULE: {
    if (payload_len < sizeof(uint32_t) + 1 + sizeof(uint64_t)) {
      return -1;
    }
    size_t offset = 0;
    uint32_t node_raw;
    memcpy(&node_raw, payload + offset, sizeof(node_raw));
    offset += sizeof(node_raw);
    uint8_t length = payload[offset++];
    if (length > 32U || payload_len < offset + length + sizeof(uint64_t)) {
      return -1;
    }
    memset(out_message->data.formula.digits, 0,
           sizeof(out_message->data.formula.digits));
    memcpy(out_message->data.formula.digits, payload + offset, length);
    out_message->data.formula.length = length;
    offset += length;
    uint64_t fitness_raw;
    memcpy(&fitness_raw, payload + offset, sizeof(fitness_raw));
    fitness_raw = kolibri_ntohll(fitness_raw);
    double fitness_value;
    memcpy(&fitness_value, &fitness_raw, sizeof(fitness_value));
    out_message->data.formula.node_id = ntohl(node_raw);
    out_message->data.formula.fitness = fitness_value;
    break;
  }
  case KOLIBRI_MSG_SWARM_KNOWLEDGE: {
    if (payload_len < 11) return -1;
    size_t offset = 0;
    uint32_t ih, oh;
    memcpy(&ih, payload, 4); offset += 4;
    memcpy(&oh, payload + offset, 4); offset += 4;
    out_message->data.knowledge.input_hash = (int)ntohl(ih);
    out_message->data.knowledge.output_hash = (int)ntohl(oh);
    uint8_t qlen = payload[offset++];
    if (offset + qlen > payload_len) return -1;
    memcpy(out_message->data.knowledge.question, payload + offset, qlen);
    out_message->data.knowledge.question[qlen] = '\0';
    offset += qlen;
    uint8_t alen = payload[offset++];
    if (offset + alen > payload_len) return -1;
    memcpy(out_message->data.knowledge.answer, payload + offset, alen);
    out_message->data.knowledge.answer[alen] = '\0';
    break;
  }
  case KOLIBRI_MSG_ACK: {
    if (payload_len != 1) {
      return -1;
    }
    out_message->data.ack.status = payload[0];
    break;
  }
  default:
    return -1;
  }

  return 0;
}

static int kn_send_message(int sockfd, const uint8_t *buffer, size_t len) {
  if (!buffer || len == 0) {
    return -1;
  }
  return kolibri_send_all(sockfd, buffer, len);
}

int kn_share_formula(const char *host, uint16_t port, uint32_t node_id,
                     const KolibriFormula *formula) {
  if (!host || !formula) {
    return -1;
  }
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(sockfd); return -1; }
  struct timeval tv = {2, 0};
  setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(sockfd); return -1; }
  uint8_t buffer[KOLIBRI_MAX_PAYLOAD + 10];
  size_t len = kn_message_encode_formula(buffer, sizeof(buffer), node_id, formula);
  int status = kn_send_message(sockfd, buffer, len);
  close(sockfd);
  return status;
}

int kn_share_knowledge(const char *host, uint16_t port, const char *q, const char *a) {
  if (!host || !q || !a) return -1;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(sockfd); return -1; }
  struct timeval tv = {2, 0};
  setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(sockfd); return -1; }
  uint8_t buffer[512];
  size_t len = kn_message_encode_knowledge(buffer, sizeof(buffer), q, a);
  int status = kn_send_message(sockfd, buffer, len);
  close(sockfd);
  return status;
}

int kn_listener_start(KolibriNetListener *listener, uint16_t port) {
  if (!listener) {
    return -1;
  }

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    listener->socket_fd = -1;
    return -1;
  }

  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(sockfd);
    listener->socket_fd = -1;
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sockfd);
    listener->socket_fd = -1;
    return -1;
  }

  if (listen(sockfd, 4) < 0) {
    close(sockfd);
    listener->socket_fd = -1;
    return -1;
  }

  listener->socket_fd = sockfd;
  listener->port = port;
  return 0;
}

int kn_listener_poll(KolibriNetListener *listener, uint32_t timeout_ms,
                     KolibriNetMessage *out_message) {
  if (!listener || listener->socket_fd < 0 || !out_message) {
    return -1;
  }

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(listener->socket_fd, &readfds);

  struct timeval tv;
  struct timeval *timeout_ptr = NULL;
  if (timeout_ms != UINT32_MAX) {
    /* Pass an explicit zeroed timeval when timeout_ms == 0 to preserve
       non-blocking semantics. UINT32_MAX can be used to wait indefinitely. */
    tv.tv_sec = timeout_ms / 1000U;
    tv.tv_usec = (timeout_ms % 1000U) * 1000U;
    timeout_ptr = &tv;
  }

  int ready = select(listener->socket_fd + 1, &readfds, NULL, NULL, timeout_ptr);
  if (ready < 0) {
    if (errno == EINTR) {
      return 0;
    }
    return -1;
  }
  if (ready == 0) {
    return 0;
  }

  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd = accept(listener->socket_fd, (struct sockaddr *)&client_addr,
                         &client_len);
  if (client_fd < 0) {
    return -1;
  }

  uint8_t buffer[KOLIBRI_HEADER_SIZE + KOLIBRI_MAX_PAYLOAD];

  KolibriNetMessage last_message;
  bool has_last = false;

  while (true) {
    if (kolibri_recv_all(client_fd, buffer, KOLIBRI_HEADER_SIZE) != 0) {
      break;
    }
    uint16_t payload_len;
    memcpy(&payload_len, &buffer[1], sizeof(payload_len));
    payload_len = ntohs(payload_len);
    if (payload_len > KOLIBRI_MAX_PAYLOAD) {
      break;
    }
    if (kolibri_recv_all(client_fd, buffer + KOLIBRI_HEADER_SIZE,
                         payload_len) != 0) {
      break;
    }
    size_t message_len = KOLIBRI_HEADER_SIZE + payload_len;
    KolibriNetMessage decoded;
    if (kn_message_decode(buffer, message_len, &decoded) == 0) {
      has_last = true;
      last_message = decoded;
      if (decoded.type == KOLIBRI_MSG_MIGRATE_RULE) {
        *out_message = decoded;
        close(client_fd);
        return 1;
      }
      continue;
    }
  }

  if (has_last) {
    *out_message = last_message;
    close(client_fd);
    return 1;
  }

  close(client_fd);
  return -1;
}

void kn_listener_close(KolibriNetListener *listener) {
  if (!listener) {
    return;
  }
  if (listener->socket_fd >= 0) {
    close(listener->socket_fd);
  }
  listener->socket_fd = -1;
}
