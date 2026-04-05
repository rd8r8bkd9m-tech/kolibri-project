set(EMCC_LINK_FLAGS
    "-sEXPORT_ALL=1"
    "-sEXPORTED_FUNCTIONS=['_kolibri_sim_wasm_init','_kolibri_sim_wasm_tick','_kolibri_sim_wasm_get_logs','_kolibri_sim_wasm_reset','_kolibri_sim_wasm_free']"
    "-sALLOW_MEMORY_GROWTH=1"
)

add_executable(kolibri_sim_wasm wasm/kolibri_sim_wasm.c)
set_target_properties(kolibri_sim_wasm PROPERTIES LINK_FLAGS "${EMCC_LINK_FLAGS}")
target_link_libraries(kolibri_sim_wasm PRIVATE kolibri_core)

# === Kolibri Core WASM — полный экспорт C-ядра ===
set(EMCC_LINK_FLAGS_CORE
    "-sEXPORT_ALL=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sERROR_ON_UNDEFINED_SYMBOLS=0"
)

add_executable(kolibri_core_wasm wasm/kolibri_core_wasm.c)
set_target_properties(kolibri_core_wasm PROPERTIES LINK_FLAGS "${EMCC_LINK_FLAGS_CORE}")
target_link_libraries(kolibri_core_wasm PRIVATE kolibri_core)
