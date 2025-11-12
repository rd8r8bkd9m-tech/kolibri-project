import { useParams } from 'react-router-dom';

export default function LicenseDetail() {
  const { id } = useParams();

  const license = {
    id,
    name: 'Professional Plus',
    tier: 'Professional',
    status: 'Active',
    users: 45,
    maxUsers: 100,
    storage: '8.5 TB',
    maxStorage: '10 TB',
    purchased: '12.01.2024',
    expiry: '12.01.2025',
    daysLeft: 245,
    price: '$15,000/year',
    features: [
      'До 100 пользователей',
      'До 10 TB хранилища',
      'Приоритетная поддержка 24/7',
      'API доступ',
      'Расширенная аналитика',
      'Кастомная интеграция',
      'Резервные копии',
      'SSO аутентификация',
    ],
  };

  return (
    <div className="p-8">
      <div className="flex items-center justify-between mb-8">
        <h1 className="text-3xl font-bold">{license.name}</h1>
        <span className="px-3 py-1 bg-green-900 bg-opacity-50 border border-green-600 text-green-300 text-sm rounded-full">
          {license.status}
        </span>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-6 mb-8">
        {/* License Info */}
        <div className="bg-gray-800 rounded-lg p-6 md:col-span-2">
          <h3 className="text-lg font-bold mb-4">Информация о лицензии</h3>
          <div className="grid grid-cols-2 gap-4">
            <div>
              <p className="text-gray-400 text-sm">Тип</p>
              <p className="font-semibold">{license.tier}</p>
            </div>
            <div>
              <p className="text-gray-400 text-sm">Цена</p>
              <p className="font-semibold">{license.price}</p>
            </div>
            <div>
              <p className="text-gray-400 text-sm">Куплена</p>
              <p className="font-semibold">{license.purchased}</p>
            </div>
            <div>
              <p className="text-gray-400 text-sm">Истекает</p>
              <p className="font-semibold">{license.expiry}</p>
            </div>
          </div>
        </div>

        {/* Expiry Card */}
        <div className="bg-gradient-to-br from-cyan-600 to-blue-600 rounded-lg p-6 text-white">
          <p className="text-sm opacity-90">Дней до истечения</p>
          <p className="text-4xl font-bold">{license.daysLeft}</p>
          <p className="text-sm mt-2 opacity-90">Обновление доступно</p>
        </div>
      </div>

      {/* Usage */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6 mb-8">
        <div className="bg-gray-800 rounded-lg p-6">
          <h3 className="text-lg font-bold mb-4">Пользователи</h3>
          <div className="mb-4">
            <div className="flex justify-between mb-2">
              <span className="text-sm text-gray-400">{license.users} из {license.maxUsers}</span>
              <span className="text-sm font-semibold">{Math.round((license.users / license.maxUsers) * 100)}%</span>
            </div>
            <div className="w-full bg-gray-700 rounded-full h-2">
              <div
                className="bg-cyan-500 h-2 rounded-full"
                style={{ width: `${(license.users / license.maxUsers) * 100}%` }}
              />
            </div>
          </div>
          <button className="w-full px-4 py-2 border border-cyan-500 text-cyan-400 hover:bg-cyan-500 hover:text-black font-semibold rounded-lg transition">
            Управлять пользователями
          </button>
        </div>

        <div className="bg-gray-800 rounded-lg p-6">
          <h3 className="text-lg font-bold mb-4">Хранилище</h3>
          <div className="mb-4">
            <div className="flex justify-between mb-2">
              <span className="text-sm text-gray-400">{license.storage} из {license.maxStorage}</span>
              <span className="text-sm font-semibold">{Math.round((8.5 / 10) * 100)}%</span>
            </div>
            <div className="w-full bg-gray-700 rounded-full h-2">
              <div
                className="bg-orange-500 h-2 rounded-full"
                style={{ width: '85%' }}
              />
            </div>
          </div>
          <button className="w-full px-4 py-2 border border-cyan-500 text-cyan-400 hover:bg-cyan-500 hover:text-black font-semibold rounded-lg transition">
            Управлять хранилищем
          </button>
        </div>
      </div>

      {/* Features */}
      <div className="bg-gray-800 rounded-lg p-6 mb-8">
        <h3 className="text-lg font-bold mb-4">Включённые функции</h3>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          {license.features.map((feature, idx) => (
            <div key={idx} className="flex items-center">
              <div className="w-5 h-5 rounded-full bg-green-500 flex items-center justify-center mr-3">
                <span className="text-white text-xs font-bold">✓</span>
              </div>
              <span>{feature}</span>
            </div>
          ))}
        </div>
      </div>

      {/* Actions */}
      <div className="flex gap-4 max-w-2xl">
        <button className="flex-1 px-4 py-2 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition">
          Продлить лицензию
        </button>
        <button className="flex-1 px-4 py-2 border border-gray-600 hover:bg-gray-700 text-white font-semibold rounded-lg transition">
          Скачать счёт
        </button>
        <button className="flex-1 px-4 py-2 border border-red-600 text-red-400 hover:bg-red-600 hover:text-white font-semibold rounded-lg transition">
          Отменить
        </button>
      </div>
    </div>
  );
}
