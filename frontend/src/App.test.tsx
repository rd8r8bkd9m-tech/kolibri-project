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

    const textarea = screen.getByPlaceholderText(/Напишите сообщение для ассистента Колибри/i);
    expect(textarea).toBeInTheDocument();
  });

  it('allows typing a message', async () => {
    await act(async () => {
      render(<App />);
    });

    const textarea = screen.getByPlaceholderText(/Напишите сообщение для ассистента Колибри/i);
    await userEvent.type(textarea, 'Привет');
    expect(textarea).toHaveValue('Привет');
  });

  it('sends a message and shows assistant response', async () => {
    await act(async () => {
      render(<App />);
    });

    const textarea = screen.getByPlaceholderText(/Напишите сообщение для ассистента Колибри/i);
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

    const textarea = screen.getByPlaceholderText(/Напишите сообщение для ассистента Колибри/i) as HTMLTextAreaElement;
    expect(textarea.value).toBe('Сделай быстрый статус backend и frontend.');
  });

  it('switches tabs via sidebar navigation', async () => {
    await act(async () => {
      render(<App />);
    });

    await userEvent.click(screen.getByText('Задачи'));
    await waitFor(() => expect(screen.getByText('Задачи', { selector: 'h1' })).toBeInTheDocument());
  });
});
