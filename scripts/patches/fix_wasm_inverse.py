import sys

def patch_compress_inverse():
    path = "core/compress.c"
    with open(path, "r") as f:
        content = f.read()

    dummy_inverse = """
#ifdef EMSCRIPTEN
static int inverse_bw_transform(const unsigned char *T, unsigned char *U, saidx_t *A, saidx_t n, saidx_t primary_index) {
    return -1;
}
#endif
"""
    if "static int inverse_bw_transform(" not in content:
        # insert after the previous patch
        target = "static int bw_transform("
        idx = content.find(target)
        if idx != -1:
            end_idx = content.find("#endif", idx) + 6
            content = content[:end_idx] + dummy_inverse + content[end_idx:]

            with open(path, "w") as f:
                f.write(content)

patch_compress_inverse()
