#include <stdio.h>
#include <string.h>
int main() {
    FILE *f = fopen("knowledge/knowledge_base_full.md", "r");
    char line[2048], cur_q[1024]={0}, cur_a[1024]={0};
    int in_a=0, count=0;
    while(fgets(line,sizeof(line),f) && count<5) {
        if(strncmp(line,"### Q",5)==0) {
            if(cur_q[0]&&cur_a[0]) {
                printf("Q[%d]: [%s]\nA[%d]: [%s]\n---\n",count,cur_q,count,cur_a);
                count++;
            }
            char *c=strchr(line,':');
            if(c){strncpy(cur_q,c+2,1023);cur_q[strcspn(cur_q,"\r\n")]=0;cur_a[0]=0;in_a=0;}
        } else if(strncmp(line,"**Ответ:**",10)==0) {
            const char *p=line+10;
            while(*p==' ' || *p=='\t')p++;
            strncpy(cur_a,p,1023);
            cur_a[strcspn(cur_a,"\r\n")]=0;
            in_a=1;
        }
    }
    fclose(f);
    return 0;
}
