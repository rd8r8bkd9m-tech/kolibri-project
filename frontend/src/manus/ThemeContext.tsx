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
  mode: 'light',
  resolved: 'light',
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

/* --- CSS-переменные для тем (Manus-style: light — основная) --- */
const THEME_VARS: Record<ResolvedTheme, Record<string, string>> = {
  light: {
    '--bg-primary': '#f8f8f7',
    '--bg-secondary': '#ffffff',
    '--bg-tertiary': '#f0f0ef',
    '--bg-card': '#ffffff',
    '--bg-hover': '#ebebea',
    '--bg-input': '#ffffff',
    '--bg-overlay': '#fafaf9',
    '--text-primary': '#1a1a1a',
    '--text-secondary': '#6b6b6b',
    '--text-muted': '#999999',
    '--text-dimmed': '#b3b3b3',
    '--text-faint': '#d4d4d3',
    '--border-primary': '#e5e5e3',
    '--border-hover': '#d4d4d2',
    '--border-accent': 'rgba(26, 26, 26, 0.2)',
    '--accent-primary': '#1a1a1a',
    '--accent-secondary': '#3b82f6',
    '--accent-bg': 'rgba(26, 26, 26, 0.06)',
    '--accent-gradient': 'linear-gradient(135deg, #1a1a1a, #404040)',
    '--accent-gradient-text': 'linear-gradient(135deg, #1a1a1a, #555555)',
    '--success': '#22c55e',
    '--warning': '#f59e0b',
    '--error': '#ef4444',
    '--info': '#3b82f6',
    '--gradient-bg': 'none',
    '--shadow-card': '0 1px 3px rgba(0,0,0,0.06), 0 1px 2px rgba(0,0,0,0.04)',
    '--shadow-elevated': '0 4px 16px rgba(0,0,0,0.08)',
    '--scrollbar-thumb': 'rgba(0,0,0,0.1)',
  },
  dark: {
    '--bg-primary': '#09090b',
    '--bg-secondary': '#18181b',
    '--bg-tertiary': '#27272a',
    '--bg-card': '#1c1c1f',
    '--bg-hover': '#2c2c30',
    '--bg-input': '#1c1c1f',
    '--bg-overlay': '#111113',
    '--text-primary': '#fafafa',
    '--text-secondary': '#a1a1aa',
    '--text-muted': '#71717a',
    '--text-dimmed': '#52525b',
    '--text-faint': '#3f3f46',
    '--border-primary': 'rgba(255,255,255,0.08)',
    '--border-hover': 'rgba(255,255,255,0.12)',
    '--border-accent': 'rgba(129,140,248,0.3)',
    '--accent-primary': '#818cf8',
    '--accent-secondary': '#c084fc',
    '--accent-bg': 'rgba(99,102,241,0.15)',
    '--accent-gradient': 'linear-gradient(135deg, #6366f1, #8b5cf6)',
    '--accent-gradient-text': 'linear-gradient(135deg, #818cf8, #c084fc)',
    '--success': '#22c55e',
    '--warning': '#f59e0b',
    '--error': '#ef4444',
    '--info': '#3b82f6',
    '--gradient-bg': 'none',
    '--shadow-card': '0 1px 3px rgba(0,0,0,0.3)',
    '--shadow-elevated': '0 4px 12px rgba(0,0,0,0.4)',
    '--scrollbar-thumb': 'rgba(255,255,255,0.08)',
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
    return 'light';
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
