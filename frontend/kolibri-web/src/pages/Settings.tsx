import { useState } from 'react';

export default function Settings() {
  const [settings, setSettings] = useState({
    notifications: true,
    emailDigest: true,
    darkMode: true,
    language: 'ru',
  });

  const handleToggle = (key: string) => {
    setSettings(prev => ({ ...prev, [key]: !prev[key] }));
  };

  return (
    <div className="p-8">
      <h1 className="text-3xl font-bold mb-8">Настройки</h1>

      {/* Notifications */}
      <div className="bg-gray-800 rounded-lg p-6 max-w-2xl mb-6">
        <h3 className="text-lg font-bold mb-4">Уведомления</h3>
        <div className="space-y-4">
          <div className="flex justify-between items-center">
            <div>
              <p className="font-medium">Уведомления в приложении</p>
              <p className="text-gray-400 text-sm">Получать уведомления о событиях</p>
            </div>
            <button
              onClick={() => handleToggle('notifications')}
              className={`w-12 h-6 rounded-full transition ${
                settings.notifications ? 'bg-cyan-500' : 'bg-gray-600'
              }`}
            >
              <div
                className={`w-5 h-5 rounded-full bg-white transition ${
                  settings.notifications ? 'translate-x-6' : 'translate-x-0.5'
                }`}
              />
            </button>
          </div>

          <div className="flex justify-between items-center">
            <div>
              <p className="font-medium">Еженедельная рассылка</p>
              <p className="text-gray-400 text-sm">Отчеты по использованию каждую неделю</p>
            </div>
            <button
              onClick={() => handleToggle('emailDigest')}
              className={`w-12 h-6 rounded-full transition ${
                settings.emailDigest ? 'bg-cyan-500' : 'bg-gray-600'
              }`}
            >
              <div
                className={`w-5 h-5 rounded-full bg-white transition ${
                  settings.emailDigest ? 'translate-x-6' : 'translate-x-0.5'
                }`}
              />
            </button>
          </div>
        </div>
      </div>

      {/* Appearance */}
      <div className="bg-gray-800 rounded-lg p-6 max-w-2xl mb-6">
        <h3 className="text-lg font-bold mb-4">Внешний вид</h3>
        <div className="space-y-4">
          <div className="flex justify-between items-center">
            <div>
              <p className="font-medium">Тёмная тема</p>
              <p className="text-gray-400 text-sm">Использовать тёмную тему по умолчанию</p>
            </div>
            <button
              onClick={() => handleToggle('darkMode')}
              className={`w-12 h-6 rounded-full transition ${
                settings.darkMode ? 'bg-cyan-500' : 'bg-gray-600'
              }`}
            >
              <div
                className={`w-5 h-5 rounded-full bg-white transition ${
                  settings.darkMode ? 'translate-x-6' : 'translate-x-0.5'
                }`}
              />
            </button>
          </div>

          <div>
            <p className="font-medium mb-2">Язык</p>
            <select
              value={settings.language}
              onChange={(e) => setSettings(prev => ({ ...prev, language: e.target.value }))}
              className="w-full px-4 py-2 bg-gray-700 text-white rounded-lg focus:outline-none focus:ring-2 focus:ring-cyan-500"
            >
              <option value="ru">Русский</option>
              <option value="en">English</option>
            </select>
          </div>
        </div>
      </div>

      {/* Security */}
      <div className="bg-gray-800 rounded-lg p-6 max-w-2xl mb-6">
        <h3 className="text-lg font-bold mb-4">Безопасность</h3>
        <div className="space-y-3">
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>Активные сеансы</span>
            <span className="text-gray-400">›</span>
          </button>
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>Авторизованные приложения</span>
            <span className="text-gray-400">›</span>
          </button>
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>API ключи</span>
            <span className="text-gray-400">›</span>
          </button>
        </div>
      </div>

      {/* Danger Zone */}
      <div className="bg-red-900 bg-opacity-30 border border-red-700 rounded-lg p-6 max-w-2xl">
        <h3 className="text-lg font-bold mb-4 text-red-400">Опасная зона</h3>
        <p className="text-gray-400 mb-4">Необратимые действия</p>
        <button className="w-full px-4 py-2 bg-red-600 hover:bg-red-700 text-white font-semibold rounded-lg transition">
          Удалить аккаунт
        </button>
      </div>
    </div>
  );
}
