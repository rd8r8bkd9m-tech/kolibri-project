#include <stdio.h>
#include <stdlib.h>

extern int kolibri_bridge_init(void);
extern int kolibri_bridge_query_json(const char *query, char *out_json, size_t out_capacity);

int main() {
    printf("Init...\n");
    if (kolibri_bridge_init() != 0) {
        printf("Init failed\n");
        return 1;
    }
    printf("Init OK. Querying...\n");
    char buffer[8192];
    int rc = kolibri_bridge_query_json("Привет", buffer, sizeof(buffer));
    printf("RC: %d\nJSON: %s\n", rc, buffer);
    return 0;
}
