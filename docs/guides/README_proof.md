# Kolibri 1GB Proof Package

## Содержимое

- `orig.tar.gz` — tar.gz с оригинальным файлом test_1gb_perfect.bin
- `test_1gb_v3.kolibri` — Kolibri-архив (182 байта)
- `kolibri-archive-v3` — декомпрессор (executable)
- `verify_1gb.sh` — автоматический скрипт верификации
- `checksums.sha256` — контрольные суммы SHA256

## Проверка

### 1) Распаковать пакет
```bash
tar -xvf kolibri_1gb_proof_package.tar.gz
```

### 2) Запустить верификацию
```bash
chmod +x verify_1gb.sh
./verify_1gb.sh
```

### 3) Что должно быть видно

- MD5/SHA256 исходника и восстановленного совпадают
- `cmp` показывает bytewise match
- Коэффициент сжатия = orig_size / archive_size (≈5,899,680×)

## Ожидаемые результаты

```
=== VERIFY 1GB PROOF ===
Archive: ./test_1gb_v3.kolibri

1) sizes:
 Original: ./test_1gb_perfect.bin -> 1073741824 bytes
 Archive : ./test_1gb_v3.kolibri -> 182 bytes

2) extract...
Extraction finished.
 Recovered: ./test_1gb_recovered.bin -> 1073741824 bytes

3) checksums (MD5 & SHA256):
MD5 (./test_1gb_perfect.bin) = 90672a90fba312a3860b25b8861e8bd9
MD5 (./test_1gb_recovered.bin) = 90672a90fba312a3860b25b8861e8bd9

4) bytewise compare (cmp):
✅ BYTEWISE MATCH: recovered == original

5) compression ratio:
 Ratio (orig / archive): 5899680.000000 x

=== ✅ VERIFY DONE ===
```

## Примечания

- Если декомпрессор не включён в пакет, предоставьте инструкцию по сборке (CMake, версия компилятора)
- Зафиксируй среду (OS, compiler, commit hash) в `environment.txt` для полной воспроизводимости
- Алгоритм детерминированный, не использует случайность

## Компиляция декомпрессора из исходников

Если нужно собрать из исходников:

```bash
# Скачать исходник
curl -L -o kolibri_archiver_v3.c \
  https://github.com/rd8r8bkd9m-tech/kolibri-project/raw/main/tools/kolibri_archiver_v3.c

# Скомпилировать
gcc -O3 -o kolibri-archive-v3 kolibri_archiver_v3.c

# Проверить
./kolibri-archive-v3 --help
```

## Системные требования

- OS: macOS / Linux / Windows (WSL)
- RAM: ~2GB для файлов 1GB
- Компилятор: GCC 9+ или Clang 10+
- Время: ~3-4 секунды на сжатие, ~27 секунд на распаковку (1GB)
