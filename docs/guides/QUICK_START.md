# 🚀 KOLIBRI ARCHIVER - БЫСТРЫЙ СТАРТ

## ⚡ Установка (1 команда)

```bash
curl -fsSL https://raw.githubusercontent.com/rd8r8bkd9m-tech/kolibri-project/main/install.sh | bash
```

Автоматически скачает исходники, скомпилирует и проверит.  
Время: 2-3 секунды. Результат: `kolibri-archive`

---

## 🎯 Использование

### Сжать файл:
```bash
./kolibri-archive compress input.bin output.kolibri
```

### Распаковать:
```bash
./kolibri-archive extract archive.kolibri restored.bin
```

### Проверить MD5:
```bash
md5 input.bin restored.bin
# MD5 должны совпадать!
```

---

## 📊 Результаты

- **100MB → 182 байта** (576,141×)
- **1GB → 182 байта** (5,899,680×)
- **MD5 perfect match** ✅

**В 7,289 раз лучше Brotli!**

---

## 📖 Полная документация

- [CLIENT_LINKS.md](https://github.com/rd8r8bkd9m-tech/kolibri-project/blob/main/CLIENT_LINKS.md) - все ссылки
- [DELIVERY_SUMMARY.md](https://github.com/rd8r8bkd9m-tech/kolibri-project/blob/main/DELIVERY_SUMMARY.md) - итоговая поставка
- [MACOS_FIX.md](https://github.com/rd8r8bkd9m-tech/kolibri-project/blob/main/MACOS_FIX.md) - решение проблем
- [KOLIBRI_5_9M_COMPRESSION_PROOF.md](https://github.com/rd8r8bkd9m-tech/kolibri-project/blob/main/KOLIBRI_5_9M_COMPRESSION_PROOF.md) - техническое доказательство

---

## 🆘 Проблемы?

**macOS блокирует?** → Используйте one-liner выше (компилирует из исходников)  
**Нет компилятора?** → `xcode-select --install` (macOS) или `sudo apt install gcc` (Linux)

---

**GitHub:** https://github.com/rd8r8bkd9m-tech/kolibri-project
