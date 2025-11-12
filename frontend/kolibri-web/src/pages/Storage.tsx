import React from 'react';
import { StorageManager } from '../components/StorageManager';

export const Storage: React.FC = () => {
  return (
    <div className="space-y-8">
      <div>
        <h1 className="text-4xl font-bold text-gray-900 dark:text-white mb-2">
          ☁️ Колибри Облачное Хранилище
        </h1>
        <p className="text-lg text-gray-600 dark:text-gray-400">
          Загружайте и управляйте файлами с автоматическим сжатием Колибри DECIMAL10X
        </p>
      </div>

      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-6">
        <h3 className="font-semibold text-blue-900 dark:text-blue-400 mb-2">ℹ️ О системе</h3>
        <ul className="text-sm text-blue-800 dark:text-blue-300 space-y-1">
          <li>✓ Автоматическое сжатие файлов алгоритмом DECIMAL10X</li>
          <li>✓ Анализ паттернов и формульное кодирование</li>
          <li>✓ Шифрование каналов связи и защита данных</li>
          <li>✓ Отслеживание статистики сжатия для каждого файла</li>
          <li>✓ Максимум 10 ГБ на пользователя</li>
        </ul>
      </div>

      <StorageManager />
    </div>
  );
};
