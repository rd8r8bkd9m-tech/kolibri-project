import { act, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { MockInstance } from 'vitest';
import App from './App';

describe('Колибри Manus UI', () => {
  let consoleErrorSpy: MockInstance<Parameters<typeof console.error>, ReturnType<typeof console.error>>;
  let fetchSpy: MockInstance;

  beforeEach(() => {
    consoleErrorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    Element.prototype.scrollIntoView = vi.fn();
    try {
      localStorage.clear();
    } catch {
      // localStorage may be unavailable in some test runtimes
    }

    fetchSpy = vi.spyOn(globalThis, 'fetch').mockImplementation(async () => {
      return new Response(
        JSON.stringify({
          response: 'Тестовый ответ',
          conversation_id: 'test-conversation-id',
        }),
        { status: 200, headers: { 'Content-Type': 'application/json' } },
      );
    });
  });

  afterEach(() => {
    consoleErrorSpy.mockRestore();
    fetchSpy?.mockRestore();
  });

  it('renders sidebar with navigation tabs', async () => {
    await act(async () => {
      render(<App />);
    });

    expect(screen.getByText('Колибри')).toBeInTheDocument();
    expect(screen.getByText('Чаты')).toBeInTheDocument();
    expect(screen.getByText('Ядро')).toBeInTheDocument();
    expect(screen.getByText('GPU Store')).toBeInTheDocument();
    expect(screen.getByText('Фабрика')).toBeInTheDocument();
    expect(screen.getByText('AI Агент')).toBeInTheDocument();
    expect(screen.getByText('Задачи')).toBeInTheDocument();
    expect(screen.getByText('Знания')).toBeInTheDocument();
    expect(screen.getByText('Терминал')).toBeInTheDocument();
    expect(screen.getByText('Настройки')).toBeInTheDocument();
  });

  it('renders chat welcome state with suggestions', async () => {
    await act(async () => {
      render(<App />);
    });

    expect(screen.getByText('Чем помочь в этом чате?')).toBeInTheDocument();
    expect(screen.getByText('Сделай быстрый статус backend и frontend.')).toBeInTheDocument();
  });

  it('renders chat input with correct placeholder', async () => {
    await act(async () => {
      render(<App />);
    });

    const textarea = screen.getByPlaceholderText(/Спрашивай что угодно/i);
    expect(textarea).toBeInTheDocument();
  });

  it('allows typing a message', async () => {
    await act(async () => {
      render(<App />);
    });

    const textarea = screen.getByPlaceholderText(/Спрашивай что угодно/i);
    await userEvent.type(textarea, 'Привет');
    expect(textarea).toHaveValue('Привет');
  });

  it('sends a message and shows assistant response', async () => {
    await act(async () => {
      render(<App />);
    });

    const textarea = screen.getByPlaceholderText(/Спрашивай что угодно/i);
    await userEvent.type(textarea, 'Привет');

    const sendButton = screen.getByRole('button', { name: /Отправить сообщение/i });
    await userEvent.click(sendButton);

    await waitFor(() => expect(fetchSpy).toHaveBeenCalled());
    await waitFor(() => expect(screen.getAllByText('Привет').length).toBeGreaterThan(0));
    await waitFor(() => expect(screen.getAllByText('Тестовый ответ').length).toBeGreaterThan(0));
  });

  it('fills input when clicking a starter suggestion', async () => {
    await act(async () => {
      render(<App />);
    });

    const suggestion = screen.getByText('Сделай быстрый статус backend и frontend.');
    await userEvent.click(suggestion);

    const textarea = screen.getByPlaceholderText(/Спрашивай что угодно/i) as HTMLTextAreaElement;
    expect(textarea.value).toBe('Сделай быстрый статус backend и frontend.');
  });

  it('switches tabs via sidebar navigation', async () => {
    await act(async () => {
      render(<App />);
    });

    await userEvent.click(screen.getByText('Задачи'));
    await waitFor(() => expect(screen.getByText('Задачи', { selector: 'h1' })).toBeInTheDocument());
  });

  it('opens the core module page', async () => {
    fetchSpy.mockImplementation(async (input: RequestInfo | URL) => {
      const url = String(input);
      const bodyByUrl: Record<string, unknown> = {
        '/api/v1/health/detail': {
          status: 'ok',
          uptime_s: 10,
          version: 'test',
          subsystems: {
            engine: { status: 'ok', patterns: 12, edges: 34, documents_loaded: true, embeddings_ready: true, causal_index_ready: false },
            persistence: { status: 'ok', db_size_mb: 1.5, patterns_count: 12, edges_count: 34, enabled: true },
            corpus: { status: 'ok', files: 2, size_kb: 128 },
            memory: { rss_mb: 64, percent: 1 },
            disk: { free_gb: 10, percent_used: 20 },
          },
        },
        '/api/v1/ai/stats': {
          model_available: true,
          graph_patterns: 12,
          graph_edges: 34,
          graph_max_patterns: 100,
          graph_max_edges: 100,
          graph_documents: 2,
          graph_tokens: 40,
          graph_version: 7,
          formula_generation: 3,
          formula_fitness: 0.5,
          formula_layers: 5,
          formula_layers_fast: 2,
          embedding_vocab_size: 4,
          embedding_trained_pairs: 3,
          sentence_store_size: 2,
        },
        '/api/v1/model/stats': { exists: true, path: '/tmp/model.klm', size_mb: 1, patterns: 0, edges: 0 },
        '/api/knowledge/stats': { documents: 1, relations: 0, reason_blocks: 0, by_type: { chat: 1 } },
        '/api/archiver/project/status': {
          success: true,
          project_root: '/tmp/project',
          seed_bin_available: true,
          vault_available: true,
          default_archive_root: '/tmp/archives',
          default_restore_root: '/tmp/restores',
        },
        '/api/v1/ai/voice/health': { enabled: false, provider: 'test', detail: 'missing key' },
        '/api/v1/swarm/status': { local_node_id: 'test-node', total_nodes: 0, active_nodes: 0, target_nodes: 1, sync_events: 0 },
        '/api/v1/sync/status': { enabled: false, detail: 'token missing', node_id: 'test', global_version: 7, peers: 0, deltas_sent: 0, deltas_received: 0, bytes_sent: 0, bytes_received: 0 },
        '/api/system/stats': { cpu: 1, memory: 2, memory_used_gb: 3, uptime: 4, processes: 5 },
        '/api/observer/nodes': { nodes: [], count: 0 },
        '/api/factory/items': [],
      };
      const payload = bodyByUrl[url] ?? {};
      return new Response(JSON.stringify(payload), { status: 200, headers: { 'Content-Type': 'application/json' } });
    });

    await act(async () => {
      render(<App />);
    });

    await userEvent.click(screen.getByText('Ядро'));
    await waitFor(() => expect(screen.getByText('Kolibri AGI Runtime')).toBeInTheDocument());
    expect(screen.getByText('AI Engine')).toBeInTheDocument();
    expect(screen.getByText('Archiver')).toBeInTheDocument();
  });
});
