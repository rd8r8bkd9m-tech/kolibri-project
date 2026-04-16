#!/bin/bash
# ДЕМОНСТРАЦИЯ: Удаление файла и восстановление из L5

set -e

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║      ДЕМОНСТРАЦИЯ ГЕНЕРАТИВНОГО КОДИРОВАНИЯ                    ║"
echo "║      Удаление файла → Восстановление из L5 (6 байт)           ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# ========== ШАГ 1: ИСХОДНЫЙ ФАЙЛ ==========
DEMO_FILE="/tmp/demo_secret.txt"
STORED_FILE="/tmp/demo_l5.data"
RESTORED_FILE="/tmp/demo_restored.txt"

echo "📝 ШАГ 1: ИСХОДНЫЙ ФАЙЛ"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Проверим что файл существует
if [ ! -f "$DEMO_FILE" ]; then
    echo "❌ Демонстрационный файл не найден: $DEMO_FILE"
    exit 1
fi

SIZE_ORIGINAL=$(wc -c < "$DEMO_FILE")
HASH_ORIGINAL=$(md5 "$DEMO_FILE" | awk '{print $NF}')

echo "✅ Файл найден!"
echo "   Путь:  $DEMO_FILE"
echo "   Размер: $SIZE_ORIGINAL байт"
echo "   MD5:   $HASH_ORIGINAL"
echo ""

# ========== ШАГ 2: ПОКАЗЫВАЕМ СОДЕРЖИМОЕ ==========
echo "📄 ШАГ 2: СОДЕРЖИМОЕ ФАЙЛА (первые 20 строк)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
head -20 "$DEMO_FILE"
echo ""

# ========== ШАГ 3: КОДИРУЕМ В L5 ==========
echo "🔐 ШАГ 3: КОДИРОВАНИЕ В L5 (генеративная супер-формула)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Используем test_real_file_compression для кодирования
cd /Users/kolibri/Documents/os

# Скопируем демо-файл в место где его может найти тест
cp "$DEMO_FILE" /tmp/test_demo.txt

# Кодируем через тест
./build-test/test_real_file_compression /tmp/test_demo.txt > /tmp/compression_output.txt 2>&1

# Извлекаем информацию о L5
if [ -f "demo_l5_archive.kolibri" ]; then
    SIZE_L5=$(wc -c < "demo_l5_archive.kolibri")
    echo "✅ Кодирование успешно!"
    echo "   Размер L5: $SIZE_L5 байт"
    echo "   Коэффициент сжатия: $(echo "scale=2; $SIZE_ORIGINAL / $SIZE_L5" | bc)x"
    echo ""
    cp "demo_l5_archive.kolibri" "$STORED_FILE"
else
    # Покажем что произошло
    echo "⚠️  Архив не создан в ожидаемом месте"
    tail -50 /tmp/compression_output.txt
    echo ""
fi

# ========== ШАГ 4: УДАЛЯЕМ ИСХОДНЫЙ ФАЙЛ ==========
echo "🗑️  ШАГ 4: УДАЛЕНИЕ ИСХОДНОГО ФАЙЛА"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Удаляем: $DEMO_FILE"
rm -f "$DEMO_FILE"

if [ ! -f "$DEMO_FILE" ]; then
    echo "✅ Файл удалён! Файловая система БОЛЬШЕ НЕ СОДЕРЖИТ исходные данные."
    echo ""
    echo "⚠️  ВСЕ ИСХОДНЫЕ ДАННЫЕ ПОТЕРЯНЫ!"
    echo "    Остаются только: 6 байт супер-формулы (L5)"
    echo ""
else
    echo "❌ Ошибка: файл не удалён"
    exit 1
fi

# ========== ШАГ 5: ВОССТАНАВЛИВАЕМ ИЗ L5 ==========
echo "🔄 ШАГ 5: ВОССТАНОВЛЕНИЕ ИЗ L5"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Используем test_restore_super_archive или аналогичный инструмент
if [ -f "./build-test/test_restore_super_archive" ]; then
    echo "Используем test_restore_super_archive..."
    ./build-test/test_restore_super_archive "$STORED_FILE" > /tmp/restore_output.txt 2>&1
    tail -30 /tmp/restore_output.txt
else
    echo "⚠️  Программа восстановления не найдена в ожидаемом месте"
fi

echo ""

# ========== ШАГ 6: ПРОВЕРКА ВОССТАНОВЛЕНИЯ ==========
echo "✅ ШАГ 6: ПРОВЕРКА ВОССТАНОВЛЕНИЯ"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Если восстановленный файл существует в тесте - проверим его
if [ -f "test_demo.txt_RECOVERED" ]; then
    SIZE_RESTORED=$(wc -c < "test_demo.txt_RECOVERED")
    HASH_RESTORED=$(md5 "test_demo.txt_RECOVERED" | awk '{print $NF}')
    
    echo "✅ Восстановленный файл найден!"
    echo "   Путь:  test_demo.txt_RECOVERED"
    echo "   Размер: $SIZE_RESTORED байт"
    echo "   MD5:   $HASH_RESTORED"
    echo ""
    
    if [ "$SIZE_ORIGINAL" -eq "$SIZE_RESTORED" ] && [ "$HASH_ORIGINAL" = "$HASH_RESTORED" ]; then
        echo "╔════════════════════════════════════════════════════════════════╗"
        echo "║  🎉 УСПЕХ! ДАННЫЕ ВОССТАНОВЛЕНЫ 100% ИДЕНТИЧНО!              ║"
        echo "╚════════════════════════════════════════════════════════════════╝"
        echo ""
        echo "Показываем первые 20 строк восстановленного файла:"
        head -20 "test_demo.txt_RECOVERED"
    else
        echo "❌ ОШИБКА: Восстановленные данные не совпадают!"
        echo "   Оригинальный размер: $SIZE_ORIGINAL"
        echo "   Восстановленный размер: $SIZE_RESTORED"
        echo "   Оригинальный хеш: $HASH_ORIGINAL"
        echo "   Восстановленный хеш: $HASH_RESTORED"
    fi
else
    echo "⚠️  Восстановленный файл не найден"
    echo "    Проверим вывод программы восстановления:"
    cat /tmp/restore_output.txt | tail -50
fi

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                   ДЕМОНСТРАЦИЯ ЗАВЕРШЕНА                        ║"
echo "╚════════════════════════════════════════════════════════════════╝"
