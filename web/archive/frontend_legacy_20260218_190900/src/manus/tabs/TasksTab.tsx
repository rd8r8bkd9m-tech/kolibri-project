import { useEffect, useMemo, useState } from 'react';
import { AlarmClock, ChevronDown, Plus, X } from 'lucide-react';

type TaskFrequency = 'daily' | 'weekly' | 'hourly';

interface TaskItem {
  id: string;
  title: string;
  instructions: string;
  frequency: TaskFrequency;
  time: string;
  pushEnabled: boolean;
  emailEnabled: boolean;
  createdAt: string;
}

interface TasksTabProps {
  onClose?: () => void;
}

const TASK_STORAGE_KEY = 'kolibri-mobile-tasks-v2';

const loadTasks = (): TaskItem[] => {
  try {
    const raw = localStorage.getItem(TASK_STORAGE_KEY);
    if (!raw) {
      return [];
    }
    const parsed = JSON.parse(raw) as TaskItem[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
};

const formatFrequency = (value: TaskFrequency): string => {
  if (value === 'daily') {
    return 'Daily';
  }
  if (value === 'weekly') {
    return 'Weekly';
  }
  return 'Hourly';
};

export const TasksTab = ({ onClose }: TasksTabProps) => {
  const [tasks, setTasks] = useState<TaskItem[]>(loadTasks);
  const [mode, setMode] = useState<'list' | 'create'>(() => (loadTasks().length > 0 ? 'list' : 'list'));

  const [title, setTitle] = useState('');
  const [instructions, setInstructions] = useState('');
  const [frequency, setFrequency] = useState<TaskFrequency>('daily');
  const [taskTime, setTaskTime] = useState(() => {
    const now = new Date();
    return `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`;
  });
  const [pushEnabled, setPushEnabled] = useState(true);
  const [emailEnabled, setEmailEnabled] = useState(true);

  useEffect(() => {
    try {
      localStorage.setItem(TASK_STORAGE_KEY, JSON.stringify(tasks));
    } catch {
      // ignore localStorage errors
    }
  }, [tasks]);

  const dailyCount = useMemo(() => tasks.filter((task) => task.frequency === 'daily').length, [tasks]);

  const resetForm = () => {
    setTitle('');
    setInstructions('');
    setFrequency('daily');
    setPushEnabled(true);
    setEmailEnabled(true);
  };

  const handleCreateTask = () => {
    if (!title.trim()) {
      return;
    }

    const task: TaskItem = {
      id: `task-${Date.now()}`,
      title: title.trim(),
      instructions: instructions.trim(),
      frequency,
      time: taskTime,
      pushEnabled,
      emailEnabled,
      createdAt: new Date().toISOString(),
    };

    setTasks((prev) => [task, ...prev]);
    resetForm();
    setMode('list');
  };

  const handleCloseList = () => {
    if (onClose) {
      onClose();
      return;
    }
    setMode('list');
  };

  return (
    <div className="kol-task-page">
      <div className="kol-task-sheet">
        {mode === 'list' ? (
          <>
            <header className="kol-task-header">
              <button type="button" className="kol-task-circle" onClick={handleCloseList} aria-label="Закрыть">
                <X size={32} />
              </button>
              <h1>Задачи</h1>
              <span className="kol-task-spacer" aria-hidden="true" />
            </header>

            {tasks.length === 0 ? (
              <section className="kol-task-empty">
                <div className="kol-task-empty-icon" aria-hidden="true">
                  <AlarmClock size={44} />
                </div>
                <h2>Начни с добавления задачи</h2>
                <p>Запланировать задачу для автоматизации любых запросов и получения напоминания после их выполнения</p>
                <button type="button" className="kol-task-outline-btn" onClick={() => setMode('create')}>
                  <Plus size={34} />
                  <span>Создать задачу</span>
                </button>
              </section>
            ) : (
              <section className="kol-task-list-wrap">
                <button type="button" className="kol-task-create-link" onClick={() => setMode('create')}>
                  Создать задачу
                </button>
                <div className="kol-task-list">
                  {tasks.map((task) => (
                    <article key={task.id} className="kol-task-row">
                      <div>
                        <h3>{task.title}</h3>
                        <p>{task.instructions || 'Инструкции будут добавлены позже'}</p>
                      </div>
                      <div className="kol-task-meta">
                        <span>{formatFrequency(task.frequency)}</span>
                        <time>{task.time}</time>
                      </div>
                    </article>
                  ))}
                </div>
              </section>
            )}
          </>
        ) : (
          <>
            <header className="kol-task-header is-create">
              <button
                type="button"
                className="kol-task-circle"
                onClick={() => {
                  setMode('list');
                  resetForm();
                }}
                aria-label="Назад"
              >
                <X size={32} />
              </button>
              <h1>Новая задача</h1>
              <button type="button" className="kol-task-create-top" onClick={handleCreateTask} disabled={!title.trim()}>
                Создать
              </button>
            </header>

            <div className="kol-task-form">
              <label className="kol-task-input-wrap">
                <input
                  type="text"
                  value={title}
                  onChange={(event) => setTitle(event.target.value)}
                  placeholder="Название задачи"
                  autoFocus
                />
              </label>

              <section className="kol-task-block">
                <h3>Schedule</h3>
                <div className="kol-task-table-row">
                  <span>Частота</span>
                  <label className="kol-task-select-wrap">
                    <select value={frequency} onChange={(event) => setFrequency(event.target.value as TaskFrequency)}>
                      <option value="daily">Daily</option>
                      <option value="weekly">Weekly</option>
                      <option value="hourly">Hourly</option>
                    </select>
                    <ChevronDown size={16} />
                  </label>
                </div>
                <div className="kol-task-divider" />
                <div className="kol-task-table-row">
                  <span>Время</span>
                  <label className="kol-task-time-wrap">
                    <input type="time" value={taskTime} onChange={(event) => setTaskTime(event.target.value)} />
                  </label>
                </div>
              </section>

              <section className="kol-task-block">
                <h3>Instructions</h3>
                <textarea
                  value={instructions}
                  onChange={(event) => setInstructions(event.target.value)}
                  placeholder="Введи запрос здесь"
                  rows={5}
                />
              </section>

              <section className="kol-task-block">
                <h3>Notifications</h3>
                <div className="kol-task-table-row">
                  <span>Push-уведомления</span>
                  <button
                    type="button"
                    className={`kol-task-switch ${pushEnabled ? 'is-on' : ''}`}
                    onClick={() => setPushEnabled((value) => !value)}
                    aria-pressed={pushEnabled}
                  >
                    <span />
                  </button>
                </div>
                <div className="kol-task-divider" />
                <div className="kol-task-table-row">
                  <span>Электронная почта</span>
                  <button
                    type="button"
                    className={`kol-task-switch ${emailEnabled ? 'is-on' : ''}`}
                    onClick={() => setEmailEnabled((value) => !value)}
                    aria-pressed={emailEnabled}
                  >
                    <span />
                  </button>
                </div>
              </section>
            </div>

            <div className="kol-task-counter">
              <span className="kol-task-counter-dot" />
              <div>
                <strong>{dailyCount}/2</strong>
                <span>Daily Tasks</span>
              </div>
            </div>
          </>
        )}
      </div>

      <style>{`
        .kol-task-page {
          height: 100%;
          overflow: auto;
          overflow-x: hidden;
          padding: 18px 14px calc(24px + env(safe-area-inset-bottom));
          background: var(--bg-primary);
          color: var(--text-primary);
        }

        .kol-task-sheet {
          min-height: calc(100% - 2px);
          background: var(--bg-secondary);
          border-radius: 34px;
          border: 1px solid var(--border-primary);
          padding: 16px 16px 26px;
          display: flex;
          flex-direction: column;
          position: relative;
          max-width: 100%;
        }

        .kol-task-header {
          display: grid;
          grid-template-columns: 84px minmax(0, 1fr) 84px;
          align-items: center;
          gap: 10px;
          min-height: 82px;
          margin-bottom: 14px;
        }

        .kol-task-header h1 {
          margin: 0;
          text-align: center;
          font-size: 60px;
          font-weight: 700;
          letter-spacing: -0.02em;
        }

        .kol-task-spacer {
          width: 84px;
          height: 84px;
        }

        .kol-task-circle {
          width: 84px;
          height: 84px;
          border-radius: 999px;
          border: 2px solid var(--border-primary);
          background: var(--bg-tertiary);
          color: var(--text-primary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
        }

        .kol-task-empty {
          flex: 1;
          display: grid;
          place-content: center;
          text-align: center;
          gap: 20px;
          padding: 20px;
        }

        .kol-task-empty-icon {
          width: 126px;
          height: 126px;
          border-radius: 50%;
          margin: 0 auto;
          background: var(--bg-tertiary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          color: var(--text-primary);
        }

        .kol-task-empty h2 {
          margin: 0;
          font-size: 64px;
          line-height: 1.08;
          letter-spacing: -0.02em;
        }

        .kol-task-empty p {
          margin: 0 auto;
          max-width: 680px;
          font-size: 48px;
          line-height: 1.24;
          color: var(--text-muted);
        }

        .kol-task-outline-btn {
          margin: 12px auto 0;
          border: 2px solid var(--border-primary);
          background: var(--bg-tertiary);
          color: var(--text-primary);
          height: 112px;
          border-radius: 56px;
          padding: 0 44px;
          display: inline-flex;
          align-items: center;
          gap: 16px;
          font-size: 58px;
          font-weight: 700;
        }

        .kol-task-list-wrap {
          display: grid;
          gap: 14px;
        }

        .kol-task-create-link {
          justify-self: flex-end;
          border: 1px solid var(--border-primary);
          border-radius: 999px;
          min-height: 56px;
          padding: 0 24px;
          font-size: 24px;
          color: var(--text-primary);
          background: transparent;
        }

        .kol-task-list {
          display: grid;
          gap: 12px;
        }

        .kol-task-row {
          border-radius: 20px;
          background: var(--bg-card);
          border: 1px solid var(--border-primary);
          padding: 18px;
          display: flex;
          justify-content: space-between;
          gap: 10px;
          align-items: flex-start;
        }

        .kol-task-row h3 {
          margin: 0;
          font-size: 24px;
        }

        .kol-task-row p {
          margin: 6px 0 0;
          font-size: 16px;
          color: var(--text-muted);
          line-height: 1.35;
        }

        .kol-task-meta {
          text-align: right;
          display: grid;
          gap: 5px;
          font-size: 14px;
          color: var(--text-muted);
        }

        .kol-task-header.is-create {
          margin-bottom: 10px;
        }

        .kol-task-create-top {
          justify-self: end;
          border: 2px solid var(--border-primary);
          min-height: 84px;
          padding: 0 26px;
          border-radius: 999px;
          background: transparent;
          color: var(--text-primary);
          font-size: 56px;
          font-weight: 700;
        }

        .kol-task-create-top:disabled {
          opacity: 0.4;
        }

        .kol-task-form {
          display: grid;
          gap: 18px;
          padding-bottom: 130px;
        }

        .kol-task-input-wrap input,
        .kol-task-block textarea,
        .kol-task-select-wrap,
        .kol-task-time-wrap {
          width: 100%;
          border-radius: 28px;
          border: 1px solid var(--border-primary);
          background: var(--bg-input);
          color: var(--text-primary);
        }

        .kol-task-input-wrap input {
          height: 118px;
          font-size: 62px;
          padding: 0 34px;
          outline: none;
        }

        .kol-task-input-wrap input::placeholder,
        .kol-task-block textarea::placeholder {
          color: var(--text-dimmed);
        }

        .kol-task-block {
          border-radius: 34px;
          border: 1px solid var(--border-primary);
          background: var(--bg-card);
          padding: 20px 22px;
          display: grid;
          gap: 16px;
        }

        .kol-task-block h3 {
          margin: 0;
          font-size: 56px;
          color: var(--text-muted);
          letter-spacing: -0.01em;
        }

        .kol-task-table-row {
          min-height: 86px;
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 16px;
          font-size: 62px;
          font-weight: 600;
        }

        .kol-task-table-row > span {
          min-width: 0;
          flex: 1;
          line-height: 1.12;
          overflow-wrap: anywhere;
        }

        .kol-task-divider {
          height: 1px;
          background: var(--border-primary);
        }

        .kol-task-select-wrap,
        .kol-task-time-wrap {
          min-width: 220px;
          height: 86px;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          gap: 8px;
          padding: 0 16px;
          overflow: hidden;
        }

        .kol-task-select-wrap select,
        .kol-task-time-wrap input {
          border: 0;
          outline: 0;
          background: transparent;
          color: var(--text-primary);
          font-size: 60px;
          font-weight: 700;
          text-align: center;
        }

        .kol-task-time-wrap input {
          width: 100%;
        }

        .kol-task-block textarea {
          min-height: 300px;
          resize: none;
          padding: 26px 28px;
          font-size: 58px;
          line-height: 1.18;
          outline: none;
        }

        .kol-task-switch {
          width: 134px;
          height: 74px;
          border-radius: 999px;
          border: 0;
          background: var(--bg-hover);
          padding: 7px;
          display: inline-flex;
          justify-content: flex-start;
        }

        .kol-task-switch span {
          width: 60px;
          height: 60px;
          border-radius: 50%;
          background: #ececec;
          transition: transform 160ms ease;
        }

        .kol-task-switch.is-on {
          background: #4be46a;
        }

        .kol-task-switch.is-on span {
          transform: translateX(60px);
          background: #ffffff;
        }

        .kol-task-counter {
          position: absolute;
          left: 50%;
          bottom: calc(18px + env(safe-area-inset-bottom));
          transform: translateX(-50%);
          width: min(360px, calc(100% - 40px));
          min-height: 78px;
          border-radius: 999px;
          border: 2px solid var(--border-primary);
          background: var(--bg-overlay);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          gap: 14px;
          color: var(--text-primary);
        }

        .kol-task-counter-dot {
          width: 40px;
          height: 40px;
          border-radius: 50%;
          border: 2px solid var(--border-primary);
        }

        .kol-task-counter div {
          display: grid;
          gap: 0;
          line-height: 1.05;
          text-align: left;
        }

        .kol-task-counter strong {
          font-size: 48px;
        }

        .kol-task-counter span {
          font-size: 42px;
          color: var(--text-muted);
        }

        @media (max-width: 900px) {
          .kol-task-header h1 {
            font-size: clamp(26px, 8.2vw, 56px);
          }

          .kol-task-circle {
            width: clamp(64px, 18vw, 84px);
            height: clamp(64px, 18vw, 84px);
          }

          .kol-task-spacer {
            width: clamp(64px, 18vw, 84px);
            height: clamp(64px, 18vw, 84px);
          }

          .kol-task-create-top {
            min-height: clamp(64px, 18vw, 84px);
            font-size: clamp(18px, 5.4vw, 28px);
            padding: 0 clamp(18px, 4vw, 26px);
          }

          .kol-task-empty h2 {
            font-size: clamp(24px, 7.4vw, 34px);
          }

          .kol-task-empty p {
            font-size: clamp(15px, 4.8vw, 20px);
          }

          .kol-task-outline-btn {
            font-size: clamp(17px, 4.8vw, 24px);
            height: clamp(60px, 14vw, 78px);
          }

          .kol-task-input-wrap input {
            height: clamp(60px, 15vw, 78px);
            font-size: clamp(18px, 5.2vw, 24px);
            padding: 0 clamp(18px, 4.5vw, 34px);
          }

          .kol-task-block h3 {
            font-size: clamp(17px, 4.9vw, 24px);
          }

          .kol-task-table-row {
            min-height: clamp(56px, 15vw, 86px);
            font-size: clamp(15px, 4.6vw, 21px);
            align-items: flex-start;
            gap: 10px;
          }

          .kol-task-select-wrap,
          .kol-task-time-wrap {
            height: clamp(44px, 12vw, 56px);
            min-width: clamp(104px, 28vw, 160px);
            padding: 0 10px;
          }

          .kol-task-select-wrap select,
          .kol-task-time-wrap input {
            font-size: clamp(14px, 4.2vw, 18px);
          }

          .kol-task-block textarea {
            min-height: clamp(136px, 36vw, 300px);
            font-size: clamp(16px, 4.6vw, 20px);
            padding: clamp(14px, 3vw, 26px) clamp(16px, 4vw, 28px);
          }

          .kol-task-switch {
            width: clamp(56px, 17vw, 76px);
            height: clamp(34px, 9vw, 44px);
            padding: 4px;
          }

          .kol-task-switch span {
            width: clamp(24px, 6.8vw, 30px);
            height: clamp(24px, 6.8vw, 30px);
          }

          .kol-task-switch.is-on span {
            transform: translateX(clamp(20px, 6.2vw, 34px));
          }

          .kol-task-counter strong {
            font-size: clamp(19px, 5.2vw, 24px);
          }

          .kol-task-counter span {
            font-size: clamp(14px, 3.8vw, 17px);
          }
        }

        @media (min-width: 901px) {
          .kol-task-page {
            padding: 24px;
            background: transparent;
          }

          .kol-task-sheet {
            border-radius: 24px;
            max-width: 920px;
            margin: 0 auto;
            background: var(--bg-secondary);
            padding: 18px 20px 22px;
          }

          .kol-task-header {
            grid-template-columns: 56px minmax(0, 1fr) 56px;
            min-height: 56px;
            margin-bottom: 12px;
          }

          .kol-task-circle,
          .kol-task-spacer,
          .kol-task-create-top {
            width: 56px;
            height: 56px;
            min-height: 56px;
            padding: 0;
            font-size: 16px;
          }

          .kol-task-create-top {
            width: auto;
            min-width: 120px;
            border-width: 1px;
          }

          .kol-task-header h1 {
            font-size: 38px;
          }

          .kol-task-empty h2 {
            font-size: 42px;
          }

          .kol-task-empty p {
            font-size: 26px;
            max-width: 700px;
          }

          .kol-task-outline-btn {
            height: 72px;
            font-size: 34px;
            border-width: 1px;
          }

          .kol-task-input-wrap input {
            height: 68px;
            font-size: 36px;
          }

          .kol-task-block h3 {
            font-size: 26px;
          }

          .kol-task-table-row {
            min-height: 64px;
            font-size: 34px;
          }

          .kol-task-select-wrap,
          .kol-task-time-wrap {
            height: 64px;
            min-width: 170px;
          }

          .kol-task-select-wrap select,
          .kol-task-time-wrap input {
            font-size: 32px;
          }

          .kol-task-block textarea {
            min-height: 170px;
            font-size: 32px;
          }

          .kol-task-switch {
            width: 96px;
            height: 52px;
          }

          .kol-task-switch span {
            width: 38px;
            height: 38px;
          }

          .kol-task-switch.is-on span {
            transform: translateX(44px);
          }

          .kol-task-counter {
            max-width: 320px;
            min-height: 64px;
            border-width: 1px;
            bottom: 12px;
          }

          .kol-task-counter-dot {
            width: 26px;
            height: 26px;
          }

          .kol-task-counter strong {
            font-size: 30px;
          }

          .kol-task-counter span {
            font-size: 20px;
          }
        }
      `}</style>
    </div>
  );
};
