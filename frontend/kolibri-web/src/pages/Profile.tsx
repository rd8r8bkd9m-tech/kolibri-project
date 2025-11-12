export default function Profile() {
  const user = {
    name: 'Иван Петров',
    email: 'ivan@company.com',
    company: 'Tech Company LLC',
    licenses: 2,
    spent: '$60,000',
    joined: '12.11.2024',
  };

  return (
    <div className="p-8">
      <h1 className="text-3xl font-bold mb-8">Мой профиль</h1>

      {/* Profile Card */}
      <div className="bg-gray-800 rounded-lg p-8 max-w-2xl mb-8">
        <div className="flex items-center mb-8">
          <div className="w-20 h-20 rounded-full bg-cyan-500 flex items-center justify-center text-2xl font-bold text-black">
            ИП
          </div>
          <div className="ml-6">
            <h2 className="text-2xl font-bold">{user.name}</h2>
            <p className="text-gray-400">{user.email}</p>
          </div>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          <div>
            <p className="text-gray-400 text-sm">Компания</p>
            <p className="font-semibold">{user.company}</p>
          </div>
          <div>
            <p className="text-gray-400 text-sm">Лицензий</p>
            <p className="font-semibold">{user.licenses}</p>
          </div>
          <div>
            <p className="text-gray-400 text-sm">Всего потрачено</p>
            <p className="font-semibold">{user.spent}</p>
          </div>
          <div>
            <p className="text-gray-400 text-sm">Присоединился</p>
            <p className="font-semibold">{user.joined}</p>
          </div>
        </div>

        <button className="mt-8 w-full px-4 py-2 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition">
          Редактировать профиль
        </button>
      </div>

      {/* Account Settings */}
      <div className="bg-gray-800 rounded-lg p-6 max-w-2xl">
        <h3 className="text-lg font-bold mb-4">Аккаунт</h3>
        <div className="space-y-3">
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>Смена пароля</span>
            <span className="text-gray-400">›</span>
          </button>
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>Двухфакторная аутентификация</span>
            <span className="text-gray-400">›</span>
          </button>
          <button className="w-full text-left px-4 py-3 hover:bg-gray-700 rounded-lg transition flex justify-between items-center">
            <span>История активности</span>
            <span className="text-gray-400">›</span>
          </button>
        </div>
      </div>
    </div>
  );
}
