import { useMemo } from "react";
import {
  Archive,
  ArrowRight,
  CheckCircle2,
  Clock3,
  Cpu,
  Database,
  Globe,
  MessageSquare,
  Settings,
  ShieldCheck,
  Sparkles,
  Terminal,
} from "lucide-react";

interface LandingPageProps {
  onEnter: () => void;
}

const PRIMARY_DOMAIN = "kolibriai.ru";
const LEGACY_DOMAIN = "calibri.ai.ru";

const PREVIEW_TABS = [
  { id: "chat", label: "Чат", icon: MessageSquare, active: true },
  { id: "crawler", label: "AI Агент", icon: Globe, active: false },
  { id: "knowledge", label: "Знания", icon: Database, active: false },
  { id: "archiver", label: "Архиватор", icon: Archive, active: false },
  { id: "terminal", label: "Терминал", icon: Terminal, active: false },
  { id: "settings", label: "Настройки", icon: Settings, active: false },
];

export const LandingPage = ({ onEnter }: LandingPageProps) => {
  const hostLabel = useMemo(() => {
    if (typeof window === "undefined") {
      return PRIMARY_DOMAIN;
    }
    return window.location.host || PRIMARY_DOMAIN;
  }, []);

  return (
    <div className="landing-root">
      <div className="landing-decor landing-decor-left" aria-hidden="true" />
      <div className="landing-decor landing-decor-right" aria-hidden="true" />

      <div className="landing-shell">
        <header className="landing-header">
          <div className="landing-logo-wrap">
            <div className="landing-logo-icon">
              <Sparkles size={16} />
            </div>
            <div>
              <p className="landing-logo-title">Kolibri AI</p>
              <p className="landing-logo-subtitle">Production Preview</p>
            </div>
          </div>
          <span className="landing-beta-pill">
            <Clock3 size={14} />
            Public Beta
          </span>
        </header>

        <main className="landing-main">
          <section className="landing-hero">
            <p className="landing-kicker">Стартовая страница</p>
            <h1 className="landing-title">
              Kolibri AI запущен в бета-режиме на домене{" "}
              <span>{PRIMARY_DOMAIN}</span>
            </h1>
            <p className="landing-description">
              Интерфейс уже рабочий и повторяет структуру основного приложения:
              чат, агент, знания, архиватор и терминал. Часть функций еще
              дорабатывается, но платформой уже можно пользоваться.
            </p>

            <div className="landing-actions">
              <button className="landing-btn landing-btn-primary" onClick={onEnter}>
                Войти в приложение
                <ArrowRight size={16} />
              </button>
              <a className="landing-btn landing-btn-secondary" href="/api/docs">
                API Docs
              </a>
            </div>

            <div className="landing-status-grid">
              <article className="landing-status-card">
                <ShieldCheck size={16} />
                <div>
                  <p>Статус запуска</p>
                  <strong>Бета, доступ открыт</strong>
                </div>
              </article>
              <article className="landing-status-card">
                <Cpu size={16} />
                <div>
                  <p>Среда</p>
                  <strong>Домашний production server</strong>
                </div>
              </article>
              <article className="landing-status-card">
                <CheckCircle2 size={16} />
                <div>
                  <p>Домен</p>
                  <strong>{hostLabel}</strong>
                </div>
              </article>
            </div>

            <p className="landing-note">
              Примечание: исторический домен <code>{LEGACY_DOMAIN}</code>{" "}
              можно оставить как алиас, основной адрес -{" "}
              <code>{PRIMARY_DOMAIN}</code>.
            </p>
          </section>

          <section className="landing-preview-wrap" aria-label="Превью интерфейса">
            <div className="landing-preview">
              <aside className="landing-preview-sidebar">
                <div className="landing-preview-brand">
                  <div className="landing-preview-dot" />
                  <span>Kolibri</span>
                </div>
                {PREVIEW_TABS.map((tab) => (
                  <div
                    key={tab.id}
                    className={`landing-preview-tab ${tab.active ? "active" : ""}`}
                  >
                    <tab.icon size={15} />
                    <span>{tab.label}</span>
                  </div>
                ))}
              </aside>

              <div className="landing-preview-content">
                <div className="landing-preview-topbar">
                  <div>
                    <p className="landing-preview-heading">Чат</p>
                    <p className="landing-preview-meta">Бета-интерфейс Manus</p>
                  </div>
                  <span className="landing-preview-ready">ONLINE</span>
                </div>

                <div className="landing-preview-message system">
                  Добро пожаловать в Kolibri AI Beta.
                </div>
                <div className="landing-preview-message user">
                  Покажи статус системы и доступные модули.
                </div>
                <div className="landing-preview-message system">
                  Доступны: чат, AI агент, архиватор, терминал, знания.
                </div>

                <div className="landing-preview-input">
                  Введите запрос...
                  <span>Enter</span>
                </div>
              </div>
            </div>
          </section>
        </main>
      </div>

      <style>{`
        .landing-root {
          position: relative;
          min-height: 100vh;
          background:
            radial-gradient(1200px 600px at -10% -10%, rgba(16, 185, 129, 0.12), transparent 70%),
            radial-gradient(900px 500px at 110% 10%, rgba(59, 130, 246, 0.14), transparent 70%),
            var(--bg-primary);
          color: var(--text-primary);
          overflow: hidden;
        }

        .landing-decor {
          position: absolute;
          width: 440px;
          height: 440px;
          border-radius: 50%;
          filter: blur(54px);
          pointer-events: none;
          z-index: 0;
        }

        .landing-decor-left {
          top: -200px;
          left: -120px;
          background: rgba(16, 185, 129, 0.16);
        }

        .landing-decor-right {
          top: -180px;
          right: -120px;
          background: rgba(59, 130, 246, 0.18);
        }

        .landing-shell {
          position: relative;
          z-index: 1;
          max-width: 1220px;
          margin: 0 auto;
          padding: 28px 24px 40px;
        }

        .landing-header {
          display: flex;
          align-items: center;
          justify-content: space-between;
          gap: 16px;
          margin-bottom: 28px;
        }

        .landing-logo-wrap {
          display: flex;
          align-items: center;
          gap: 12px;
        }

        .landing-logo-icon {
          width: 34px;
          height: 34px;
          border-radius: 10px;
          display: flex;
          align-items: center;
          justify-content: center;
          background: linear-gradient(135deg, #10b981, #0891b2);
          color: #ffffff;
        }

        .landing-logo-title {
          margin: 0;
          font-size: 15px;
          font-weight: 700;
          letter-spacing: 0.02em;
        }

        .landing-logo-subtitle {
          margin: 2px 0 0;
          font-size: 12px;
          color: var(--text-secondary);
        }

        .landing-beta-pill {
          display: inline-flex;
          align-items: center;
          gap: 8px;
          padding: 7px 12px;
          border-radius: 999px;
          font-size: 12px;
          font-weight: 600;
          color: #0f766e;
          background: rgba(16, 185, 129, 0.14);
          border: 1px solid rgba(16, 185, 129, 0.28);
        }

        .landing-main {
          display: grid;
          grid-template-columns: minmax(0, 1.02fr) minmax(0, 0.98fr);
          gap: 28px;
          align-items: stretch;
        }

        .landing-hero {
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 20px;
          padding: 34px;
          box-shadow: var(--shadow-card);
        }

        .landing-kicker {
          margin: 0 0 12px;
          display: inline-flex;
          align-items: center;
          gap: 8px;
          font-size: 12px;
          font-weight: 700;
          letter-spacing: 0.08em;
          text-transform: uppercase;
          color: #0f766e;
        }

        .landing-title {
          margin: 0;
          font-size: clamp(30px, 4.2vw, 46px);
          line-height: 1.12;
          font-weight: 700;
          letter-spacing: -0.02em;
          max-width: 720px;
        }

        .landing-title span {
          color: #0f766e;
        }

        .landing-description {
          margin: 18px 0 0;
          font-size: 16px;
          line-height: 1.65;
          color: var(--text-secondary);
          max-width: 720px;
        }

        .landing-actions {
          margin-top: 26px;
          display: flex;
          gap: 12px;
          flex-wrap: wrap;
        }

        .landing-btn {
          border: 1px solid var(--border-primary);
          border-radius: 12px;
          padding: 11px 16px;
          font-size: 14px;
          font-weight: 600;
          line-height: 1;
          display: inline-flex;
          align-items: center;
          gap: 8px;
          text-decoration: none;
          cursor: pointer;
          transition: all 0.18s ease;
        }

        .landing-btn-primary {
          background: #111827;
          color: #f9fafb;
          border-color: #111827;
        }

        .landing-btn-primary:hover {
          transform: translateY(-1px);
          background: #1f2937;
          border-color: #1f2937;
        }

        .landing-btn-secondary {
          color: var(--text-primary);
          background: var(--bg-secondary);
        }

        .landing-btn-secondary:hover {
          background: var(--bg-hover);
        }

        .landing-status-grid {
          margin-top: 22px;
          display: grid;
          grid-template-columns: repeat(3, minmax(0, 1fr));
          gap: 10px;
        }

        .landing-status-card {
          display: flex;
          gap: 10px;
          align-items: flex-start;
          padding: 11px 12px;
          border-radius: 12px;
          background: var(--bg-tertiary);
          border: 1px solid var(--border-primary);
          color: var(--text-secondary);
        }

        .landing-status-card p {
          margin: 0;
          font-size: 11px;
          text-transform: uppercase;
          letter-spacing: 0.05em;
        }

        .landing-status-card strong {
          display: block;
          margin-top: 3px;
          font-size: 13px;
          color: var(--text-primary);
          font-weight: 600;
        }

        .landing-note {
          margin: 16px 0 0;
          color: var(--text-secondary);
          font-size: 13px;
        }

        .landing-note code {
          color: var(--text-primary);
          background: var(--bg-tertiary);
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          padding: 2px 6px;
          font-size: 12px;
        }

        .landing-preview-wrap {
          min-height: 560px;
        }

        .landing-preview {
          height: 100%;
          display: grid;
          grid-template-columns: 220px minmax(0, 1fr);
          border-radius: 20px;
          overflow: hidden;
          border: 1px solid var(--border-primary);
          box-shadow: var(--shadow-elevated);
          background: var(--bg-secondary);
        }

        .landing-preview-sidebar {
          padding: 14px 10px;
          border-right: 1px solid var(--border-primary);
          background: color-mix(in srgb, var(--bg-secondary) 65%, var(--bg-tertiary));
        }

        .landing-preview-brand {
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 10px 10px 14px;
          margin-bottom: 4px;
          border-bottom: 1px solid var(--border-primary);
          font-size: 13px;
          font-weight: 600;
        }

        .landing-preview-dot {
          width: 9px;
          height: 9px;
          border-radius: 50%;
          background: #10b981;
        }

        .landing-preview-tab {
          display: flex;
          align-items: center;
          gap: 8px;
          border-radius: 9px;
          padding: 9px 10px;
          font-size: 13px;
          color: var(--text-secondary);
          margin-top: 2px;
        }

        .landing-preview-tab.active {
          background: var(--accent-bg);
          color: var(--text-primary);
          font-weight: 600;
        }

        .landing-preview-content {
          padding: 18px;
          display: flex;
          flex-direction: column;
          gap: 12px;
          background:
            linear-gradient(180deg, color-mix(in srgb, var(--bg-secondary) 70%, var(--bg-tertiary)) 0%, var(--bg-secondary) 140px),
            var(--bg-secondary);
        }

        .landing-preview-topbar {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding-bottom: 14px;
          border-bottom: 1px solid var(--border-primary);
          margin-bottom: 2px;
        }

        .landing-preview-heading {
          margin: 0;
          font-size: 18px;
          font-weight: 700;
        }

        .landing-preview-meta {
          margin: 2px 0 0;
          color: var(--text-secondary);
          font-size: 12px;
        }

        .landing-preview-ready {
          font-size: 11px;
          letter-spacing: 0.06em;
          text-transform: uppercase;
          color: #047857;
          background: rgba(16, 185, 129, 0.14);
          border: 1px solid rgba(16, 185, 129, 0.24);
          border-radius: 999px;
          padding: 4px 9px;
          font-weight: 700;
        }

        .landing-preview-message {
          max-width: 88%;
          padding: 10px 12px;
          border-radius: 11px;
          line-height: 1.45;
          font-size: 13px;
          border: 1px solid var(--border-primary);
        }

        .landing-preview-message.system {
          background: var(--bg-tertiary);
          color: var(--text-primary);
        }

        .landing-preview-message.user {
          align-self: flex-end;
          background: #111827;
          color: #f9fafb;
          border-color: #111827;
        }

        .landing-preview-input {
          margin-top: auto;
          border: 1px solid var(--border-primary);
          border-radius: 11px;
          background: var(--bg-tertiary);
          color: var(--text-muted);
          font-size: 13px;
          padding: 10px 12px;
          display: flex;
          justify-content: space-between;
          align-items: center;
        }

        .landing-preview-input span {
          color: var(--text-primary);
          background: var(--bg-secondary);
          border: 1px solid var(--border-primary);
          border-radius: 8px;
          padding: 3px 7px;
          font-size: 11px;
          font-weight: 600;
        }

        @media (max-width: 1180px) {
          .landing-main {
            grid-template-columns: 1fr;
          }

          .landing-preview-wrap {
            min-height: 500px;
          }
        }

        @media (max-width: 760px) {
          .landing-shell {
            padding: 18px 14px 26px;
          }

          .landing-header {
            margin-bottom: 16px;
          }

          .landing-hero {
            padding: 20px;
          }

          .landing-title {
            font-size: clamp(26px, 9vw, 34px);
          }

          .landing-description {
            font-size: 15px;
          }

          .landing-status-grid {
            grid-template-columns: 1fr;
          }

          .landing-preview {
            grid-template-columns: 1fr;
          }

          .landing-preview-sidebar {
            border-right: 0;
            border-bottom: 1px solid var(--border-primary);
          }
        }
      `}</style>
    </div>
  );
};
