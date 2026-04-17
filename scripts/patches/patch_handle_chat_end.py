import re

with open("core/kolibri_http_server.c", "r") as f:
    content = f.read()

old_end = """        FILE *lf = fopen("knowledge/chat_learned.md", "a");
        if (lf) {
            char qa_entry[8192];
            snprintf(qa_entry, sizeof(qa_entry), "### Q: %s\\n%s\\n---\\n", message, save_answer);
            fwrite(qa_entry, 1, strlen(qa_entry), lf);
            fclose(lf);
        }
    }
}
}"""

new_end = """        FILE *lf = fopen("knowledge/chat_learned.md", "a");
        if (lf) {
            char qa_entry[8192];
            snprintf(qa_entry, sizeof(qa_entry), "### Q: %s\\n%s\\n---\\n", message, save_answer);
            fwrite(qa_entry, 1, strlen(qa_entry), lf);
            fclose(lf);
        }
    }
    
    /* Phase 1.1: Free decimal cognition block */
    k_free_decimal_block(decimal_input);
}
}"""

if old_end in content:
    content = content.replace(old_end, new_end)
    with open("core/kolibri_http_server.c", "w") as f:
        f.write(content)
else:
    print("Could not find old_end")

