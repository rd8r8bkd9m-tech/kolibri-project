# 🔧 Решение проблем с запуском на macOS

## Проблема: "не открывает" / "не запускается"

### Причина
macOS блокирует скачанные исполняемые файлы механизмом **Gatekeeper** (карантин).

---

## ✅ Решение 1: Снять карантин (быстро)

```bash
# Скачать
curl -L -o kolibri-archive https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri-archive-v3

# Снять карантин macOS
xattr -d com.apple.quarantine kolibri-archive 2>/dev/null || true

# Сделать исполняемым
chmod +x kolibri-archive

# Запустить
./kolibri-archive
```

---

## ✅ Решение 2: Скомпилировать самостоятельно (рекомендуется)

```bash
# Скачать исходник
curl -L -o kolibri_archiver_v3.c \
  https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c

# Скомпилировать
gcc -O3 -o kolibri-archive kolibri_archiver_v3.c

# Готово!
./kolibri-archive compress test.bin test.kolibri
```

**Преимущества компиляции:**
- ✅ Нет проблем с карантином macOS
- ✅ Оптимизация под вашу систему
- ✅ Можно проверить исходный код
- ✅ Работает на любой архитектуре (Intel/ARM)

---

## ✅ Решение 3: Разрешить через System Settings

Если при запуске появляется окно "cannot be opened because it is from an unidentified developer":

1. Открыть **System Settings** (Системные настройки)
2. Перейти в **Privacy & Security** (Конфиденциальность и безопасность)
3. Прокрутить вниз до раздела **Security**
4. Нажать **"Open Anyway"** (Открыть в любом случае)
5. Подтвердить действие

---

## 🧪 Проверка работоспособности

После любого из решений проверьте:

```bash
# Должна появиться справка
./kolibri-archive

# Создать тестовый файл
echo "AAAAAAAAAAAAAAAA" > test.txt

# Сжать
./kolibri-archive compress test.txt test.kolibri

# Проверить размер
ls -lh test.kolibri

# Распаковать
./kolibri-archive extract test.kolibri restored.txt

# Проверить MD5
md5 test.txt restored.txt
```

---

## ⚠️ Если ничего не помогло

### Intel Mac (x86_64)
```bash
# Скачать исходник и скомпилировать
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c
gcc -arch x86_64 -O3 -o kolibri-archive kolibri.c
```

### Apple Silicon (ARM64/M1/M2/M3)
```bash
# Скачать исходник и скомпилировать
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c
gcc -arch arm64 -O3 -o kolibri-archive kolibri.c
```

### Universal Binary (оба процессора)
```bash
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c
gcc -arch x86_64 -arch arm64 -O3 -o kolibri-archive kolibri.c
```

---

## 🐧 Linux

```bash
# Скачать исходник
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c

# Скомпилировать
gcc -O3 -o kolibri-archive kolibri.c

# Запустить
./kolibri-archive compress file.bin archive.kolibri
```

---

## 🪟 Windows (WSL/MinGW)

### WSL (Windows Subsystem for Linux)
```bash
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c
gcc -O3 -o kolibri-archive.exe kolibri.c
./kolibri-archive.exe compress file.bin archive.kolibri
```

### MinGW
```cmd
curl -L -o kolibri.c https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c
gcc -O3 -o kolibri-archive.exe kolibri.c
kolibri-archive.exe compress file.bin archive.kolibri
```

---

## 📋 Системные требования

- **Компилятор:** GCC 9+ или Clang 10+
- **Стандарт:** C99
- **Зависимости:** Нет (чистый C, только stdlib)
- **RAM:** ~2GB для файлов 1GB
- **Диск:** Размер исходного файла × 2 (временно)

---

## 💡 Рекомендация

**Всегда компилируйте из исходников** — это:
- Безопаснее (вы видите код)
- Надёжнее (нет проблем с карантином)
- Быстрее (оптимизация под вашу систему)
- Универсальнее (работает на любой платформе)

Компиляция занимает ~1 секунду:
```bash
time gcc -O3 -o kolibri kolibri_archiver_v3.c
# real    0m0.842s
```

---

## 🆘 Поддержка

Если проблемы остались, создайте **Issue** на GitHub:
```
https://github.com/rd8r8bkd9m-tech/kolibri-project/issues
```

Укажите:
- Версию macOS (`sw_vers`)
- Архитектуру процессора (`uname -m`)
- Текст ошибки
- Шаги, которые пробовали
