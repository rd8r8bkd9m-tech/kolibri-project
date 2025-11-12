import { Link } from 'react-router-dom';

export default function NotFound() {
  return (
    <div className="min-h-screen flex flex-col items-center justify-center p-4">
      <div className="text-center">
        <h1 className="text-9xl font-bold text-cyan-500 mb-4">404</h1>
        <h2 className="text-3xl font-bold mb-2">Страница не найдена</h2>
        <p className="text-gray-400 mb-8">
          К сожалению, запрашиваемая вами страница не существует или была удалена.
        </p>

        <div className="space-y-4">
          <p className="text-gray-500 mb-6">
            Вернитесь на главную страницу или свяжитесь с поддержкой, если вы считаете, что это ошибка.
          </p>

          <Link
            to="/"
            className="inline-block px-6 py-3 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition"
          >
            На главную
          </Link>
        </div>
      </div>

      <div className="mt-16 text-gray-600 text-sm">
        Ошибка 404 • © 2025 Kolibri
      </div>
    </div>
  );
}
