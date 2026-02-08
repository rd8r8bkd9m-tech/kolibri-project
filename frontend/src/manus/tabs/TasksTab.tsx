/**
 * tabs/TasksTab.tsx
 *
 * Полнофункциональный менеджер задач с сохранением в localStorage.
 * Создание, удаление, смена статуса, приоритетов.
 */

import { useState, useCallback, useEffect } from 'react';
import {
  Plus,
  CheckCircle2,
  Circle,
  Clock,
  AlertCircle,
  PlayCircle,
  Trash2,
  X,
  ArrowUp,
  ArrowRight,
  ArrowDown,
  Edit3,
  Save,
} from 'lucide-react';

interface Task {
  id: string;
  title: string;
  description: string;
  status: 'pending' | 'running' | 'completed' | 'failed';
  priority: 'low' | 'medium' | 'high';
  createdAt: string;
}

const STORAGE_KEY = 'kolibri-tasks';

function loadTasks(): Task[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) return JSON.parse(raw);
  } catch {}
  return [];
}

function saveTasks(tasks: Task[]) {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify(tasks)); } catch {}
}

const STATUS_CONFIG = {
  pending: { icon: Circle, color: 'var(--text-muted)', label: 'Ожидает' },
  running: { icon: PlayCircle, color: 'var(--warning)', label: 'В работе' },
  completed: { icon: CheckCircle2, color: 'var(--success)', label: 'Готово' },
  failed: { icon: AlertCircle, color: 'var(--error)', label: 'Ошибка' },
};

const PRIORITY_CONFIG = {
  low: { color: 'var(--info)', label: 'Низкий', icon: ArrowDown },
  medium: { color: 'var(--warning)', label: 'Средний', icon: ArrowRight },
  high: { color: 'var(--error)', label: 'Высокий', icon: ArrowUp },
};

export const TasksTab = () => {
  const [tasks, setTasks] = useState<Task[]>(loadTasks);
  const [filter, setFilter] = useState<'all' | 'pending' | 'running' | 'completed'>('all');
  const [showAddDialog, setShowAddDialog] = useState(false);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editTitle, setEditTitle] = useState('');

  // Новая задача
  const [newTitle, setNewTitle] = useState('');
  const [newDesc, setNewDesc] = useState('');
  const [newPriority, setNewPriority] = useState<Task['priority']>('medium');

  // Сохранять при каждом изменении
  useEffect(() => { saveTasks(tasks); }, [tasks]);

  const filteredTasks = tasks.filter(t => filter === 'all' || t.status === filter);

  const stats = {
    total: tasks.length,
    completed: tasks.filter(t => t.status === 'completed').length,
    running: tasks.filter(t => t.status === 'running').length,
    pending: tasks.filter(t => t.status === 'pending').length,
  };

  const addTask = useCallback(() => {
    if (!newTitle.trim()) return;
    const task: Task = {
      id: Date.now().toString(),
      title: newTitle.trim(),
      description: newDesc.trim(),
      status: 'pending',
      priority: newPriority,
      createdAt: new Date().toISOString(),
    };
    setTasks(prev => [task, ...prev]);
    setNewTitle('');
    setNewDesc('');
    setNewPriority('medium');
    setShowAddDialog(false);
  }, [newTitle, newDesc, newPriority]);

  const toggleStatus = (id: string) => {
    setTasks(prev => prev.map(t => {
      if (t.id !== id) return t;
      const nextStatus: Record<string, Task['status']> = {
        pending: 'running',
        running: 'completed',
        completed: 'pending',
        failed: 'pending',
      };
      return { ...t, status: nextStatus[t.status] };
    }));
  };

  const deleteTask = (id: string) => {
    setTasks(prev => prev.filter(t => t.id !== id));
  };

  const startEdit = (task: Task) => {
    setEditingId(task.id);
    setEditTitle(task.title);
  };

  const saveEdit = (id: string) => {
    if (editTitle.trim()) {
      setTasks(prev => prev.map(t => t.id === id ? { ...t, title: editTitle.trim() } : t));
    }
    setEditingId(null);
  };

  const changePriority = (id: string) => {
    setTasks(prev => prev.map(t => {
      if (t.id !== id) return t;
      const cycle: Record<string, Task['priority']> = { low: 'medium', medium: 'high', high: 'low' };
      return { ...t, priority: cycle[t.priority] };
    }));
  };

  return (
    <div className="tasks-tab">
      {/* Заголовок */}
      <div className="tasks-header">
        <div className="tasks-title-section">
          <h1 className="tasks-title">Задачи</h1>
          <span className="tasks-count">{stats.total}</span>
        </div>
        <button className="add-task-btn" onClick={() => setShowAddDialog(true)}>
          <Plus size={18} />
          <span>Новая задача</span>
        </button>
      </div>

      {/* Статистика */}
      <div className="tasks-stats">
        <div className="tstat-card">
          <div className="tstat-value done">{stats.completed}</div>
          <div className="tstat-label">Выполнено</div>
        </div>
        <div className="tstat-card">
          <div className="tstat-value running">{stats.running}</div>
          <div className="tstat-label">В работе</div>
        </div>
        <div className="tstat-card">
          <div className="tstat-value pending">{stats.pending}</div>
          <div className="tstat-label">Ожидает</div>
        </div>
      </div>

      {/* Фильтры */}
      <div className="tasks-filters">
        {(['all', 'pending', 'running', 'completed'] as const).map(f => (
          <button
            key={f}
            className={`tfilter-btn ${filter === f ? 'active' : ''}`}
            onClick={() => setFilter(f)}
          >
            {f === 'all' ? `Все (${stats.total})` : `${STATUS_CONFIG[f].label} (${stats[f === 'completed' ? 'completed' : f === 'running' ? 'running' : 'pending']})`}
          </button>
        ))}
      </div>

      {/* Список задач */}
      <div className="tasks-list">
        {filteredTasks.map(task => {
          const StatusIcon = STATUS_CONFIG[task.status].icon;
          const PriorityIcon = PRIORITY_CONFIG[task.priority].icon;
          const isEditing = editingId === task.id;

          return (
            <div key={task.id} className={`task-item ${task.status}`}>
              <button
                className="task-status-btn"
                onClick={() => toggleStatus(task.id)}
                style={{ color: STATUS_CONFIG[task.status].color }}
                title={`Сейчас: ${STATUS_CONFIG[task.status].label}. Клик — сменить.`}
              >
                <StatusIcon size={20} />
              </button>

              <div className="task-content">
                {isEditing ? (
                  <div className="task-edit-row">
                    <input
                      className="task-edit-input"
                      value={editTitle}
                      onChange={(e) => setEditTitle(e.target.value)}
                      onKeyDown={(e) => e.key === 'Enter' && saveEdit(task.id)}
                      autoFocus
                    />
                    <button className="task-save-btn" onClick={() => saveEdit(task.id)}>
                      <Save size={14} />
                    </button>
                  </div>
                ) : (
                  <div className="task-title" onDoubleClick={() => startEdit(task)}>{task.title}</div>
                )}
                {task.description && !isEditing && (
                  <div className="task-desc">{task.description}</div>
                )}
                <div className="task-meta">
                  <button
                    className="task-priority-btn"
                    style={{ color: PRIORITY_CONFIG[task.priority].color }}
                    onClick={() => changePriority(task.id)}
                    title="Клик — сменить приоритет"
                  >
                    <PriorityIcon size={12} />
                    {PRIORITY_CONFIG[task.priority].label}
                  </button>
                  <span className="task-time">
                    <Clock size={12} />
                    {new Date(task.createdAt).toLocaleDateString('ru-RU')}
                  </span>
                </div>
              </div>

              <div className="task-actions">
                <button className="taction-btn" onClick={() => startEdit(task)} title="Редактировать">
                  <Edit3 size={14} />
                </button>
                <button className="taction-btn danger" onClick={() => deleteTask(task.id)} title="Удалить">
                  <Trash2 size={14} />
                </button>
              </div>
            </div>
          );
        })}

        {filteredTasks.length === 0 && (
          <div className="tasks-empty">
            <Circle size={48} strokeWidth={1} />
            <p>{tasks.length === 0 ? 'Задач пока нет. Создайте первую!' : 'Нет задач с таким фильтром.'}</p>
          </div>
        )}
      </div>

      {/* === Диалог создания задачи === */}
      {showAddDialog && (
        <div className="dialog-overlay" onClick={() => setShowAddDialog(false)}>
          <div className="dialog" onClick={(e) => e.stopPropagation()}>
            <div className="dialog-header">
              <h3>Новая задача</h3>
              <button className="dialog-close" onClick={() => setShowAddDialog(false)}>
                <X size={18} />
              </button>
            </div>

            <div className="dialog-body">
              <div className="dialog-field">
                <label>Название</label>
                <input
                  type="text"
                  className="dialog-input"
                  value={newTitle}
                  onChange={(e) => setNewTitle(e.target.value)}
                  onKeyDown={(e) => e.key === 'Enter' && addTask()}
                  placeholder="Что нужно сделать?"
                  autoFocus
                />
              </div>

              <div className="dialog-field">
                <label>Описание (опционально)</label>
                <textarea
                  className="dialog-textarea"
                  value={newDesc}
                  onChange={(e) => setNewDesc(e.target.value)}
                  placeholder="Подробности задачи..."
                  rows={3}
                />
              </div>

              <div className="dialog-field">
                <label>Приоритет</label>
                <div className="priority-selector">
                  {(['low', 'medium', 'high'] as const).map(p => {
                    const PI = PRIORITY_CONFIG[p].icon;
                    return (
                      <button
                        key={p}
                        className={`priority-btn ${newPriority === p ? 'active' : ''}`}
                        style={newPriority === p ? { borderColor: PRIORITY_CONFIG[p].color, color: PRIORITY_CONFIG[p].color } : {}}
                        onClick={() => setNewPriority(p)}
                      >
                        <PI size={14} />
                        {PRIORITY_CONFIG[p].label}
                      </button>
                    );
                  })}
                </div>
              </div>
            </div>

            <div className="dialog-footer">
              <button className="dialog-cancel" onClick={() => setShowAddDialog(false)}>Отмена</button>
              <button className="dialog-submit" onClick={addTask} disabled={!newTitle.trim()}>
                <Plus size={16} />
                Создать
              </button>
            </div>
          </div>
        </div>
      )}

      <style>{`
        .tasks-tab { display: flex; flex-direction: column; height: 100%; padding: 24px; overflow-y: auto; }

        .tasks-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; }
        .tasks-title-section { display: flex; align-items: center; gap: 12px; }
        .tasks-title { font-size: 28px; font-weight: 600; margin: 0; color: var(--text-primary); }
        .tasks-count { background: var(--accent-bg); color: var(--accent-primary); padding: 4px 10px; border-radius: 20px; font-size: 13px; font-weight: 500; }
        .add-task-btn { display: flex; align-items: center; gap: 8px; padding: 10px 16px; background: var(--accent-gradient); border: none; border-radius: 10px; color: white; font-size: 14px; font-weight: 500; cursor: pointer; transition: all 0.15s ease; }
        .add-task-btn:hover { transform: translateY(-1px); box-shadow: var(--shadow-elevated); }

        .tasks-stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; margin-bottom: 24px; }
        .tstat-card { background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 12px; padding: 20px; text-align: center; }
        .tstat-value { font-size: 32px; font-weight: 600; margin-bottom: 4px; }
        .tstat-value.done { color: var(--success); }
        .tstat-value.running { color: var(--warning); }
        .tstat-value.pending { color: var(--text-muted); }
        .tstat-label { font-size: 13px; color: var(--text-muted); }

        .tasks-filters { display: flex; gap: 8px; margin-bottom: 20px; flex-wrap: wrap; }
        .tfilter-btn { padding: 8px 16px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 8px; color: var(--text-secondary); font-size: 13px; cursor: pointer; transition: all 0.15s ease; }
        .tfilter-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
        .tfilter-btn.active { background: var(--accent-bg); border-color: var(--border-accent); color: var(--accent-primary); }

        .tasks-list { display: flex; flex-direction: column; gap: 8px; }
        .task-item { display: flex; align-items: flex-start; gap: 12px; padding: 16px; background: var(--bg-card); border: 1px solid var(--border-primary); border-radius: 12px; transition: all 0.15s ease; }
        .task-item:hover { background: var(--bg-hover); border-color: var(--border-hover); }
        .task-item.completed .task-title { text-decoration: line-through; color: var(--text-muted); }

        .task-status-btn { background: none; border: none; cursor: pointer; padding: 4px; display: flex; transition: transform 0.15s ease; flex-shrink: 0; margin-top: 2px; }
        .task-status-btn:hover { transform: scale(1.15); }
        .task-content { flex: 1; min-width: 0; }
        .task-title { font-size: 14px; font-weight: 500; color: var(--text-primary); margin-bottom: 4px; cursor: default; }
        .task-desc { font-size: 12px; color: var(--text-muted); margin-bottom: 6px; line-height: 1.4; }
        .task-meta { display: flex; align-items: center; gap: 12px; }
        .task-priority-btn { display: flex; align-items: center; gap: 4px; padding: 2px 8px; background: transparent; border: none; border-radius: 4px; font-size: 11px; font-weight: 500; cursor: pointer; }
        .task-priority-btn:hover { opacity: 0.8; }
        .task-time { display: flex; align-items: center; gap: 4px; font-size: 11px; color: var(--text-dimmed); }

        .task-edit-row { display: flex; gap: 8px; margin-bottom: 4px; }
        .task-edit-input { flex: 1; padding: 6px 10px; background: var(--bg-overlay); border: 1px solid var(--accent-primary); border-radius: 6px; color: var(--text-primary); font-size: 14px; outline: none; }
        .task-save-btn { width: 28px; height: 28px; border-radius: 6px; background: var(--accent-bg); border: none; color: var(--accent-primary); cursor: pointer; display: flex; align-items: center; justify-content: center; }

        .task-actions { display: flex; gap: 4px; opacity: 0; transition: opacity 0.15s ease; flex-shrink: 0; }
        .task-item:hover .task-actions { opacity: 1; }
        .taction-btn { width: 28px; height: 28px; border-radius: 6px; background: transparent; border: none; color: var(--text-muted); cursor: pointer; display: flex; align-items: center; justify-content: center; }
        .taction-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
        .taction-btn.danger:hover { background: rgba(239, 68, 68, 0.15); color: var(--error); }

        .tasks-empty { display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 60px; color: var(--text-dimmed); }
        .tasks-empty p { margin-top: 16px; font-size: 14px; }

        /* Диалог */
        .dialog-overlay { position: fixed; inset: 0; background: rgba(0, 0, 0, 0.5); backdrop-filter: blur(4px); display: flex; align-items: center; justify-content: center; z-index: 100; }
        .dialog { background: var(--bg-primary); border: 1px solid var(--border-primary); border-radius: 16px; width: 480px; max-width: 90vw; box-shadow: var(--shadow-elevated); }
        .dialog-header { display: flex; justify-content: space-between; align-items: center; padding: 20px 24px 0; }
        .dialog-header h3 { margin: 0; font-size: 18px; font-weight: 600; color: var(--text-primary); }
        .dialog-close { background: none; border: none; color: var(--text-muted); cursor: pointer; padding: 4px; border-radius: 6px; }
        .dialog-close:hover { background: var(--bg-hover); color: var(--text-primary); }
        .dialog-body { padding: 20px 24px; }
        .dialog-field { margin-bottom: 16px; }
        .dialog-field label { display: block; font-size: 13px; font-weight: 500; color: var(--text-secondary); margin-bottom: 6px; }
        .dialog-input, .dialog-textarea { width: 100%; padding: 10px 14px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 8px; color: var(--text-primary); font-size: 14px; outline: none; box-sizing: border-box; font-family: inherit; }
        .dialog-input:focus, .dialog-textarea:focus { border-color: var(--accent-primary); }
        .dialog-input::placeholder, .dialog-textarea::placeholder { color: var(--text-faint); }
        .dialog-textarea { resize: vertical; min-height: 60px; }

        .priority-selector { display: flex; gap: 8px; }
        .priority-btn { display: flex; align-items: center; gap: 6px; padding: 8px 14px; background: var(--bg-tertiary); border: 2px solid var(--border-primary); border-radius: 8px; color: var(--text-muted); font-size: 13px; cursor: pointer; transition: all 0.15s ease; }
        .priority-btn:hover { background: var(--bg-hover); }
        .priority-btn.active { background: var(--bg-card); }

        .dialog-footer { display: flex; justify-content: flex-end; gap: 8px; padding: 0 24px 20px; }
        .dialog-cancel { padding: 10px 16px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 8px; color: var(--text-secondary); font-size: 13px; cursor: pointer; }
        .dialog-cancel:hover { background: var(--bg-hover); }
        .dialog-submit { display: flex; align-items: center; gap: 6px; padding: 10px 20px; background: var(--accent-gradient); border: none; border-radius: 8px; color: white; font-size: 13px; font-weight: 500; cursor: pointer; }
        .dialog-submit:disabled { opacity: 0.4; cursor: not-allowed; }
        .dialog-submit:hover:not(:disabled) { transform: translateY(-1px); box-shadow: var(--shadow-elevated); }
      `}</style>
    </div>
  );
};
