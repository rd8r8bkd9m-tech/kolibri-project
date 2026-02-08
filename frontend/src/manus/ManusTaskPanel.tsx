/**
 * ManusTaskPanel.tsx
 * 
 * Боковая панель с визуализацией задач и прогресса.
 * Показывает текущие задачи AI-агента в реальном времени.
 */

import { CheckCircle2, Circle, Loader2, XCircle, Zap } from 'lucide-react';
import type { Task } from './ManusApp';

interface ManusTaskPanelProps {
  tasks: Task[];
  currentPhase: string;
  isProcessing: boolean;
}

const TaskIcon = ({ status }: { status: Task['status'] }) => {
  switch (status) {
    case 'completed':
      return <CheckCircle2 size={16} className="task-icon task-icon-completed" />;
    case 'running':
      return <Loader2 size={16} className="task-icon task-icon-running" />;
    case 'failed':
      return <XCircle size={16} className="task-icon task-icon-failed" />;
    default:
      return <Circle size={16} className="task-icon task-icon-pending" />;
  }
};

export const ManusTaskPanel = ({ tasks, currentPhase, isProcessing }: ManusTaskPanelProps) => {
  return (
    <aside className="manus-task-panel">
      <div className="manus-task-header">
        <Zap size={18} className="manus-task-header-icon" />
        <span>Задачи</span>
        {isProcessing && (
          <span className="manus-task-badge">В работе</span>
        )}
      </div>

      {currentPhase && isProcessing && (
        <div className="manus-current-phase">
          <div className="manus-phase-indicator" />
          <span>{currentPhase}</span>
        </div>
      )}

      <div className="manus-task-list">
        {tasks.length === 0 ? (
          <div className="manus-task-empty">
            <Circle size={32} className="manus-task-empty-icon" />
            <p>Нет активных задач</p>
            <span>Задайте вопрос или дайте задание</span>
          </div>
        ) : (
          tasks.map((task) => (
            <div 
              key={task.id} 
              className={`manus-task-item manus-task-${task.status}`}
            >
              <TaskIcon status={task.status} />
              <div className="manus-task-content">
                <span className="manus-task-title">{task.title}</span>
                {task.progress !== undefined && task.status === 'running' && (
                  <div className="manus-task-progress">
                    <div 
                      className="manus-task-progress-bar"
                      style={{ width: `${task.progress}%` }}
                    />
                  </div>
                )}
              </div>
            </div>
          ))
        )}
      </div>

      <style>{`
        .manus-task-panel {
          width: 280px;
          min-width: 280px;
          background: rgba(15, 15, 20, 0.6);
          border-right: 1px solid rgba(255, 255, 255, 0.06);
          display: flex;
          flex-direction: column;
          overflow: hidden;
        }

        .manus-task-header {
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 16px 20px;
          font-size: 14px;
          font-weight: 600;
          color: #a1a1aa;
          border-bottom: 1px solid rgba(255, 255, 255, 0.06);
        }

        .manus-task-header-icon {
          color: #6366f1;
        }

        .manus-task-badge {
          margin-left: auto;
          font-size: 10px;
          font-weight: 500;
          padding: 2px 8px;
          background: rgba(34, 197, 94, 0.2);
          color: #22c55e;
          border-radius: 10px;
          animation: pulse 2s infinite;
        }

        .manus-current-phase {
          display: flex;
          align-items: center;
          gap: 10px;
          padding: 12px 20px;
          background: rgba(99, 102, 241, 0.1);
          border-bottom: 1px solid rgba(99, 102, 241, 0.2);
          font-size: 13px;
          color: #a5b4fc;
        }

        .manus-phase-indicator {
          width: 8px;
          height: 8px;
          background: #6366f1;
          border-radius: 50%;
          animation: pulse 1s infinite;
        }

        .manus-task-list {
          flex: 1;
          overflow-y: auto;
          padding: 12px;
        }

        .manus-task-empty {
          display: flex;
          flex-direction: column;
          align-items: center;
          justify-content: center;
          height: 200px;
          text-align: center;
          color: #52525b;
        }

        .manus-task-empty-icon {
          margin-bottom: 12px;
          opacity: 0.3;
        }

        .manus-task-empty p {
          font-size: 14px;
          margin-bottom: 4px;
        }

        .manus-task-empty span {
          font-size: 12px;
          opacity: 0.6;
        }

        .manus-task-item {
          display: flex;
          align-items: flex-start;
          gap: 12px;
          padding: 12px;
          margin-bottom: 8px;
          background: rgba(255, 255, 255, 0.02);
          border: 1px solid rgba(255, 255, 255, 0.04);
          border-radius: 12px;
          transition: all 0.2s ease;
          animation: slideIn 0.3s ease;
        }

        .manus-task-item:hover {
          background: rgba(255, 255, 255, 0.04);
        }

        .manus-task-running {
          border-color: rgba(99, 102, 241, 0.3);
          background: rgba(99, 102, 241, 0.05);
        }

        .manus-task-completed {
          border-color: rgba(34, 197, 94, 0.2);
        }

        .manus-task-failed {
          border-color: rgba(239, 68, 68, 0.2);
        }

        .task-icon {
          flex-shrink: 0;
          margin-top: 2px;
        }

        .task-icon-pending { color: #52525b; }
        .task-icon-running { 
          color: #6366f1; 
          animation: spin 1s linear infinite;
        }
        .task-icon-completed { color: #22c55e; }
        .task-icon-failed { color: #ef4444; }

        .manus-task-content {
          flex: 1;
          min-width: 0;
        }

        .manus-task-title {
          display: block;
          font-size: 13px;
          color: #e4e4e7;
          line-height: 1.4;
        }

        .manus-task-progress {
          margin-top: 8px;
          height: 4px;
          background: rgba(99, 102, 241, 0.2);
          border-radius: 2px;
          overflow: hidden;
        }

        .manus-task-progress-bar {
          height: 100%;
          background: linear-gradient(90deg, #6366f1, #8b5cf6);
          border-radius: 2px;
          transition: width 0.3s ease;
        }

        @keyframes spin {
          from { transform: rotate(0deg); }
          to { transform: rotate(360deg); }
        }

        @keyframes slideIn {
          from { opacity: 0; transform: translateX(-10px); }
          to { opacity: 1; transform: translateX(0); }
        }

        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
      `}</style>
    </aside>
  );
};

export default ManusTaskPanel;
