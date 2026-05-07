/**
 * ThemeContext.tsx
 *
 * Полноценная система тем: светлая, тёмная, системная.
 * Сохраняет выбор в localStorage, реагирует на системные изменения.
 */

/* eslint-disable react-refresh/only-export-components */

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
    '--bg-primary': '#f3f7fb',
    '--bg-secondary': '#ffffff',
    '--bg-tertiary': '#ebf1f7',
    '--bg-card': '#ffffff',
    '--bg-hover': '#e5edf6',
    '--bg-input': '#ffffff',
    '--bg-overlay': '#f7fafe',
    '--text-primary': '#13232f',
    '--text-secondary': '#4c6373',
    '--text-muted': '#6e8190',
    '--text-dimmed': '#8ea0ad',
    '--text-faint': '#c7d2db',
    '--text-tertiary': '#8ea0ad',
    '--border-primary': '#d8e2eb',
    '--border-hover': '#c5d2dc',
    '--border-accent': 'rgba(0, 163, 160, 0.35)',
    '--accent-primary': '#008f95',
    '--accent-secondary': '#00b8b3',
    '--accent-bg': 'rgba(0, 163, 160, 0.1)',
    '--brand-primary': '#0e8fa0',
    '--brand-secondary': '#2363d3',
    '--brand-glow': 'rgba(35, 99, 211, 0.18)',
    '--accent-gradient': 'linear-gradient(135deg, #007f85, #00b8b3)',
    '--accent-gradient-text': 'linear-gradient(135deg, #066574, #008f95)',
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
    '--bg-primary': '#05080c',
    '--bg-secondary': '#0a1017',
    '--bg-tertiary': '#101923',
    '--bg-card': '#0d141d',
    '--bg-hover': '#151f2b',
    '--bg-input': '#111a24',
    '--bg-overlay': '#0c121a',
    '--text-primary': '#e8f0f7',
    '--text-secondary': '#b2c0cd',
    '--text-muted': '#8b9ba8',
    '--text-dimmed': '#6e7d89',
    '--text-faint': '#4d5b67',
    '--text-tertiary': '#6e7d89',
    '--border-primary': 'rgba(220, 235, 250, 0.12)',
    '--border-hover': 'rgba(220, 235, 250, 0.2)',
    '--border-accent': 'rgba(0, 210, 201, 0.44)',
    '--accent-primary': '#19c8c5',
    '--accent-secondary': '#31e5db',
    '--accent-bg': 'rgba(0, 210, 201, 0.14)',
    '--brand-primary': '#35ddd3',
    '--brand-secondary': '#5b8eff',
    '--brand-glow': 'rgba(91, 142, 255, 0.22)',
    '--accent-gradient': 'linear-gradient(135deg, #0d8f97, #19c8c5)',
    '--accent-gradient-text': 'linear-gradient(135deg, #19c8c5, #73efe8)',
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
    } catch {
      // ignore storage errors
    }
    return 'dark';
  });

  const [resolved, setResolved] = useState<ResolvedTheme>(() => resolveTheme(mode));

  const setMode = useCallback((newMode: ThemeMode) => {
    setModeState(newMode);
    try {
      localStorage.setItem(STORAGE_KEY, newMode);
    } catch {
      // ignore storage errors
    }
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

  return (
    <ThemeContext.Provider value={{ mode, resolved, setMode }}>
      {children}
    </ThemeContext.Provider>
  );
};

export const useTheme = () => useContext(ThemeContext);
