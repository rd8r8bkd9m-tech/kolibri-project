/**
 * ThemeContext.tsx
 *
 * Полноценная система тем: светлая, тёмная, системная.
 * Сохраняет выбор в localStorage, реагирует на системные изменения.
 */

import { createContext, useContext, useEffect, useState, useCallback, type ReactNode } from 'react';

export type ThemeMode = 'light' | 'dark' | 'system';
export type ResolvedTheme = 'light' | 'dark';

interface ThemeContextValue {
  mode: ThemeMode;
  resolved: ResolvedTheme;
  setMode: (mode: ThemeMode) => void;
}

const ThemeContext = createContext<ThemeContextValue>({
  mode: 'dark',
  resolved: 'dark',
  setMode: () => {},
});

const STORAGE_KEY = 'kolibri-theme';

function getSystemTheme(): ResolvedTheme {
  if (typeof window !== 'undefined' && window.matchMedia('(prefers-color-scheme: light)').matches) {
    return 'light';
  }
  return 'dark';
}

function resolveTheme(mode: ThemeMode): ResolvedTheme {
  if (mode === 'system') return getSystemTheme();
  return mode;
}

/* --- CSS-переменные для тем --- */
const THEME_VARS: Record<ResolvedTheme, Record<string, string>> = {
  dark: {
    '--bg-primary': '#09090b',
    '--bg-secondary': 'rgba(24, 24, 27, 0.6)',
    '--bg-tertiary': 'rgba(39, 39, 42, 0.5)',
    '--bg-card': 'rgba(39, 39, 42, 0.4)',
    '--bg-hover': 'rgba(63, 63, 70, 0.5)',
    '--bg-input': 'rgba(39, 39, 42, 0.6)',
    '--bg-overlay': 'rgba(9, 9, 11, 0.8)',
    '--text-primary': '#fafafa',
    '--text-secondary': '#a1a1aa',
    '--text-muted': '#71717a',
    '--text-dimmed': '#52525b',
    '--text-faint': '#3f3f46',
    '--border-primary': 'rgba(255, 255, 255, 0.06)',
    '--border-hover': 'rgba(255, 255, 255, 0.1)',
    '--border-accent': 'rgba(99, 102, 241, 0.3)',
    '--accent-primary': '#818cf8',
    '--accent-secondary': '#c084fc',
    '--accent-bg': 'rgba(99, 102, 241, 0.15)',
    '--accent-gradient': 'linear-gradient(135deg, #6366f1, #8b5cf6)',
    '--accent-gradient-text': 'linear-gradient(135deg, #818cf8, #c084fc)',
    '--success': '#22c55e',
    '--warning': '#f59e0b',
    '--error': '#ef4444',
    '--info': '#3b82f6',
    '--gradient-bg': 'radial-gradient(ellipse 80% 50% at 50% -20%, rgba(99, 102, 241, 0.12), transparent), radial-gradient(ellipse 60% 40% at 90% 50%, rgba(139, 92, 246, 0.08), transparent), radial-gradient(ellipse 50% 30% at 10% 90%, rgba(59, 130, 246, 0.06), transparent)',
    '--shadow-card': '0 1px 3px rgba(0, 0, 0, 0.3)',
    '--shadow-elevated': '0 4px 12px rgba(0, 0, 0, 0.4)',
    '--scrollbar-thumb': 'rgba(255, 255, 255, 0.08)',
  },
  light: {
    '--bg-primary': '#ffffff',
    '--bg-secondary': 'rgba(241, 245, 249, 0.8)',
    '--bg-tertiary': 'rgba(241, 245, 249, 0.6)',
    '--bg-card': 'rgba(248, 250, 252, 0.8)',
    '--bg-hover': 'rgba(226, 232, 240, 0.5)',
    '--bg-input': 'rgba(241, 245, 249, 0.8)',
    '--bg-overlay': 'rgba(248, 250, 252, 0.95)',
    '--text-primary': '#0f172a',
    '--text-secondary': '#475569',
    '--text-muted': '#64748b',
    '--text-dimmed': '#94a3b8',
    '--text-faint': '#cbd5e1',
    '--border-primary': 'rgba(0, 0, 0, 0.08)',
    '--border-hover': 'rgba(0, 0, 0, 0.15)',
    '--border-accent': 'rgba(99, 102, 241, 0.4)',
    '--accent-primary': '#6366f1',
    '--accent-secondary': '#8b5cf6',
    '--accent-bg': 'rgba(99, 102, 241, 0.1)',
    '--accent-gradient': 'linear-gradient(135deg, #6366f1, #8b5cf6)',
    '--accent-gradient-text': 'linear-gradient(135deg, #4f46e5, #7c3aed)',
    '--success': '#16a34a',
    '--warning': '#d97706',
    '--error': '#dc2626',
    '--info': '#2563eb',
    '--gradient-bg': 'radial-gradient(ellipse 80% 50% at 50% -20%, rgba(99, 102, 241, 0.06), transparent), radial-gradient(ellipse 60% 40% at 90% 50%, rgba(139, 92, 246, 0.04), transparent), radial-gradient(ellipse 50% 30% at 10% 90%, rgba(59, 130, 246, 0.03), transparent)',
    '--shadow-card': '0 1px 3px rgba(0, 0, 0, 0.08)',
    '--shadow-elevated': '0 4px 12px rgba(0, 0, 0, 0.1)',
    '--scrollbar-thumb': 'rgba(0, 0, 0, 0.15)',
  },
};

function applyThemeVars(theme: ResolvedTheme) {
  const vars = THEME_VARS[theme];
  const root = document.documentElement;
  for (const [key, value] of Object.entries(vars)) {
    root.style.setProperty(key, value);
  }
  root.setAttribute('data-theme', theme);
}

export const ThemeProvider = ({ children }: { children: ReactNode }) => {
  const [mode, setModeState] = useState<ThemeMode>(() => {
    try {
      const stored = localStorage.getItem(STORAGE_KEY);
      if (stored === 'light' || stored === 'dark' || stored === 'system') return stored;
    } catch {}
    return 'dark';
  });

  const [resolved, setResolved] = useState<ResolvedTheme>(() => resolveTheme(mode));

  const setMode = useCallback((newMode: ThemeMode) => {
    setModeState(newMode);
    try {
      localStorage.setItem(STORAGE_KEY, newMode);
    } catch {}
  }, []);

  // Применяем тему при изменении mode или системных настроек
  useEffect(() => {
    const newResolved = resolveTheme(mode);
    setResolved(newResolved);
    applyThemeVars(newResolved);
  }, [mode]);

  // Слушаем изменения системной темы
  useEffect(() => {
    if (mode !== 'system') return;

    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    const handler = () => {
      const newResolved = resolveTheme('system');
      setResolved(newResolved);
      applyThemeVars(newResolved);
    };
    mq.addEventListener('change', handler);
    return () => mq.removeEventListener('change', handler);
  }, [mode]);

  // Применяем при монтировании
  useEffect(() => {
    applyThemeVars(resolved);
  }, []);

  return (
    <ThemeContext.Provider value={{ mode, resolved, setMode }}>
      {children}
    </ThemeContext.Provider>
  );
};

export const useTheme = () => useContext(ThemeContext);
