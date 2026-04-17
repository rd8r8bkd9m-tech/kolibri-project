static void handle_conversations(int fd) {
    char buffer[8192];
    int offset = 0;
    offset += sprintf(buffer + offset, "[");
    int count = 0;
    for (int i = 0; i < CHAT_STATE_MAX; i++) {
        if (g_chat_states[i].active) {
            if (count > 0) offset += sprintf(buffer + offset, ",");
            offset += sprintf(buffer + offset, "{\"id\":\"%s\",\"title\":\"Чат %d\"}", g_chat_states[i].conversation_id, i);
            count++;
        }
    }
    offset += sprintf(buffer + offset, "]");
    send_json(fd, 200, "OK", buffer);
}
