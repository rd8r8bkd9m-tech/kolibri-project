export default function Licenses() {
  const licenses = [
    {
      id: 'LIC-001',
      tier: 'Startup',
      status: 'active',
      expiry: '2026-11-12',
      users: '3/10',
      storage: '20 GB',
    },
    {
      id: 'LIC-002',
      tier: 'Business',
      status: 'expiring',
      expiry: '2025-11-25',
      users: '45/50',
      storage: '100 GB',
    },
  ];

  return (
    <div className="p-8">
      <div className="flex justify-between items-center mb-8">
        <h1 className="text-3xl font-bold">Мои лицензии</h1>
        <button className="px-4 py-2 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition">
          + Добавить
        </button>
      </div>

      <div className="space-y-4">
        {licenses.map(license => (
          <div key={license.id} className="bg-gray-800 rounded-lg p-6 hover:bg-gray-750 transition cursor-pointer">
            <div className="flex justify-between items-start mb-4">
              <div>
                <p className="font-semibold text-cyan-400 text-lg">{license.id}</p>
                <p className="text-gray-400">{license.tier} План</p>
              </div>
              <span
                className={`px-3 py-1 rounded-full text-sm font-semibold ${
                  license.status === 'active'
                    ? 'bg-green-900 text-green-100'
                    : 'bg-yellow-900 text-yellow-100'
                }`}
              >
                {license.status === 'active' ? '● Активна' : '⚠ Истекает'}
              </span>
            </div>

            <div className="grid grid-cols-2 md:grid-cols-4 gap-4 text-sm">
              <div>
                <p className="text-gray-400">Пользователи</p>
                <p className="font-semibold">{license.users}</p>
                <div className="h-2 bg-gray-700 rounded mt-1">
                  <div className="h-2 bg-cyan-500 rounded" style={{ width: '30%' }} />
                </div>
              </div>
              <div>
                <p className="text-gray-400">Хранилище</p>
                <p className="font-semibold">{license.storage}</p>
              </div>
              <div>
                <p className="text-gray-400">Истекает</p>
                <p className="font-semibold">{license.expiry}</p>
              </div>
              <div>
                <button className="w-full px-3 py-1 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded transition text-sm">
                  Подробнее
                </button>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
