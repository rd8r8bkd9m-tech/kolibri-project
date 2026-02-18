/**
 * MarkdownRenderer.tsx
 *
 * Рендер Markdown в сообщениях с подсветкой кода.
 */

import { useState, useCallback } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { Copy, Check } from 'lucide-react';

interface MarkdownRendererProps {
  content: string;
}

/** Кнопка копирования для блоков кода */
const CodeBlock = ({ className, children }: { className?: string; children?: React.ReactNode }) => {
  const [copied, setCopied] = useState(false);
  const lang = className?.replace('language-', '') || '';
  const code = String(children).replace(/\n$/, '');

  const handleCopy = useCallback(async () => {
    try {
      await navigator.clipboard.writeText(code);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {}
  }, [code]);

  return (
    <div style={{ margin: '12px 0', borderRadius: 'var(--m-radius-md)', overflow: 'hidden', border: '1px solid var(--m-border)' }}>
      <div className="m-code-header">
        <span className="m-code-header__lang">{lang || 'code'}</span>
        <button className="m-code-header__copy" onClick={handleCopy}>
          {copied ? <Check size={12} /> : <Copy size={12} />}
          <span>{copied ? 'Скопировано' : 'Копировать'}</span>
        </button>
      </div>
      <pre style={{ margin: 0 }}>
        <code className={className} style={{
          display: 'block',
          padding: '16px 20px',
          background: 'var(--m-bg-secondary)',
          color: 'var(--m-text-secondary)',
          fontFamily: 'var(--m-font-mono)',
          fontSize: '13px',
          lineHeight: '1.6',
          overflowX: 'auto',
          border: 'none',
          borderRadius: 0,
        }}>
          {code}
        </code>
      </pre>
    </div>
  );
};

/** Inline code */
const InlineCode = ({ children }: { children?: React.ReactNode }) => (
  <code style={{
    fontFamily: 'var(--m-font-mono)',
    fontSize: '13px',
    padding: '2px 6px',
    background: 'var(--m-bg-surface)',
    border: '1px solid var(--m-border)',
    borderRadius: 'var(--m-radius-xs)',
    color: 'var(--m-accent)',
  }}>
    {children}
  </code>
);

export const MarkdownRenderer = ({ content }: MarkdownRendererProps) => {
  return (
    <ReactMarkdown
      remarkPlugins={[remarkGfm]}
      components={{
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        code({ node, className, children, ref, ...props }) {
          const isBlock = className?.startsWith('language-') ||
            String(children).includes('\n');
          if (isBlock) {
            return <CodeBlock className={className}>{children}</CodeBlock>;
          }
          return <InlineCode>{children}</InlineCode>;
        },
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        pre({ node, children, ref, ...props }) {
          return <>{children}</>;
        },
        table({ children }) {
          return (
            <div style={{ overflowX: 'auto', margin: '12px 0' }}>
              <table style={{
                width: '100%',
                borderCollapse: 'collapse',
                fontSize: '14px',
              }}>
                {children}
              </table>
            </div>
          );
        },
        th({ children }) {
          return (
            <th style={{
              padding: '10px 14px',
              background: 'var(--m-bg-surface)',
              borderBottom: '2px solid var(--m-border)',
              textAlign: 'left',
              fontWeight: 600,
              color: 'var(--m-text-primary)',
              fontSize: '13px',
            }}>
              {children}
            </th>
          );
        },
        td({ children }) {
          return (
            <td style={{
              padding: '8px 14px',
              borderBottom: '1px solid var(--m-border)',
              color: 'var(--m-text-secondary)',
            }}>
              {children}
            </td>
          );
        },
        blockquote({ children }) {
          return (
            <blockquote style={{
              margin: '12px 0',
              padding: '12px 16px',
              borderLeft: '3px solid var(--m-accent)',
              background: 'var(--m-accent-dim)',
              borderRadius: '0 var(--m-radius-sm) var(--m-radius-sm) 0',
              color: 'var(--m-text-secondary)',
            }}>
              {children}
            </blockquote>
          );
        },
        hr() {
          return (
            <hr style={{
              margin: '20px 0',
              border: 'none',
              borderTop: '1px solid var(--m-border)',
            }} />
          );
        },
        a({ href, children }) {
          return (
            <a
              href={href}
              target="_blank"
              rel="noopener noreferrer"
              style={{
                color: 'var(--m-accent)',
                textDecoration: 'none',
                borderBottom: '1px solid transparent',
                transition: 'border-color 0.15s',
              }}
              onMouseOver={e => (e.currentTarget.style.borderBottomColor = 'var(--m-accent)')}
              onMouseOut={e => (e.currentTarget.style.borderBottomColor = 'transparent')}
            >
              {children}
            </a>
          );
        },
      }}
    >
      {content}
    </ReactMarkdown>
  );
};
