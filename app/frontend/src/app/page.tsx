import Link from 'next/link'
import { Calculator, FileText, Database, Cog } from 'lucide-react'

export default function HomePage() {
  return (
    <div className="min-h-screen bg-gradient-to-br from-blue-50 to-indigo-100">
      <header className="bg-white shadow-sm">
        <div className="max-w-7xl mx-auto px-4 py-4 sm:px-6 lg:px-8">
          <h1 className="text-3xl font-bold text-gray-900">Kolibri Smeta</h1>
          <p className="text-sm text-gray-600">Расчет смет ФЕР/ГЭСН/ТЕР с AI</p>
        </div>
      </header>

      <main className="max-w-7xl mx-auto px-4 py-12 sm:px-6 lg:px-8">
        <div className="text-center mb-12">
          <h2 className="text-4xl font-bold text-gray-900 mb-4">
            Мультиплатформенное приложение для расчета смет
          </h2>
          <p className="text-xl text-gray-600">
            Автоматический расчет по нормам ФЕР, ГЭСН, ТЕР с поддержкой AI
          </p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-6 mb-12">
          <Link
            href="/smeta/new"
            className="block p-6 bg-white rounded-lg shadow-md hover:shadow-lg transition-shadow"
          >
            <div className="flex items-center mb-4">
              <FileText className="w-8 h-8 text-blue-600 mr-3" />
              <h3 className="text-xl font-semibold">Создать смету</h3>
            </div>
            <p className="text-gray-600">
              Создайте новую смету вручную или с помощью AI
            </p>
          </Link>

          <Link
            href="/calculator"
            className="block p-6 bg-white rounded-lg shadow-md hover:shadow-lg transition-shadow"
          >
            <div className="flex items-center mb-4">
              <Calculator className="w-8 h-8 text-green-600 mr-3" />
              <h3 className="text-xl font-semibold">Калькулятор объемов</h3>
            </div>
            <p className="text-gray-600">
              Рассчитайте объемы работ по размерам помещений
            </p>
          </Link>

          <Link
            href="/norms"
            className="block p-6 bg-white rounded-lg shadow-md hover:shadow-lg transition-shadow"
          >
            <div className="flex items-center mb-4">
              <Database className="w-8 h-8 text-purple-600 mr-3" />
              <h3 className="text-xl font-semibold">База норм</h3>
            </div>
            <p className="text-gray-600">
              Поиск и просмотр норм ФЕР, ГЭСН, ТЕР
            </p>
          </Link>

          <Link
            href="/settings"
            className="block p-6 bg-white rounded-lg shadow-md hover:shadow-lg transition-shadow"
          >
            <div className="flex items-center mb-4">
              <Cog className="w-8 h-8 text-gray-600 mr-3" />
              <h3 className="text-xl font-semibold">Настройки</h3>
            </div>
            <p className="text-gray-600">
              Коэффициенты, регионы, оффлайн-режим
            </p>
          </Link>
        </div>

        <div className="bg-white rounded-lg shadow-md p-8">
          <h3 className="text-2xl font-bold mb-4">Возможности</h3>
          <ul className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Автоматический расчет по ФЕР/ГЭСН/ТЕР</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>AI распознавание видов работ</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Расчет объемов работ (м², м³, пог.м)</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Работа в оффлайн-режиме</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Экспорт в PDF, Excel, Word</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Региональные коэффициенты</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Шаблоны смет</span>
            </li>
            <li className="flex items-start">
              <span className="text-green-500 mr-2">✓</span>
              <span>Мобильное приложение</span>
            </li>
          </ul>
        </div>
      </main>
    </div>
  )
}
