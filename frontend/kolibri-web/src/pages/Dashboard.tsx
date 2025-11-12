export default function Dashboard() {
  const stats = [
    { label: 'Активных лицензий', value: '2', icon: '📋' },
    { label: 'Пользователей', value: '5', icon: '👥' },
    { label: 'Использовано', value: '5 GB', icon: '💾' },
    { label: 'До обновления', value: '30 дн', icon: '⏳' },
  ];

  const licenses = [
    {
      id: 'LIC-001',
      tier: 'Startup',
      status: 'active',
      expiry: '2026-11-12',
      users: '3/10',
    },
  ];

  return (
    <div className="p-8">
      <h1 className="text-3xl font-bold mb-8">Добро пожаловать!</h1>

      {/* Stats Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4 mb-8">
        {stats.map(stat => (
          <div key={stat.label} className="bg-gray-800 rounded-lg p-6 border-l-4 border-cyan-500">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-gray-400 text-sm">{stat.label}</p>
                <p className="text-3xl font-bold text-white mt-2">{stat.value}</p>
              </div>
              <span className="text-4xl">{stat.icon}</span>
            </div>
          </div>
        ))}
      </div>

      {/* Licenses */}
      <div>
        <h2 className="text-xl font-bold mb-4">Мои лицензии</h2>
        <div className="space-y-4">
          {licenses.map(license => (
            <div key={license.id} className="bg-gray-800 rounded-lg p-6">
              <div className="flex justify-between items-start">
                <div>
                  <p className="font-semibold text-cyan-400">{license.id}</p>
                  <p className="text-gray-400">{license.tier} План</p>
                </div>
                <span className="px-3 py-1 bg-green-900 text-green-100 rounded-full text-sm">
                  ● Активна
                </span>
              </div>
              <div className="mt-4 grid grid-cols-3 gap-4 text-sm">
                <div>
                  <p className="text-gray-400">Пользователи</p>
                  <p className="font-semibold">{license.users}</p>
                </div>
                <div>
                  <p className="text-gray-400">Истекает</p>
                  <p className="font-semibold">{license.expiry}</p>
                </div>
                <button className="col-span-3 px-4 py-2 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition">
                  Подробнее
                </button>
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
