import { useEffect, useMemo, useState } from 'react';
import {
  ArrowLeft,
  ChevronRight,
  Globe,
  Moon,
  Palette,
  Shield,
  SlidersHorizontal,
  Smile,
  Sun,
  User,
  X,
} from 'lucide-react';
import { useTheme, type ThemeMode } from '../ThemeContext';

interface SettingsTabProps {
  onClose?: () => void;
}

type SettingsView = 'root' | 'appearance' | 'widget' | 'info' | 'profile';

interface ProfileState {
  firstName: string;
  lastName: string;
  birthYear: number;
  email: string;
}

const PROFILE_STORAGE_KEY = 'kolibri-profile-v2';
const TEXT_SCALE_KEY = 'kolibri-text-scale-v1';

const DEFAULT_PROFILE: ProfileState = {
  firstName: 'Vladislav',
  lastName: 'Kochurov',
  birthYear: 1978,
  email: 'nz79yxzkcm@privaterelay.appleid.com',
};

const loadProfile = (): ProfileState => {
  try {
    const raw = localStorage.getItem(PROFILE_STORAGE_KEY);
    if (!raw) {
      return DEFAULT_PROFILE;
    }
    const parsed = JSON.parse(raw) as Partial<ProfileState>;
    return {
      firstName: parsed.firstName || DEFAULT_PROFILE.firstName,
      lastName: parsed.lastName || DEFAULT_PROFILE.lastName,
      birthYear: typeof parsed.birthYear === 'number' ? parsed.birthYear : DEFAULT_PROFILE.birthYear,
      email: parsed.email || DEFAULT_PROFILE.email,
    };
  } catch {
    return DEFAULT_PROFILE;
  }
};

const loadTextScale = (): number => {
  try {
    const raw = localStorage.getItem(TEXT_SCALE_KEY);
    const value = raw ? Number(raw) : 1;
    if (!Number.isFinite(value)) {
      return 1;
    }
    return Math.min(1.3, Math.max(0.85, value));
  } catch {
    return 1;
  }
};

export const SettingsTab = ({ onClose }: SettingsTabProps) => {
  const { mode, setMode } = useTheme();
  const [view, setView] = useState<SettingsView>('root');
  const [profile, setProfile] = useState<ProfileState>(loadProfile);
  const [textScale, setTextScale] = useState(loadTextScale);
  const [hapticsEnabled, setHapticsEnabled] = useState(true);
  const [childModeEnabled, setChildModeEnabled] = useState(false);
  const [language, setLanguage] = useState<'ru' | 'en'>('ru');

  useEffect(() => {
    try {
      localStorage.setItem(PROFILE_STORAGE_KEY, JSON.stringify(profile));
    } catch {
      // ignore storage errors
    }
  }, [profile]);

  useEffect(() => {
    const value = Math.min(1.3, Math.max(0.85, textScale));
    document.documentElement.style.setProperty('--kolibri-font-scale', value.toFixed(2));
    try {
      localStorage.setItem(TEXT_SCALE_KEY, value.toString());
    } catch {
      // ignore storage errors
    }
  }, [textScale]);

  const yearOptions = useMemo(() => {
    const currentYear = new Date().getFullYear();
    return Array.from({ length: 70 }, (_, index) => currentYear - 18 - index);
  }, []);

  const closeOrBackToRoot = () => {
    if (view !== 'root') {
      setView('root');
      return;
    }
    onClose?.();
  };

  const renderRoot = () => {
    return (
      <>
        <header className="kol-settings-header">
          <h1>Настройки</h1>
          <button type="button" className="kol-settings-circle" onClick={closeOrBackToRoot} aria-label="Закрыть">
            <X size={34} />
          </button>
        </header>

        <button type="button" className="kol-settings-profile-card" onClick={() => setView('profile')}>
          <span className="kol-settings-profile-avatar">
            <User size={44} />
          </span>
          <span className="kol-settings-profile-info">
            <strong>{profile.firstName} {profile.lastName}</strong>
            <span>{profile.email}</span>
          </span>
          <ChevronRight size={32} />
        </button>

        <section className="kol-settings-section">
          <h3>Подписка</h3>
          <div className="kol-settings-upgrade">
            <div className="kol-settings-upgrade-copy">
              <strong>Попробуй Колибри Pro бесплатно</strong>
              <span>Улучшить для увеличенные лимиты</span>
            </div>
            <button type="button">Попробовать</button>
          </div>
        </section>

        <section className="kol-settings-section">
          <h3>Общие</h3>
          <div className="kol-settings-list-card">
            <button type="button" className="kol-settings-row" onClick={() => setView('appearance')}>
              <span><Palette size={26} /> Оформление</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row">
              <span><SlidersHorizontal size={26} /> Персонализировать Колибри</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row" onClick={() => setHapticsEnabled((value) => !value)}>
              <span><Smile size={26} /> Тактильный отклик</span>
              <strong className="kol-settings-row-value">{hapticsEnabled ? 'ON' : 'OFF'}</strong>
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row" onClick={() => setView('widget')}>
              <span><Palette size={26} /> Виджет</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row" onClick={() => setLanguage((value) => (value === 'ru' ? 'en' : 'ru'))}>
              <span><Globe size={26} /> Язык приложения</span>
              <strong className="kol-settings-row-value">{language === 'ru' ? 'русский' : 'english'}</strong>
            </button>
          </div>
        </section>

        <section className="kol-settings-section">
          <button type="button" className="kol-settings-list-card kol-settings-row" onClick={() => setChildModeEnabled((value) => !value)}>
            <span><Shield size={26} /> Детский режим</span>
            <strong className="kol-settings-row-value">{childModeEnabled ? 'ON' : 'OFF'}</strong>
          </button>
        </section>

        <section className="kol-settings-section">
          <button type="button" className="kol-settings-list-card kol-settings-row" onClick={() => setView('info')}>
            <span><Shield size={26} /> Данные и инфо</span>
            <ChevronRight size={24} />
          </button>
        </section>
      </>
    );
  };

  const renderAppearance = () => {
    const themeButtons: Array<{ id: ThemeMode; label: string; icon: JSX.Element }> = [
      { id: 'system', label: 'Система', icon: <Palette size={28} /> },
      { id: 'light', label: 'День', icon: <Sun size={28} /> },
      { id: 'dark', label: 'Ночь', icon: <Moon size={28} /> },
    ];

    return (
      <>
        <header className="kol-settings-header is-subpage">
          <button type="button" className="kol-settings-circle" onClick={() => setView('root')} aria-label="Назад">
            <ArrowLeft size={34} />
          </button>
          <h1>Оформление</h1>
          <span className="kol-settings-spacer" aria-hidden="true" />
        </header>

        <div className="kol-settings-theme-grid">
          {themeButtons.map((item) => (
            <button
              key={item.id}
              type="button"
              className={`kol-settings-theme-card ${mode === item.id ? 'is-active' : ''}`}
              onClick={() => setMode(item.id)}
            >
              {item.icon}
              <span>{item.label}</span>
            </button>
          ))}
        </div>

        <section className="kol-settings-section">
          <h3>Размер текста</h3>
          <div className="kol-settings-list-card kol-settings-font-card">
            <div className="kol-settings-font-range">
              <span>A</span>
              <input
                type="range"
                min={0.85}
                max={1.3}
                step={0.01}
                value={textScale}
                onChange={(event) => setTextScale(Number(event.target.value))}
              />
              <span className="is-right">A</span>
            </div>
            <div className="kol-settings-divider" />
            <div className="kol-settings-font-preview">
              <div className="kol-settings-preview-user">Расскажи мне о Вселенной.</div>
              <p>
                Вселенная это всё, что существует: пространство, время, материя, энергия и законы,
                по которым всё работает.
              </p>
              <button type="button" onClick={() => setTextScale(1)}>Сбросить</button>
            </div>
          </div>
        </section>
      </>
    );
  };

  const renderWidget = () => {
    return (
      <>
        <header className="kol-settings-header is-subpage">
          <button type="button" className="kol-settings-circle" onClick={() => setView('root')} aria-label="Назад">
            <ArrowLeft size={34} />
          </button>
          <h1>Виджет</h1>
          <span className="kol-settings-spacer" aria-hidden="true" />
        </header>

        <section className="kol-settings-list-card kol-settings-widget-card">
          <h3>Виджет главного экрана</h3>
          <div className="kol-settings-widget-preview" aria-hidden="true" />
          <p>From the Home Screen, touch and hold an empty area until the apps jiggle.</p>
          <div className="kol-settings-widget-dots">
            <span className="is-active" />
            <span />
            <span />
            <span />
          </div>
        </section>
      </>
    );
  };

  const renderInfo = () => {
    return (
      <>
        <header className="kol-settings-header">
          <h1>Настройки</h1>
          <button type="button" className="kol-settings-circle" onClick={closeOrBackToRoot} aria-label="Закрыть">
            <X size={34} />
          </button>
        </header>

        <section className="kol-settings-section">
          <h3>Данные и инфо</h3>
          <div className="kol-settings-list-card">
            <button type="button" className="kol-settings-row">
              <span>Общие разговоры</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row">
              <span>Управление данными</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row">
              <span>Недавно удалённые</span>
              <ChevronRight size={24} />
            </button>
          </div>
        </section>

        <section className="kol-settings-section">
          <div className="kol-settings-list-card">
            <button type="button" className="kol-settings-row">
              <span>Условия использования</span>
              <ChevronRight size={24} />
            </button>
            <div className="kol-settings-divider" />
            <button type="button" className="kol-settings-row">
              <span>Политика конфиденциальности</span>
              <ChevronRight size={24} />
            </button>
          </div>
        </section>

        <section className="kol-settings-section">
          <button type="button" className="kol-settings-list-card kol-settings-row">
            <span>Сообщить о проблеме</span>
            <ChevronRight size={24} />
          </button>
        </section>

        <section className="kol-settings-section">
          <button type="button" className="kol-settings-logout">Выйти</button>
        </section>

        <footer className="kol-settings-version">xI · ВЕРСИЯ 1.3.38 (СБОРКА 2713)</footer>
      </>
    );
  };

  const renderProfile = () => {
    return (
      <>
        <header className="kol-settings-header is-subpage">
          <button type="button" className="kol-settings-circle" onClick={() => setView('root')} aria-label="Назад">
            <ArrowLeft size={34} />
          </button>
          <h1>Профиль</h1>
          <button type="button" className="kol-settings-save">Сохранить</button>
        </header>

        <section className="kol-settings-profile-edit">
          <span className="kol-settings-profile-avatar is-large">
            <User size={58} />
          </span>
          <button type="button" className="kol-settings-edit-btn">Редактировать</button>
        </section>

        <section className="kol-settings-list-card kol-settings-profile-fields">
          <label>
            <input
              type="text"
              value={profile.firstName}
              onChange={(event) => setProfile((prev) => ({ ...prev, firstName: event.target.value }))}
              placeholder="Имя"
            />
          </label>
          <div className="kol-settings-divider" />
          <label>
            <input
              type="text"
              value={profile.lastName}
              onChange={(event) => setProfile((prev) => ({ ...prev, lastName: event.target.value }))}
              placeholder="Фамилия"
            />
          </label>
        </section>

        <section className="kol-settings-list-card kol-settings-row">
          <span>Год рождения</span>
          <label className="kol-settings-year-select">
            <select
              value={profile.birthYear}
              onChange={(event) => setProfile((prev) => ({ ...prev, birthYear: Number(event.target.value) }))}
            >
              {yearOptions.map((year) => (
                <option key={year} value={year}>{year}</option>
              ))}
            </select>
          </label>
        </section>

        <section className="kol-settings-list-card kol-settings-email-card">
          <div className="kol-settings-row">
            <span>Электронная почта</span>
            <strong className="kol-settings-row-value">{profile.email}</strong>
          </div>
          <div className="kol-settings-divider" />
          <button type="button" className="kol-settings-row">
            <span>Управление аккаунтом</span>
            <ChevronRight size={24} />
          </button>
        </section>

        <p className="kol-settings-apple-note">Вход выполнен с аккаунтом Apple.</p>
      </>
    );
  };

  return (
    <div className="kol-settings-page">
      <div className="kol-settings-sheet">
        {view === 'root' && renderRoot()}
        {view === 'appearance' && renderAppearance()}
        {view === 'widget' && renderWidget()}
        {view === 'info' && renderInfo()}
        {view === 'profile' && renderProfile()}
      </div>

      <style>{`
        .kol-settings-page {
          height: 100%;
          overflow: auto;
          overflow-x: hidden;
          background: var(--bg-primary);
          color: var(--text-primary);
          padding: 18px 14px calc(24px + env(safe-area-inset-bottom));
        }

        .kol-settings-sheet {
          min-height: calc(100% - 2px);
          border-radius: 34px;
          border: 1px solid var(--border-primary);
          background: var(--bg-secondary);
          padding: 16px;
          display: grid;
          gap: 18px;
          max-width: 100%;
        }

        .kol-settings-header {
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 12px;
          min-height: 84px;
        }

        .kol-settings-header h1 {
          margin: 0;
          font-size: clamp(32px, 8vw, 64px);
          line-height: 1.08;
          letter-spacing: -0.02em;
        }

        .kol-settings-header.is-subpage {
          justify-content: space-between;
        }

        .kol-settings-circle {
          width: clamp(64px, 18vw, 84px);
          height: clamp(64px, 18vw, 84px);
          border-radius: 999px;
          border: 2px solid var(--border-primary);
          background: var(--bg-tertiary);
          color: var(--text-primary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
        }

        .kol-settings-spacer {
          width: clamp(64px, 18vw, 84px);
          height: clamp(64px, 18vw, 84px);
        }

        .kol-settings-save {
          border: 2px solid var(--border-primary);
          border-radius: 999px;
          min-height: clamp(64px, 18vw, 84px);
          padding: 0 clamp(16px, 4vw, 24px);
          background: transparent;
          color: var(--text-primary);
          font-size: clamp(28px, 6.5vw, 54px);
          font-weight: 700;
        }

        .kol-settings-section {
          display: grid;
          gap: 10px;
        }

        .kol-settings-section h3 {
          margin: 0;
          font-size: clamp(22px, 6vw, 50px);
          color: var(--text-muted);
          letter-spacing: -0.01em;
        }

        .kol-settings-profile-card,
        .kol-settings-list-card {
          border-radius: 30px;
          border: 1px solid var(--border-primary);
          background: var(--bg-card);
          color: var(--text-primary);
          width: 100%;
        }

        .kol-settings-profile-card {
          min-height: 148px;
          padding: 20px;
          display: flex;
          align-items: center;
          gap: 16px;
          text-align: left;
        }

        .kol-settings-profile-avatar {
          width: clamp(74px, 20vw, 110px);
          height: clamp(74px, 20vw, 110px);
          border-radius: 50%;
          border: 1px solid var(--border-primary);
          background: var(--bg-tertiary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          color: var(--text-secondary);
          flex-shrink: 0;
        }

        .kol-settings-profile-avatar.is-large {
          width: clamp(130px, 34vw, 190px);
          height: clamp(130px, 34vw, 190px);
        }

        .kol-settings-profile-info {
          flex: 1;
          min-width: 0;
          display: grid;
          gap: 4px;
        }

        .kol-settings-profile-info strong {
          font-size: clamp(28px, 6.7vw, 56px);
          line-height: 1.1;
        }

        .kol-settings-profile-info span {
          font-size: clamp(20px, 5vw, 44px);
          line-height: 1.2;
          color: var(--text-muted);
          word-break: break-word;
        }

        .kol-settings-upgrade {
          min-height: 162px;
          border-radius: 34px;
          background:
            radial-gradient(circle at 24% 25%, rgba(255, 255, 255, 0.16), transparent 38%),
            linear-gradient(130deg, #2f57d5 0%, #1f45c7 34%, #0f49ff 100%);
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 16px;
          padding: 20px;
        }

        .kol-settings-upgrade-copy {
          display: grid;
          gap: 6px;
          max-width: 68%;
        }

        .kol-settings-upgrade-copy strong {
          font-size: clamp(30px, 7.4vw, 58px);
          line-height: 1.02;
        }

        .kol-settings-upgrade-copy span {
          font-size: clamp(16px, 4vw, 30px);
          color: rgba(255, 255, 255, 0.82);
        }

        .kol-settings-upgrade button {
          min-height: 88px;
          border-radius: 999px;
          border: 2px solid rgba(122, 216, 255, 0.88);
          background: rgba(4, 92, 255, 0.9);
          color: #fff;
          padding: 0 24px;
          font-size: clamp(24px, 6vw, 48px);
          font-weight: 700;
        }

        .kol-settings-row {
          width: 100%;
          min-height: clamp(70px, 17vw, 112px);
          padding: 0 clamp(16px, 4vw, 28px);
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 16px;
          color: var(--text-primary);
          background: transparent;
          border: 0;
          text-align: left;
        }

        .kol-settings-row span {
          display: inline-flex;
          align-items: center;
          gap: 10px;
          font-size: clamp(26px, 7vw, 58px);
          line-height: 1.1;
        }

        .kol-settings-row-value {
          font-size: clamp(24px, 6vw, 52px);
          font-weight: 600;
          color: var(--text-muted);
        }

        .kol-settings-divider {
          height: 1px;
          background: var(--border-primary);
          margin: 0 clamp(16px, 4vw, 28px);
        }

        .kol-settings-theme-grid {
          display: grid;
          grid-template-columns: repeat(3, minmax(0, 1fr));
          gap: 12px;
        }

        .kol-settings-theme-card {
          min-height: clamp(150px, 34vw, 230px);
          border-radius: 22px;
          border: 1px solid var(--border-primary);
          background: var(--bg-tertiary);
          color: var(--text-secondary);
          display: grid;
          place-items: center;
          gap: 10px;
          padding: 14px;
        }

        .kol-settings-theme-card span {
          font-size: clamp(22px, 5.6vw, 46px);
          font-weight: 700;
        }

        .kol-settings-theme-card.is-active {
          background: var(--accent-bg);
          color: var(--text-primary);
          border-color: var(--border-accent);
        }

        .kol-settings-font-card {
          padding: 0;
          overflow: hidden;
        }

        .kol-settings-font-range {
          min-height: 102px;
          display: grid;
          grid-template-columns: 48px 1fr 48px;
          align-items: center;
          gap: 8px;
          padding: 0 20px;
        }

        .kol-settings-font-range span {
          font-size: clamp(30px, 7vw, 56px);
          color: var(--text-secondary);
          text-align: center;
        }

        .kol-settings-font-range span.is-right {
          transform: scale(1.25);
        }

        .kol-settings-font-range input {
          width: 100%;
          accent-color: var(--accent-primary);
        }

        .kol-settings-font-preview {
          padding: 18px;
          display: grid;
          gap: 12px;
        }

        .kol-settings-preview-user {
          width: fit-content;
          margin-left: auto;
          border-radius: 999px;
          background: var(--bg-tertiary);
          padding: 10px 14px;
          font-size: clamp(20px, 4.8vw, 36px);
        }

        .kol-settings-font-preview p {
          margin: 0;
          font-size: clamp(24px, 6vw, 46px);
          line-height: 1.24;
        }

        .kol-settings-font-preview button {
          justify-self: center;
          border: 2px solid var(--border-primary);
          border-radius: 999px;
          min-height: 68px;
          padding: 0 24px;
          color: var(--text-primary);
          background: transparent;
          font-size: clamp(24px, 5.8vw, 42px);
          font-weight: 700;
        }

        .kol-settings-widget-card {
          padding: 22px;
          text-align: center;
          display: grid;
          gap: 16px;
        }

        .kol-settings-widget-card h3 {
          margin: 0;
          font-size: clamp(32px, 7.6vw, 56px);
        }

        .kol-settings-widget-preview {
          width: min(100%, 680px);
          aspect-ratio: 0.78;
          margin: 0 auto;
          border-radius: 28px;
          background:
            radial-gradient(circle at 20% 82%, rgba(255, 255, 255, 0.06), transparent 40%),
            linear-gradient(180deg, rgba(255, 255, 255, 0.09), rgba(255, 255, 255, 0.02));
          border: 1px solid rgba(255, 255, 255, 0.08);
        }

        .kol-settings-widget-card p {
          margin: 0;
          font-size: clamp(24px, 6vw, 44px);
          line-height: 1.2;
          color: var(--text-secondary);
        }

        .kol-settings-widget-dots {
          display: inline-flex;
          gap: 10px;
          justify-self: center;
        }

        .kol-settings-widget-dots span {
          width: 10px;
          height: 10px;
          border-radius: 50%;
          background: var(--text-dimmed);
        }

        .kol-settings-widget-dots span.is-active {
          background: var(--text-primary);
          transform: scale(1.2);
        }

        .kol-settings-version {
          margin-top: 8px;
          text-align: center;
          font-size: clamp(16px, 4vw, 28px);
          color: var(--text-dimmed);
        }

        .kol-settings-logout {
          width: 100%;
          min-height: 92px;
          border-radius: 26px;
          border: 1px solid var(--border-primary);
          background: var(--bg-card);
          color: var(--error);
          font-size: clamp(28px, 6vw, 48px);
          font-weight: 700;
        }

        .kol-settings-profile-edit {
          display: grid;
          justify-items: center;
          gap: 16px;
        }

        .kol-settings-edit-btn {
          border: 2px solid var(--border-primary);
          min-height: 74px;
          border-radius: 999px;
          padding: 0 22px;
          color: var(--text-primary);
          background: transparent;
          font-size: clamp(24px, 5.6vw, 42px);
          font-weight: 700;
        }

        .kol-settings-profile-fields input {
          width: 100%;
          border: 0;
          background: transparent;
          color: var(--text-primary);
          font-size: clamp(30px, 7vw, 56px);
          padding: 16px clamp(16px, 4vw, 28px);
          outline: none;
        }

        .kol-settings-year-select {
          min-width: clamp(124px, 30vw, 220px);
          height: clamp(56px, 15vw, 86px);
          border-radius: 999px;
          border: 1px solid var(--border-primary);
          background: var(--bg-tertiary);
          display: inline-flex;
          align-items: center;
          justify-content: center;
          padding: 0 12px;
        }

        .kol-settings-year-select select {
          border: 0;
          outline: 0;
          background: transparent;
          color: var(--text-primary);
          font-size: clamp(24px, 6vw, 46px);
          font-weight: 600;
        }

        .kol-settings-email-card {
          overflow: hidden;
        }

        .kol-settings-apple-note {
          margin: 2px 8px 0;
          font-size: clamp(20px, 4.8vw, 38px);
          color: var(--text-muted);
        }

        @media (max-width: 900px) {
          .kol-settings-header {
            min-height: 62px;
          }

          .kol-settings-header h1 {
            font-size: clamp(24px, 6.8vw, 34px);
          }

          .kol-settings-circle,
          .kol-settings-spacer,
          .kol-settings-save {
            width: clamp(46px, 13vw, 58px);
            height: clamp(46px, 13vw, 58px);
            min-height: clamp(46px, 13vw, 58px);
          }

          .kol-settings-save {
            font-size: clamp(14px, 4.2vw, 18px);
            padding: 0 12px;
          }

          .kol-settings-section h3 {
            font-size: clamp(16px, 4.6vw, 22px);
          }

          .kol-settings-profile-card {
            min-height: 94px;
            border-radius: 18px;
            padding: 14px;
            gap: 10px;
          }

          .kol-settings-profile-info strong {
            font-size: clamp(18px, 5vw, 24px);
          }

          .kol-settings-profile-info span {
            font-size: clamp(13px, 3.8vw, 16px);
          }

          .kol-settings-upgrade {
            min-height: 104px;
            border-radius: 18px;
            padding: 14px;
          }

          .kol-settings-upgrade-copy strong {
            font-size: clamp(18px, 5.2vw, 24px);
          }

          .kol-settings-upgrade-copy span {
            font-size: clamp(12px, 3.6vw, 14px);
          }

          .kol-settings-upgrade button {
            min-height: 46px;
            font-size: clamp(14px, 3.8vw, 16px);
            border-width: 1px;
            padding: 0 12px;
          }

          .kol-settings-row {
            min-height: 54px;
            padding: 0 14px;
            gap: 8px;
          }

          .kol-settings-row span {
            font-size: clamp(15px, 4.2vw, 18px);
            line-height: 1.2;
            min-width: 0;
            flex: 1;
          }

          .kol-settings-row-value {
            font-size: clamp(13px, 3.8vw, 16px);
            flex-shrink: 0;
          }

          .kol-settings-theme-card {
            min-height: 92px;
            border-radius: 16px;
            gap: 8px;
          }

          .kol-settings-theme-card span {
            font-size: clamp(14px, 4vw, 17px);
          }

          .kol-settings-font-range {
            min-height: 62px;
            grid-template-columns: 26px 1fr 26px;
            padding: 0 12px;
          }

          .kol-settings-font-range span {
            font-size: clamp(13px, 3.8vw, 16px);
          }

          .kol-settings-preview-user {
            font-size: clamp(13px, 3.8vw, 16px);
          }

          .kol-settings-font-preview p {
            font-size: clamp(15px, 4.3vw, 19px);
            line-height: 1.3;
          }

          .kol-settings-font-preview button {
            min-height: 44px;
            font-size: clamp(14px, 4vw, 17px);
            border-width: 1px;
          }

          .kol-settings-widget-card h3 {
            font-size: clamp(19px, 5.2vw, 24px);
          }

          .kol-settings-widget-card p {
            font-size: clamp(14px, 4.1vw, 17px);
            line-height: 1.35;
          }

          .kol-settings-logout {
            min-height: 56px;
            border-radius: 16px;
            font-size: clamp(16px, 4.6vw, 20px);
          }

          .kol-settings-edit-btn {
            min-height: 48px;
            font-size: clamp(14px, 4vw, 17px);
            border-width: 1px;
          }

          .kol-settings-profile-fields input {
            font-size: clamp(17px, 4.8vw, 22px);
            padding: 12px 14px;
          }

          .kol-settings-year-select {
            height: 44px;
            min-width: 112px;
            border-radius: 12px;
            padding: 0 8px;
          }

          .kol-settings-year-select select {
            font-size: clamp(14px, 4vw, 17px);
          }

          .kol-settings-apple-note,
          .kol-settings-version {
            font-size: clamp(12px, 3.6vw, 14px);
          }
        }

        @media (min-width: 901px) {
          .kol-settings-page {
            padding: 24px;
            background: transparent;
          }

          .kol-settings-sheet {
            max-width: 920px;
            margin: 0 auto;
            border-radius: 24px;
            padding: 18px;
            gap: 14px;
            background: var(--bg-secondary);
          }

          .kol-settings-header {
            min-height: 56px;
          }

          .kol-settings-header h1 {
            font-size: 38px;
          }

          .kol-settings-circle,
          .kol-settings-spacer,
          .kol-settings-save {
            width: 56px;
            height: 56px;
            min-height: 56px;
            font-size: 16px;
            border-width: 1px;
          }

          .kol-settings-save {
            width: auto;
            min-width: 120px;
            padding: 0 18px;
          }

          .kol-settings-section h3 {
            font-size: 26px;
          }

          .kol-settings-profile-card {
            min-height: 104px;
            border-radius: 18px;
          }

          .kol-settings-profile-info strong {
            font-size: 28px;
          }

          .kol-settings-profile-info span {
            font-size: 20px;
          }

          .kol-settings-upgrade {
            min-height: 116px;
            border-radius: 20px;
          }

          .kol-settings-upgrade-copy strong {
            font-size: 36px;
          }

          .kol-settings-upgrade-copy span {
            font-size: 18px;
          }

          .kol-settings-upgrade button {
            min-height: 56px;
            font-size: 24px;
            border-width: 1px;
          }

          .kol-settings-row {
            min-height: 66px;
            padding: 0 18px;
          }

          .kol-settings-row span,
          .kol-settings-row-value {
            font-size: 27px;
          }

          .kol-settings-theme-card {
            min-height: 122px;
          }

          .kol-settings-theme-card span {
            font-size: 24px;
          }

          .kol-settings-font-range {
            min-height: 74px;
            grid-template-columns: 36px 1fr 36px;
          }

          .kol-settings-font-range span {
            font-size: 24px;
          }

          .kol-settings-preview-user {
            font-size: 20px;
          }

          .kol-settings-font-preview p {
            font-size: 28px;
          }

          .kol-settings-font-preview button {
            min-height: 50px;
            border-width: 1px;
            font-size: 24px;
          }

          .kol-settings-widget-card h3 {
            font-size: 34px;
          }

          .kol-settings-widget-card p {
            font-size: 28px;
          }

          .kol-settings-logout {
            min-height: 64px;
            border-radius: 14px;
            font-size: 32px;
          }

          .kol-settings-profile-fields input {
            font-size: 30px;
            padding: 14px 18px;
          }

          .kol-settings-year-select {
            height: 54px;
            min-width: 160px;
            border-radius: 16px;
          }

          .kol-settings-year-select select {
            font-size: 28px;
          }

          .kol-settings-apple-note,
          .kol-settings-version {
            font-size: 20px;
          }
        }
      `}</style>
    </div>
  );
};
