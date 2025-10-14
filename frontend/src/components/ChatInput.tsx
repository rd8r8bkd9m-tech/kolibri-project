import { Keyboard, Paperclip, Plus, RefreshCw, SendHorizontal } from "lucide-react";
import { useId, useMemo } from "react";
import { MODE_OPTIONS, findModeLabel } from "../core/modes";

interface ChatInputProps {
  value: string;
  mode: string;
  isBusy: boolean;
  onChange: (value: string) => void;
  onModeChange: (mode: string) => void;
  onSubmit: () => void;
  onReset: () => void;
}

const MAX_LENGTH = 4000;

const ChatInput = ({ value, mode, isBusy, onChange, onModeChange, onSubmit, onReset }: ChatInputProps) => {
  const textAreaId = useId();
  const trimmedLength = useMemo(() => value.trim().length, [value]);
  const remaining = Math.max(0, MAX_LENGTH - value.length);

  const handleKeyDown = (event: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      if (!isBusy && value.trim()) {
        onSubmit();
      }
    }
  };

  return (
    <div className="flex flex-col gap-4 rounded-3xl border border-border-strong bg-background-input/95 p-6 backdrop-blur">
      <div className="flex flex-wrap items-center justify-between gap-3 text-sm text-text-secondary">
        <div className="flex flex-wrap items-center gap-2">
          <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-primary/15 text-primary">К</div>
          <label htmlFor={textAreaId} className="text-xs uppercase tracking-[0.3em]">
            Режим ядра
          </label>
          <select
            id={textAreaId}
            className="rounded-xl border border-border-strong bg-background-card/80 px-3 py-2 text-xs font-semibold text-text-primary focus:border-primary focus:outline-none"
            value={mode}
            onChange={(event) => onModeChange(event.target.value)}
            disabled={isBusy}
          >
            {MODE_OPTIONS.map((item) => (
              <option key={item.value} value={item.value}>
                {item.label}
              </option>
            ))}
          </select>
          <span className="rounded-lg border border-border-strong bg-background-card/70 px-2 py-1 text-[0.7rem] uppercase tracking-wide text-text-secondary">
            {findModeLabel(mode)}
          </span>
        </div>
        <div className="flex items-center gap-3 text-xs text-text-secondary">
          <span>
            Символов: {trimmedLength} / {MAX_LENGTH}
          </span>
          <button
            type="button"
            onClick={onReset}
            className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-card/80 px-3 py-2 text-xs font-semibold transition-colors hover:text-text-primary disabled:cursor-not-allowed disabled:opacity-60"
            disabled={isBusy}
          >
            <Plus className="h-4 w-4" />
            Новый диалог
          </button>
        </div>
      </div>
      <textarea
        id={`${textAreaId}-textarea`}
        value={value}
        onChange={(event) => {
          if (event.target.value.length <= MAX_LENGTH) {
            onChange(event.target.value);
          }
        }}
        onKeyDown={handleKeyDown}
        placeholder="Сообщение для Колибри"
        className="min-h-[160px] w-full resize-none rounded-2xl border border-border-strong bg-background-card/85 px-4 py-3 text-sm text-text-primary placeholder:text-text-secondary focus:border-primary focus:outline-none"
      />
      <div className="flex flex-wrap items-center justify-between gap-3 text-xs text-text-secondary">
        <div className="flex flex-wrap items-center gap-2">
          <button
            type="button"
            className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-card/80 px-3 py-2 transition-colors hover:text-text-primary"
            disabled={isBusy}
          >
            <Paperclip className="h-4 w-4" />
            Вложить
          </button>
          <button
            type="button"
            onClick={onReset}
            className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-card/80 px-3 py-2 transition-colors hover:text-text-primary"
            disabled={isBusy}
          >
            <RefreshCw className="h-4 w-4" />
            Сбросить
          </button>
          <div className="flex items-center gap-2 rounded-xl border border-border-strong bg-background-card/80 px-3 py-2">
            <Keyboard className="h-4 w-4" />
            <span>Enter — отправить, Shift + Enter — перенос строки</span>
          </div>
        </div>
        <div className="flex items-center gap-3">
          <span className="rounded-lg border border-border-strong bg-background-card/70 px-3 py-1 text-[0.7rem] uppercase tracking-wide text-text-secondary">
            Осталось: {remaining}
          </span>
          <button
            type="button"
            onClick={onSubmit}
            className="flex items-center gap-2 rounded-xl bg-primary px-4 py-2 text-sm font-semibold text-white transition-opacity hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-60"
            disabled={isBusy || !value.trim()}
          >
            <SendHorizontal className="h-4 w-4" />
            Отправить
          </button>
        </div>
      </div>
    </div>
  );
};

export default ChatInput;
