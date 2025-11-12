import { Outlet, Link, useNavigate } from 'react-router-dom';
import { FiMenu, FiX, FiLogOut } from 'react-icons/fi';
import { useState } from 'react';

export default function Layout() {
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const navigate = useNavigate();

  const handleLogout = () => {
    localStorage.removeItem('auth_token');
    navigate('/login');
  };

  const navItems = [
    { path: '/', label: 'Главная', icon: '🏠' },
    { path: '/licenses', label: 'Лицензии', icon: '📋' },
    { path: '/payments', label: 'Платежи', icon: '💳' },
    { path: '/storage', label: 'Хранилище', icon: '☁️' },
    { path: '/profile', label: 'Профиль', icon: '👤' },
    { path: '/settings', label: 'Настройки', icon: '⚙️' },
  ];

  return (
    <div className="flex h-screen bg-gray-900 text-white">
      {/* Mobile Menu Button */}
      <div className="md:hidden fixed top-0 left-0 right-0 bg-gray-800 p-4 flex justify-between items-center z-40">
        <div className="text-xl font-bold text-cyan-400">Kolibri</div>
        <button onClick={() => setSidebarOpen(!sidebarOpen)}>
          {sidebarOpen ? <FiX size={24} /> : <FiMenu size={24} />}
        </button>
      </div>

      {/* Sidebar */}
      <aside
        className={`
          fixed md:relative w-64 bg-gray-800 h-full transition-all duration-300
          ${sidebarOpen ? 'left-0' : '-left-full md:left-0'}
          z-30 md:z-0
        `}
      >
        <div className="p-6 border-b border-gray-700 mt-16 md:mt-0">
          <h1 className="text-2xl font-bold text-cyan-400">Kolibri</h1>
          <p className="text-xs text-gray-400 mt-1">License Manager</p>
        </div>

        <nav className="p-4 space-y-2">
          {navItems.map(item => (
            <Link
              key={item.path}
              to={item.path}
              onClick={() => setSidebarOpen(false)}
              className="flex items-center space-x-3 px-4 py-2 rounded-lg hover:bg-gray-700 transition text-gray-300 hover:text-white"
            >
              <span className="text-xl">{item.icon}</span>
              <span>{item.label}</span>
            </Link>
          ))}
        </nav>

        <div className="absolute bottom-6 left-4 right-4">
          <button
            onClick={handleLogout}
            className="w-full flex items-center space-x-3 px-4 py-2 rounded-lg bg-red-900 hover:bg-red-800 transition text-red-100"
          >
            <FiLogOut />
            <span>Выход</span>
          </button>
        </div>
      </aside>

      {/* Main Content */}
      <main className="flex-1 overflow-auto pt-16 md:pt-0">
        <Outlet />
      </main>
    </div>
  );
}
