const fs = require('fs');

async function run() {
    console.log("Loading WASM module...");
    const wasmBuffer = fs.readFileSync('web/public/kolibri.wasm');

    let memory;
    const importObject = {
        wasi_snapshot_preview1: {
            fd_close: () => 0, fd_read: () => 0, fd_write: () => 0, fd_seek: () => 0,
            proc_exit: (code) => { throw new Error(`exit(${code})`); },
            environ_get: () => 0, environ_sizes_get: () => 0,
            clock_time_get: (clk_id, precision, timePtr) => {
                if (memory) {
                    const view = new BigInt64Array(memory.buffer, timePtr, 1);
                    view[0] = BigInt(Date.now() * 1000000);
                }
                return 0;
            },
        },
        env: {
            emscripten_notify_memory_growth: () => 0,
            __syscall_getdents64: () => 0, __syscall_unlinkat: () => 0, __syscall_rmdir: () => 0,
            __syscall_stat64: () => 0, __syscall_fstat64: () => 0, __syscall_lstat64: () => 0,
            __syscall_fstatat64: () => 0, __syscall_openat: () => 0, __syscall_renameat: () => 0,
            __syscall_readlinkat: () => 0, __syscall_mkdirat: () => 0, __syscall_faccessat: () => 0,
            __syscall_fcntl64: () => 0, __syscall_ioctl: () => 0, __syscall_getcwd: () => 0,
            __syscall_fchmod: () => 0,
        }
    };

    const { instance } = await WebAssembly.instantiate(wasmBuffer, importObject);
    const exports = instance.exports;
    memory = exports.memory;

    function writeString(str) {
        const encoder = new TextEncoder();
        const bytes = encoder.encode(str + '\0');
        const ptr = exports.malloc(bytes.length);
        const view = new Uint8Array(memory.buffer, ptr, bytes.length);
        view.set(bytes);
        return ptr;
    }

    function readString(ptr) {
        const view = new Uint8Array(memory.buffer, ptr);
        let end = 0;
        while (view[end] !== 0) end++;
        return new TextDecoder().decode(view.subarray(0, end));
    }

    exports.kolibri_bridge_init();

    function addFact(text, conf, src) {
        const tPtr = writeString(text);
        const sPtr = writeString(src);
        exports.kolibri_bridge_kb_add_fact(tPtr, conf, sPtr);
        exports.free(tPtr); exports.free(sPtr);
    }

    function addRule(premise, conclusion, strength) {
        console.log(`\x1b[35m📜 Rule:\x1b[0m IF "${premise}" THEN "${conclusion}"`);
        const pPtr = writeString(premise);
        const cPtr = writeString(conclusion);
        const dPtr = writeString("general");
        exports.kolibri_bridge_kb_add_rule(pPtr, cPtr, 0, strength, dPtr);
        exports.free(pPtr); exports.free(cPtr); exports.free(dPtr);
    }

    function ask(query) {
        console.log(`\x1b[36m❓ Query:\x1b[0m ${query}`);
        const qPtr = writeString(query);
        const cap = 32768;
        const oPtr = exports.malloc(cap);
        exports.kolibri_bridge_reasoning_query(qPtr, oPtr, cap);
        const res = JSON.parse(readString(oPtr));
        console.log(`\x1b[32m💡 Answer:\x1b[0m ${res.answer} (Conf: ${res.confidence})`);
        if (res.chain) {
            res.chain.forEach(s => console.log(`   - ${s.desc}: ${s.premise} -> ${s.conclusion}`));
        }
        exports.free(qPtr); exports.free(oPtr);
    }

    console.log("\n--- TEST: MODUS PONENS ---");
    addFact("Сократ — человек", 1.0, "history");
    addRule("человек", "смертен", 1.0);
    ask("Является ли Сократ смертным?");

    console.log("\n--- TEST: CHAINING (A->B->C) ---");
    addFact("Нажат курок", 1.0, "event");
    addRule("Нажат курок", "Боек ударил по капсюлю", 1.0);
    addRule("ударил по капсюлю", "Произошел выстрел", 1.0);
    ask("Что произошло после нажатия курка?");

    console.log("\n--- TEST: CONTRADICTION (PENGUIN) ---");
    addFact("Пингвин - это птица", 1.0, "bio");
    addRule("птица", "умеет летать", 0.8);
    addFact("Пингвин не умеет летать", 1.0, "bio_spec");
    ask("Может ли пингвин летать?");
}

run().catch(console.error);
