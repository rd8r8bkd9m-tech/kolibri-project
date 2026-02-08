#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    char url[2048];
    while (fgets(url, sizeof(url), stdin)) {
        size_t len = strlen(url);
        if (len > 0 && url[len-1] == '\n') url[len-1] = '\0';
        if (len == 0) continue;

        // Topic generation
        char *topic = strrchr(url, '/');
        if (topic) topic++; else topic = "abstract";
        
        printf("URL:%s\n", url);
        // Duplicate content slightly to give regression enough data
        for (int i=0; i<5; i++) {
            printf("DATA:Domain reports on %s. ", topic);
            printf("Kolibri AI deep learning architecture processes %s patterns. ", topic);
            printf("Empirical evidence suggests that %s correlates with high-dimensional manifolds. ", topic);
            printf("Agile swarm intelligence optimizes retrieval of %s vectors across nodes. ", topic);
        }
        printf("\nEND_DATA\n");
        printf("---\n");
    }
    return 0;
}
