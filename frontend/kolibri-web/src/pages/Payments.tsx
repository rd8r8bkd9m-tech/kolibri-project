export default function Payments() {
  const payments = [
    {
      id: 'PAY-001',
      date: '10.11.2025',
      amount: '$10,000',
      method: 'Яндекс.Касса',
      status: 'completed',
    },
    {
      id: 'PAY-002',
      date: '05.11.2025',
      amount: '$50,000',
      method: 'Sberbank',
      status: 'completed',
    },
  ];

  return (
    <div className="p-8">
      <h1 className="text-3xl font-bold mb-8">Платежи</h1>

      {/* Balance */}
      <div className="bg-gradient-to-r from-cyan-600 to-cyan-400 rounded-lg p-8 mb-8 text-black">
        <p className="text-sm opacity-90">Баланс счета</p>
        <p className="text-4xl font-bold mt-2">$2,350.00</p>
      </div>

      {/* Payment Methods */}
      <div className="mb-8">
        <h2 className="text-xl font-bold mb-4">Способы оплаты</h2>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
          {[
            { name: 'Яндекс.Касса', icon: '🎯' },
            { name: 'Sberbank', icon: '🏦' },
            { name: 'Tinkoff', icon: '💳' },
            { name: 'ЮMoney', icon: '💰' },
            { name: 'Sber ID', icon: '📱' },
            { name: 'PaySystem', icon: '🌐' },
          ].map(method => (
            <button
              key={method.name}
              className="bg-gray-800 hover:bg-gray-700 rounded-lg p-4 text-center transition"
            >
              <p className="text-2xl mb-2">{method.icon}</p>
              <p className="text-sm font-semibold">{method.name}</p>
            </button>
          ))}
        </div>
      </div>

      {/* Recent Payments */}
      <div>
        <h2 className="text-xl font-bold mb-4">Недавние платежи</h2>
        <div className="space-y-2">
          {payments.map(payment => (
            <div key={payment.id} className="bg-gray-800 rounded-lg p-4 flex justify-between items-center">
              <div>
                <p className="font-semibold">{payment.date}</p>
                <p className="text-sm text-gray-400">{payment.method}</p>
              </div>
              <div className="text-right">
                <p className="font-semibold text-cyan-400">{payment.amount}</p>
                <p className="text-xs text-green-400">✓ {payment.status}</p>
              </div>
            </div>
          ))}
        </div>
      </div>

      <button className="mt-8 w-full px-4 py-3 bg-cyan-500 hover:bg-cyan-600 text-black font-semibold rounded-lg transition">
        Произвести платёж
      </button>
    </div>
  );
}
