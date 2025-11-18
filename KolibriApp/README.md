# Kolibri macOS App

Красивое нативное приложение для macOS с GPU acceleration.

## Создание проекта в Xcode:

1. Откройте Xcode
2. File → New → Project
3. Выберите: macOS → App
4. Название: KolibriApp
5. Interface: SwiftUI
6. Language: Swift

## Установка кода:

1. Замените содержимое `ContentView.swift` на код из `KolibriApp.swift`
2. Добавьте в Info.plist:
   ```xml
   <key>NSDocumentsFolderUsageDescription</key>
   <string>Kolibri нужен доступ к файлам для сжатия</string>
   ```

## Запуск:

1. Нажмите ⌘R в Xcode
2. Приложение откроется
3. Перетащите файл для сжатия

## Функции:

- ✅ Drag & Drop файлов
- ✅ Красивый UI с градиентами
- ✅ Прогресс в реальном времени
- ✅ Dark/Light theme
- ✅ Статистика сжатия
- ✅ Сохранение результата

## Скриншоты:

Фиолетовый градиент с белым текстом, три статистики сверху (46.7B chars/sec, 165× faster, files processed), drop zone в центре, результаты внизу.

## Альтернатива: Запуск через Swift Playgrounds

Можно также использовать Swift Playgrounds на iPad/Mac для быстрого тестирования интерфейса.
