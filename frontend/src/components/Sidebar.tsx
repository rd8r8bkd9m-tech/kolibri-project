const conversations = [
  { id: "a", title: "Приветствие и выбор тем", date: "Сегодня", preview: "Начали новую беседу" },
  { id: "b", title: "Релиз ядра", date: "Чт, 25 сент.", preview: "Запрос на тестирование" },
  { id: "c", title: "Промпт для AGENTS.md", date: "Вт, 23 сент.", preview: "Помощь в выборе направления" },
  { id: "d", title: "Логические и числовые", date: "Сб, 13 сент.", preview: "Колибри ИИ" },
  { id: "e", title: "Makет ОС Колибри", date: "Ср, 25 июня", preview: "Обсуждение интерфейса" },
];

const Sidebar = () => (
  <div className="flex h-full flex-col rounded-3xl border border-border-strong bg-background-panel/80 p-6 backdrop-blur">
    <div>
      <p className="text-xs uppercase tracking-widest text-text-secondary">Беседы</p>
      <h2 className="mt-2 text-xl font-semibold text-text-primary">Сегодня</h2>
      <ul className="mt-6 space-y-2">
        {conversations.map((item, index) => (
          <li key={item.id}>
            <button
              type="button"
              className={`w-full rounded-2xl px-4 py-3 text-left transition-colors ${
                index === 0
                  ? "bg-primary/15 text-text-primary"
                  : "bg-background-card/60 text-text-secondary hover:bg-background-card"
              }`}
            >
              <p className="text-sm font-semibold text-text-primary">{item.title}</p>
              <p className="mt-1 text-xs text-text-secondary">{item.date} • {item.preview}</p>
            </button>
          </li>
        ))}
      </ul>
    </div>
    <div className="mt-6 rounded-2xl border border-border-strong bg-background-card/80 p-4">
      <p className="text-sm font-semibold text-text-primary">Vladislav Kochurov</p>
      <p className="mt-1 text-xs text-text-secondary">Колибри может делать ошибки.</p>
    </div>
  </div>
);

export default Sidebar;
