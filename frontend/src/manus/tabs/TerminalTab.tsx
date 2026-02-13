/**
 * tabs/TerminalTab.tsx
 *
 * Полнофункциональный терминал с реальным выполнением команд через backend API.
 * Белый список команд, история, быстрые действия.
 */

import { useState, useRef, useEffect, useCallback } from 'react';
import {
  Terminal as TerminalIcon,
  Play,
  Trash2,
  Copy,
  ChevronRight,
  Loader2,
  Check,
  AlertTriangle,
} from 'lucide-react';

const API = '/api';

interface CommandEntry {
  id: string;
  command: string;
  output: string;
  status: 'success' | 'error' | 'running' | 'timeout';
  duration_ms: number;
  timestamp: Date;
}

const QUICK_COMMANDS = [
  { label: 'Статистика модели', cmd: './build/kolibri_mass_trainer --model data/models/kolibri_web.klm --stats' },
  { label: 'Список моделей', cmd: 'ls -lh data/models/' },
  { label: 'Диск', cmd: 'df -h .' },
  { label: 'Память', cmd: 'free -h' },
  { label: 'Uptime', cmd: 'uptime' },
  { label: 'Кто я', cmd: 'whoami && hostname' },
  { label: 'Бинарники', cmd: 'ls -lh build/kolibri_mass_trainer' },
  { label: 'Тесты', cmd: 'ctest --test-dir build --output-on-failure 2>&1 | tail -20' },
];

export const TerminalTab = () => {
  const [history, setHistory] = useState<CommandEntry[]>([]);
  const [currentCommand, setCurrentCommand] = useState('');
  const [isRunning, setIsRunning] = useState(false);
  const [cmdHistory, setCmdHistory] = useState<string[]>([]);
  const [historyIdx, setHistoryIdx] = useState(-1);
  const [copied, setCopied] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);
  const outputRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [history]);

  const executeCommand = useCallback(async (cmdOverride?: string) => {
    const command = (cmdOverride || currentCommand).trim();
    if (!command || isRunning) return;

    // Локальная clear
    if (command === 'clear') {
      setHistory([]);
      setCurrentCommand('');
      return;
    }

    // Локальная help
    if (command === 'help') {
      setHistory(prev => [...prev, {
        id: Date.now().toString(),
        command,
        output: `Колибри Terminal — реальное выполнение команд\n\nДоступные команды:\n  ls, cat, head, tail, wc, du, df, free, uptime\n  whoami, hostname, date, pwd, echo, find, grep\n  file, stat, uname, env, which\n  ./build/* — бинарники проекта\n  cmake, ctest — сборка и тесты\n\nСпециальные:\n  clear — очистить терминал\n  help  — эта справка\n\n⚠️ Опасные команды (rm, dd, mkfs) заблокированы.`,
        status: 'success',
        duration_ms: 0,
        timestamp: new Date(),
      }]);
      setCurrentCommand('');
      return;
    }

    const newEntry: CommandEntry = {
      id: Date.now().toString(),
      command,
      output: '',
      status: 'running',
      duration_ms: 0,
      timestamp: new Date(),
    };

    setHistory(prev => [...prev, newEntry]);
    setCmdHistory(prev => [command, ...prev.filter(c => c !== command)].slice(0, 50));
    setHistoryIdx(-1);
    setIsRunning(true);
    setCurrentCommand('');

    try {
      const r = await fetch(`${API}/v1/terminal/exec`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command, timeout: 15 }),
      });

      if (r.ok) {
        const data = await r.json();
        const output = (data.stdout || '') + (data.stderr ? `\n${data.stderr}` : '');
        setHistory(prev => prev.map(entry =>
          entry.id === newEntry.id
            ? {
                ...entry,
                output: output.trim() || '(пустой вывод)',
                status: data.status === 'timeout' ? 'timeout' : (data.exit_code === 0 ? 'success' : 'error'),
                duration_ms: data.duration_ms,
              }
            : entry
        ));
      } else {
        const errData = await r.json().catch(() => ({ detail: r.statusText }));
        setHistory(prev => prev.map(entry =>
          entry.id === newEntry.id
            ? { ...entry, output: `❌ ${errData.detail || 'Ошибка сервера'}`, status: 'error' as const, duration_ms: 0 }
            : entry
        ));
      }
    } catch (e) {
      setHistory(prev => prev.map(entry =>
        entry.id === newEntry.id
          ? { ...entry, output: `❌ Нет связи с backend: ${e}`, status: 'error' as const, duration_ms: 0 }
          : entry
      ));
    } finally {
      setIsRunning(false);
      inputRef.current?.focus();
    }
  }, [currentCommand, isRunning]);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      executeCommand();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (cmdHistory.length > 0) {
        const newIdx = Math.min(historyIdx + 1, cmdHistory.length - 1);
        setHistoryIdx(newIdx);
        setCurrentCommand(cmdHistory[newIdx]);
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIdx > 0) {
        const newIdx = historyIdx - 1;
        setHistoryIdx(newIdx);
        setCurrentCommand(cmdHistory[newIdx]);
      } else {
        setHistoryIdx(-1);
        setCurrentCommand('');
      }
    }
  };

  const clearHistory = () => { setHistory([]); };

  const copyOutput = () => {
    const text = history.map(h => `$ ${h.command}\n${h.output}`).join('\n\n');
    navigator.clipboard.writeText(text);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="terminal-tab">
      <div className="terminal-header">
        <div className="terminal-title-section">
          <TerminalIcon size={22} className="terminal-icon-green" />
          <h1 className="terminal-title">Терминал</h1>
          {isRunning && <span className="running-badge"><Loader2 size={12} className="spinning" /> Выполняется...</span>}
        </div>
        <div className="terminal-actions">
          <button className="term-action-btn" onClick={copyOutput} title="Копировать всё">
            {copied ? <Check size={16} /> : <Copy size={16} />}
          </button>
          <button className="term-action-btn danger" onClick={clearHistory} title="Очистить">
            <Trash2 size={16} />
          </button>
        </div>
      </div>

      {/* Вывод */}
      <div className="terminal-output" ref={outputRef} onClick={() => inputRef.current?.focus()}>
        <div className="terminal-welcome">
          <pre className="ascii-art">{`  ██╗  ██╗ ██████╗ ██╗     ██╗██████╗ ██████╗ ██╗
  ██║ ██╔╝██╔═══██╗██║     ██║██╔══██╗██╔══██╗██║
  █████╔╝ ██║   ██║██║     ██║██████╔╝██████╔╝██║
  ██╔═██╗ ██║   ██║██║     ██║██╔══██╗██╔══██╗██║
  ██║  ██╗╚██████╔╝███████╗██║██████╔╝██║  ██║██║
  ╚═╝  ╚═╝ ╚═════╝ ╚══════╝╚═╝╚═════╝ ╚═╝  ╚═╝╚═╝`}</pre>
          <div className="welcome-text">
            Колибри OS Terminal — реальное выполнение команд
            <br />
            Наберите <span className="hl">help</span> для списка команд · <span className="hl">↑↓</span> история
          </div>
        </div>

        {history.map(entry => (
          <div key={entry.id} className="command-entry">
            <div className="command-line">
              <span className="prompt">$</span>
              <span className="command-text">{entry.command}</span>
              {entry.status !== 'running' && entry.duration_ms > 0 && (
                <span className="cmd-duration">{entry.duration_ms.toFixed(0)}ms</span>
              )}
            </div>
            {entry.status === 'running' ? (
              <div className="command-running">
                <Loader2 size={14} className="spinning" />
                <span>Выполнение...</span>
              </div>
            ) : entry.status === 'timeout' ? (
              <div className="command-timeout">
                <AlertTriangle size={14} />
                <span>{entry.output}</span>
              </div>
            ) : (
              <pre className={`command-output ${entry.status}`}>{entry.output}</pre>
            )}
          </div>
        ))}

        {/* Строка ввода */}
        <div className="input-line">
          <span className="prompt">$</span>
          <input
            ref={inputRef}
            type="text"
            value={currentCommand}
            onChange={(e) => setCurrentCommand(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Введите команду..."
            className="command-input"
            disabled={isRunning}
            autoFocus
          />
          <button
            className="execute-btn"
            onClick={() => executeCommand()}
            disabled={isRunning || !currentCommand.trim()}
          >
            <Play size={16} />
          </button>
        </div>
      </div>

      {/* Быстрые команды */}
      <div className="quick-commands">
        <span className="quick-label">Быстрые команды:</span>
        <div className="quick-btns">
          {QUICK_COMMANDS.map(qc => (
            <button
              key={qc.label}
              className="quick-btn"
              onClick={() => executeCommand(qc.cmd)}
              disabled={isRunning}
            >
              <ChevronRight size={12} />
              {qc.label}
            </button>
          ))}
        </div>
      </div>

      <style>{`
        .terminal-tab { display: flex; flex-direction: column; height: 100%; padding: 24px; }

        .terminal-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
        .terminal-title-section { display: flex; align-items: center; gap: 12px; }
        .terminal-icon-green { color: var(--success); }
        .terminal-title { font-size: 24px; font-weight: 600; margin: 0; color: var(--text-primary); }
        .running-badge { display: flex; align-items: center; gap: 6px; padding: 4px 10px; background: rgba(245, 158, 11, 0.15); color: var(--warning); border-radius: 12px; font-size: 12px; }
        .spinning { animation: spin 1s linear infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }
        .terminal-actions { display: flex; gap: 8px; }
        .term-action-btn { width: 36px; height: 36px; border-radius: 8px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); color: var(--text-muted); cursor: pointer; display: flex; align-items: center; justify-content: center; }
        .term-action-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
        .term-action-btn.danger:hover { background: rgba(239, 68, 68, 0.15); color: var(--error); border-color: rgba(239, 68, 68, 0.3); }

        .terminal-output { flex: 1; background: var(--bg-overlay); border: 1px solid var(--border-primary); border-radius: 12px; padding: 16px; overflow-y: auto; font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace; font-size: 13px; cursor: text; }
        .terminal-output::-webkit-scrollbar { width: 6px; }
        .terminal-output::-webkit-scrollbar-thumb { background: var(--scrollbar-thumb); border-radius: 3px; }

        .terminal-welcome { margin-bottom: 24px; }
        .ascii-art { font-size: 9px; line-height: 1.2; margin: 0; color: var(--success); }
        .welcome-text { margin-top: 12px; color: var(--text-muted); font-size: 12px; }
        .welcome-text .hl { color: var(--success); font-weight: 600; }

        .command-entry { margin-bottom: 16px; }
        .command-line { display: flex; gap: 8px; align-items: center; }
        .prompt { color: var(--success); font-weight: 600; }
        .command-text { color: var(--text-primary); }
        .cmd-duration { margin-left: auto; font-size: 11px; color: var(--text-dimmed); }
        .command-output { margin: 8px 0 0 20px; padding: 0; white-space: pre-wrap; line-height: 1.5; }
        .command-output.success { color: var(--text-secondary); }
        .command-output.error { color: var(--error); }
        .command-running { display: flex; align-items: center; gap: 8px; margin: 8px 0 0 20px; color: var(--warning); font-size: 13px; }
        .command-timeout { display: flex; align-items: center; gap: 8px; margin: 8px 0 0 20px; color: var(--warning); font-size: 13px; }

        .input-line { display: flex; align-items: center; gap: 8px; margin-top: 8px; padding-top: 8px; border-top: 1px solid var(--border-primary); }
        .command-input { flex: 1; background: transparent; border: none; color: var(--text-primary); font-family: inherit; font-size: 13px; outline: none; }
        .command-input::placeholder { color: var(--text-faint); }
        .command-input:disabled { opacity: 0.5; }
        .execute-btn { width: 32px; height: 32px; border-radius: 6px; background: rgba(34, 197, 94, 0.15); border: 1px solid rgba(34, 197, 94, 0.3); color: var(--success); cursor: pointer; display: flex; align-items: center; justify-content: center; transition: all 0.15s ease; }
        .execute-btn:hover:not(:disabled) { background: rgba(34, 197, 94, 0.25); }
        .execute-btn:disabled { opacity: 0.3; cursor: not-allowed; }

        .quick-commands { margin-top: 16px; }
        .quick-label { font-size: 12px; color: var(--text-dimmed); display: block; margin-bottom: 8px; }
        .quick-btns { display: flex; gap: 8px; flex-wrap: wrap; }
        .quick-btn { display: flex; align-items: center; gap: 4px; padding: 6px 12px; background: var(--bg-tertiary); border: 1px solid var(--border-primary); border-radius: 6px; color: var(--text-muted); font-size: 12px; font-family: inherit; cursor: pointer; transition: all 0.15s ease; }
        .quick-btn:hover:not(:disabled) { background: var(--bg-hover); color: var(--text-secondary); border-color: var(--border-hover); }
        .quick-btn:disabled { opacity: 0.5; cursor: not-allowed; }
      `}</style>
    </div>
  );
};
