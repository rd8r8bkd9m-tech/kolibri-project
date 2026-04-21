import sys

def patch_compress():
    path = "core/compress.c"
    with open(path, "r") as f:
        content = f.read()

    # We need to wrap the whole fast_bwt block or the divsufsort calls.
    # Actually, in core/compress.c, any code calling bw_transform should be under #ifndef EMSCRIPTEN
    # Alternatively, define dummy functions for EMSCRIPTEN.

    # Let's see if we can just define dummy bw_transform for EMSCRIPTEN.
    dummy = """
#ifdef EMSCRIPTEN
typedef int saidx_t;
static int bw_transform(const unsigned char *T, unsigned char *U, saidx_t *A, saidx_t n, saidx_t *primary_index) {
    return -1; // Fail BWT, fallback to other methods
}
#endif
"""
    if "typedef int saidx_t;" not in content:
        content = content.replace("#ifndef EMSCRIPTEN\n#include <divsufsort.h>  /* v70: BWT preprocessing via libdivsufsort */\n#endif", "#ifndef EMSCRIPTEN\n#include <divsufsort.h>  /* v70: BWT preprocessing via libdivsufsort */\n#endif\n" + dummy)

    with open(path, "w") as f:
        f.write(content)

def patch_knowledge_queue():
    path = "core/knowledge_queue.c"
    with open(path, "r") as f:
        content = f.read()

    if "#ifndef EMSCRIPTEN" not in content[:100]:
        content = "#ifndef EMSCRIPTEN\n" + content + "\n#endif /* EMSCRIPTEN */\n"
        with open(path, "w") as f:
            f.write(content)

def patch_kolibri_memory():
    path = "backend/src/kolibri_memory.c"
    with open(path, "r") as f:
        content = f.read()

    content = content.replace("kolibri_fractal_memory_init()", "0")
    content = content.replace("kolibri_logical_memory_init()", "0")
    content = content.replace("kolibri_fractal_memory_store", "// kolibri_fractal_memory_store")
    content = content.replace("kolibri_fractal_memory_cleanup()", "// kolibri_fractal_memory_cleanup()")
    content = content.replace("kolibri_logical_memory_cleanup()", "// kolibri_logical_memory_cleanup()")

    with open(path, "w") as f:
        f.write(content)

patch_compress()
patch_knowledge_queue()
patch_kolibri_memory()
