import React, { useState, useEffect } from 'react';
import { Cloud, LogOut, AlertCircle, CheckCircle } from 'lucide-react';
import { FileUpload } from './FileUpload';
import { FileList } from './FileList';
import api from '../services/api';

interface StorageInfo {
  username: string;
  storage: {
    used: number;
    limit: number;
  };
  filesCount: number;
  usagePercent: number;
  kolibri: {
    algorithm: string;
    totalOriginalSize: number;
    totalCompressedSize: number;
    totalSaved: number;
    averageCompressionRatio: number;
    compressionMessage: string;
  };
}

export const StorageManager: React.FC = () => {
  const [isAuthenticated, setIsAuthenticated] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [storageInfo, setStorageInfo] = useState<StorageInfo | null>(null);
  const [refreshTrigger, setRefreshTrigger] = useState(0);

  // Проверка аутентификации при загрузке
  useEffect(() => {
    const token = localStorage.getItem('storageToken');
    if (token) {
      loadStorageInfo();
    } else {
      setIsLoading(false);
    }
  }, []);

  const loadStorageInfo = async () => {
    try {
      const response = await api.getStorageInfo();
      setStorageInfo(response.data);
      setIsAuthenticated(true);
      setError('');
    } catch (err) {
      console.error('Error loading storage info:', err);
      localStorage.removeItem('storageToken');
      setIsAuthenticated(false);
    } finally {
      setIsLoading(false);
    }
  };

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setIsLoading(true);

    try {
      // Сначала регистрируем пользователя (если его нет)
      try {
        await api.storageRegister(username, password);
      } catch (err) {
        // Пользователь может уже существовать
      }

      // Затем логинимся
      await api.storageLogin(username, password);
      await loadStorageInfo();
      setUsername('');
      setPassword('');
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : 'Ошибка аутентификации';
      setError(errorMessage);
    } finally {
      setIsLoading(false);
    }
  };

  const handleLogout = () => {
    localStorage.removeItem('storageToken');
    setIsAuthenticated(false);
    setStorageInfo(null);
    setUsername('');
    setPassword('');
  };

  const formatSize = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  if (isLoading) {
    return (
      <div className="flex items-center justify-center p-8">
        <div className="animate-spin">
          <Cloud className="w-8 h-8 text-blue-500" />
        </div>
        <p className="ml-3 text-gray-600 dark:text-gray-400">Загрузка...</p>
      </div>
    );
  }

  if (!isAuthenticated) {
    return (
      <div className="max-w-md mx-auto p-6">
        <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-6">
          <div className="flex items-center gap-3 mb-6">
            <Cloud className="w-8 h-8 text-blue-600 dark:text-blue-400" />
            <h2 className="text-2xl font-bold text-gray-900 dark:text-white">Облачное хранилище</h2>
          </div>

          <form onSubmit={handleLogin} className="space-y-4">
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Имя пользователя
              </label>
              <input
                type="text"
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                placeholder="введите имя"
                required
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white placeholder-gray-500 dark:placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Пароль
              </label>
              <input
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                placeholder="введите пароль"
                required
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white placeholder-gray-500 dark:placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            {error && (
              <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-3 flex items-center gap-2">
                <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
                <p className="text-red-700 dark:text-red-400 text-sm">{error}</p>
              </div>
            )}

            <button
              type="submit"
              disabled={isLoading}
              className="w-full px-4 py-2 bg-blue-600 hover:bg-blue-700 dark:bg-blue-500 dark:hover:bg-blue-600 text-white font-medium rounded-lg transition-colors disabled:opacity-50"
            >
              {isLoading ? 'Загрузка...' : 'Войти'}
            </button>
          </form>

          <p className="mt-4 text-sm text-gray-600 dark:text-gray-400 text-center">
            💡 Используйте любое имя и пароль - аккаунт создастся автоматически
          </p>
        </div>
      </div>
    );
  }

  if (!storageInfo) {
    return null;
  }

  return (
    <div className="space-y-6">
      {/* Заголовок с информацией пользователя */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Cloud className="w-8 h-8 text-blue-600 dark:text-blue-400" />
          <div>
            <h2 className="text-2xl font-bold text-gray-900 dark:text-white">
              Облачное хранилище
            </h2>
            <p className="text-sm text-gray-600 dark:text-gray-400">
              Пользователь: {storageInfo.username}
            </p>
          </div>
        </div>

        <button
          onClick={handleLogout}
          className="flex items-center gap-2 px-4 py-2 text-gray-700 dark:text-gray-300 hover:text-red-600 dark:hover:text-red-400 transition-colors"
        >
          <LogOut className="w-5 h-5" />
          <span>Выход</span>
        </button>
      </div>

      {/* Статистика сжатия Колибри */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="bg-gradient-to-br from-blue-50 to-blue-100 dark:from-blue-900/20 dark:to-blue-800/20 rounded-lg p-4 border border-blue-200 dark:border-blue-700">
          <div className="flex items-center gap-2 mb-2">
            <CheckCircle className="w-5 h-5 text-blue-600 dark:text-blue-400" />
            <h3 className="font-semibold text-gray-900 dark:text-white">Алгоритм сжатия</h3>
          </div>
          <p className="text-sm text-gray-700 dark:text-gray-300">
            {storageInfo.kolibri.algorithm}
          </p>
        </div>

        <div className="bg-gradient-to-br from-green-50 to-green-100 dark:from-green-900/20 dark:to-green-800/20 rounded-lg p-4 border border-green-200 dark:border-green-700">
          <div className="flex items-center gap-2 mb-2">
            <span className="text-lg">💾</span>
            <h3 className="font-semibold text-gray-900 dark:text-white">Сохранено</h3>
          </div>
          <p className="text-sm text-gray-700 dark:text-gray-300">
            {storageInfo.kolibri.compressionMessage}
          </p>
        </div>
      </div>

      {/* Использование хранилища */}
      <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-6">
        <h3 className="font-semibold text-lg text-gray-900 dark:text-white mb-4">
          Использование хранилища
        </h3>

        <div className="space-y-3">
          <div>
            <div className="flex justify-between mb-2">
              <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
                Использовано
              </span>
              <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
                {formatSize(storageInfo.storage.used)} / {formatSize(storageInfo.storage.limit)}
              </span>
            </div>
            <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-3 overflow-hidden">
              <div
                className="bg-gradient-to-r from-blue-500 to-blue-600 h-full rounded-full transition-all"
                style={{ width: `${storageInfo.usagePercent}%` }}
              />
            </div>
            <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
              {storageInfo.usagePercent}% использовано
            </p>
          </div>

          <div className="grid grid-cols-3 gap-4 pt-2 border-t border-gray-200 dark:border-gray-700">
            <div>
              <p className="text-xs text-gray-600 dark:text-gray-400">Файлов</p>
              <p className="text-lg font-bold text-gray-900 dark:text-white">
                {storageInfo.filesCount}
              </p>
            </div>
            <div>
              <p className="text-xs text-gray-600 dark:text-gray-400">Оригинал</p>
              <p className="text-lg font-bold text-gray-900 dark:text-white">
                {formatSize(storageInfo.kolibri.totalOriginalSize)}
              </p>
            </div>
            <div>
              <p className="text-xs text-gray-600 dark:text-gray-400">Сжато</p>
              <p className="text-lg font-bold text-gray-900 dark:text-white">
                {formatSize(storageInfo.kolibri.totalCompressedSize)}
              </p>
            </div>
          </div>

          <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
            <p className="text-xs text-gray-600 dark:text-gray-400">Среднее сжатие</p>
            <p className="text-lg font-bold text-green-600 dark:text-green-400">
              {storageInfo.kolibri.averageCompressionRatio}%
            </p>
          </div>
        </div>
      </div>

      {/* Загрузка файла */}
      <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-6">
        <h3 className="font-semibold text-lg text-gray-900 dark:text-white mb-4">
          📤 Загрузить файл
        </h3>
        <FileUpload
          onSuccess={() => {
            setRefreshTrigger(r => r + 1);
            loadStorageInfo();
          }}
          onError={(error) => setError(error)}
        />
      </div>

      {/* Список файлов */}
      <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-6">
        <h3 className="font-semibold text-lg text-gray-900 dark:text-white mb-4">
          📋 Мои файлы ({storageInfo.filesCount})
        </h3>
        <FileList
          refreshTrigger={refreshTrigger}
          onFileDeleted={() => {
            setRefreshTrigger(r => r + 1);
            loadStorageInfo();
          }}
        />
      </div>
    </div>
  );
};
