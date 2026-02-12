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

/* --- CSS-переменные для тем (Manus-style: light — основная) --- */
const THEME_VARS: Record<ResolvedTheme, Record<string, string>> = {
  light: {
    '--bg-primary': '#f6f8f8',
    '--bg-secondary': '#ffffff',
    '--bg-tertiary': '#eef2f2',
    '--bg-card': '#ffffff',
    '--bg-hover': '#e8efef',
    '--bg-input': '#ffffff',
    '--bg-overlay': '#f5f8f8',
    '--text-primary': '#0f1719',
    '--text-secondary': '#53636a',
    '--text-muted': '#73818a',
    '--text-dimmed': '#9ca8af',
    '--text-faint': '#c5ced3',
    '--text-tertiary': '#9ca8af',
    '--border-primary': '#d9e3e4',
    '--border-hover': '#c5d3d6',
    '--border-accent': 'rgba(27, 143, 135, 0.35)',
    '--accent-primary': '#1b8f87',
    '--accent-secondary': '#23b9ae',
    '--accent-bg': 'rgba(27, 143, 135, 0.1)',
    '--accent-gradient': 'linear-gradient(135deg, #1b8f87, #23b9ae)',
    '--accent-gradient-text': 'linear-gradient(135deg, #14645f, #1b8f87)',
    '--success': '#22c55e',
    '--warning': '#f59e0b',
    '--error': '#ef4444',
    '--info': '#0ea5b7',
    '--gradient-bg': 'none',
    '--shadow-card': 'none',
    '--shadow-elevated': 'none',
    '--scrollbar-thumb': 'rgba(0,0,0,0.1)',
  },
  dark: {
    '--bg-primary': '#030405',
    '--bg-secondary': '#070b0d',
    '--bg-tertiary': '#0d1317',
    '--bg-card': '#0a0f13',
    '--bg-hover': '#111920',
    '--bg-input': '#0c1318',
    '--bg-overlay': '#080d11',
    '--text-primary': '#eaf0f4',
    '--text-secondary': '#a6b3bd',
    '--text-muted': '#7d8b95',
    '--text-dimmed': '#64717a',
    '--text-faint': '#44505a',
    '--text-tertiary': '#64717a',
    '--border-primary': 'rgba(232,245,255,0.1)',
    '--border-hover': 'rgba(232,245,255,0.16)',
    '--border-accent': 'rgba(53,216,203,0.4)',
    '--accent-primary': '#35d8cb',
    '--accent-secondary': '#4de0d4',
    '--accent-bg': 'rgba(53,216,203,0.14)',
    '--accent-gradient': 'linear-gradient(135deg, #1c9188, #35d8cb)',
    '--accent-gradient-text': 'linear-gradient(135deg, #35d8cb, #79e8e0)',
    '--success': '#22c55e',
    '--warning': '#f59e0b',
    '--error': '#ef4444',
    '--info': '#22d3ee',
    '--gradient-bg': 'none',
    '--shadow-card': 'none',
    '--shadow-elevated': 'none',
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
