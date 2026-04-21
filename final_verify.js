const fs = require('fs');
const createModule = require('./web/public/kolibri_engine.js');

async function runTest() {
    console.log("🧪 ТЕСТ: Загрузка WASM и проверка Number-Thinking...");

    const Module = await createModule();
    Module._kolibri_bridge_init();

    // Загружаем реальный файл знаний
    const knowledge = JSON.parse(fs.readFileSync('./web/public/knowledge.json', 'utf8'));
    console.log(`📥 Загружено ${knowledge.length} записей из проекта.`);

    // Инжектируем первые 50 записей для теста
    for (const item of knowledge.slice(0, 50)) {
        Module.ccall('kolibri_mem_store', null, ['string', 'string', 'number'], [item.premise, item.conclusion, 1.0]);
    }

    const query = "что такое колибри";
    console.log(`\n❓ Вопрос: "${query}"`);

    const capacity = 8192;
    const qPtr = Module._malloc(query.length * 4 + 1);
    Module.stringToUTF8(query, qPtr, query.length * 4 + 1);
    const outPtr = Module._malloc(capacity);

    Module._kolibri_bridge_query_json(qPtr, outPtr, capacity);
    const resultJson = Module.UTF8ToString(outPtr);
    const result = JSON.parse(resultJson);

    console.log("\n✨ ОТВЕТ ЯДРА:");
    console.log("--------------------------------------------------");
    console.log("Response:", result.response);
    console.log("Thinking (Stream):", result.thinking);
    console.log("Duration:", result.duration_ms, "ms");
    console.log("--------------------------------------------------");

    if (result.response && result.thinking) {
        console.log("\n✅ ВЕРИФИКАЦИЯ УСПЕШНА: Ядро думает и отвечает.");
    } else {
        console.log("\n❌ ОШИБКА: Пустой ответ или отсутствие потока цифр.");
    }

    Module._free(qPtr);
    Module._free(outPtr);
}

runTest().catch(console.error);
