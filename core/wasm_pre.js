// Этот файл выполняется до загрузки основного WASM-модуля.
// Здесь можно определить глобальные переменные или функции,
// которые будут доступны из C-кода.

var Module = typeof Module !== 'undefined' ? Module : {};

Module.onRuntimeInitialized = function() {
  console.log('Kolibri WASM Runtime Initialized.');
};
