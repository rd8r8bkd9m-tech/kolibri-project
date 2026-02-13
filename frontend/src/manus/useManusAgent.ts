/**
 * useManusAgent.ts
 * 
 * React хук для управления состоянием AI-агента.
 * Обрабатывает отправку сообщений, задачи и взаимодействие с API.
 */

import { useState, useCallback, useRef } from 'react';

export interface Message {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  timestamp: Date;
}

export interface Task {
  id: string;
  title: string;
  status: 'pending' | 'running' | 'completed' | 'failed';
  progress?: number;
  startedAt?: Date;
  completedAt?: Date;
}

type AgentPhase = 'idle' | 'thinking' | 'researching' | 'coding' | 'testing' | 'reviewing';

interface UseManusAgentReturn {
  messages: Message[];
  tasks: Task[];
  isProcessing: boolean;
  isThinking: boolean;
  currentPhase: AgentPhase;
  sendMessage: (content: string) => Promise<void>;
  stopGeneration: () => void;
  clearHistory: () => void;
}

const generateId = () => Math.random().toString(36).substring(2, 11);

// Симуляция фаз работы агента
const AGENT_PHASES: { phase: AgentPhase; duration: number }[] = [
  { phase: 'thinking', duration: 1500 },
  { phase: 'researching', duration: 2000 },
  { phase: 'coding', duration: 3000 },
  { phase: 'testing', duration: 1500 },
  { phase: 'reviewing', duration: 1000 },
];

export const useManusAgent = (): UseManusAgentReturn => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [tasks, setTasks] = useState<Task[]>([]);
  const [isProcessing, setIsProcessing] = useState(false);
  const [isThinking, setIsThinking] = useState(false);
  const [currentPhase, setCurrentPhase] = useState<AgentPhase>('idle');
  
  const abortControllerRef = useRef<AbortController | null>(null);

  // Добавить сообщение пользователя
  const addUserMessage = useCallback((content: string): Message => {
    const message: Message = {
      id: generateId(),
      role: 'user',
      content,
      timestamp: new Date(),
    };
    setMessages((prev) => [...prev, message]);
    return message;
  }, []);

  // Добавить сообщение ассистента
  const addAssistantMessage = useCallback((content: string): Message => {
    const message: Message = {
      id: generateId(),
      role: 'assistant',
      content,
      timestamp: new Date(),
    };
    setMessages((prev) => [...prev, message]);
    return message;
  }, []);

  // Создать задачу
  const addTask = useCallback((title: string): Task => {
    const task: Task = {
      id: generateId(),
      title,
      status: 'pending',
      startedAt: new Date(),
    };
    setTasks((prev) => [...prev, task]);
    return task;
  }, []);

  // Обновить статус задачи
  const updateTask = useCallback((id: string, updates: Partial<Task>) => {
    setTasks((prev) =>
      prev.map((task) =>
        task.id === id ? { ...task, ...updates } : task
      )
    );
  }, []);

  // Симуляция ответа агента (заменить на реальный API)
  const simulateAgentResponse = useCallback(async (
    userMessage: string,
    signal: AbortSignal
  ): Promise<string> => {
    // Создаём задачи на основе запроса
    const taskId = generateId();
    setTasks((prev) => [
      ...prev,
      {
        id: taskId,
        title: 'Анализ запроса',
        status: 'running',
        progress: 0,
        startedAt: new Date(),
      },
    ]);

    // Симулируем фазы работы
    for (let i = 0; i < AGENT_PHASES.length; i++) {
      if (signal.aborted) throw new Error('Aborted');
      
      const { phase, duration } = AGENT_PHASES[i];
      setCurrentPhase(phase);
      
      // Обновляем прогресс
      const progress = Math.round(((i + 1) / AGENT_PHASES.length) * 100);
      updateTask(taskId, { progress });
      
      await new Promise((resolve) => setTimeout(resolve, duration));
    }

    // Завершаем задачу
    updateTask(taskId, {
      status: 'completed',
      progress: 100,
      completedAt: new Date(),
    });

    // Генерируем ответ
    const responses: Record<string, string> = {
      default: `Проанализировал ваш запрос: "${userMessage.substring(0, 50)}..."

**Результаты анализа:**

1. **Понимание контекста** — изучил структуру проекта Колибри OS
2. **Идентификация задачи** — определил ключевые точки воздействия
3. **Разработка решения** — подготовил план действий

Готов приступить к реализации. Хотите продолжить?`,
      
      анализ: `Провёл глубокий анализ архитектуры проекта:

**Технологический стек:**
- C23 для ядра (backend/src/)
- Python 3.10+ для оркестрации
- React + TypeScript для фронтенда
- WebAssembly для веб-интеграции

**Ключевые модули:**
- \`genome.c\` — логика 64-битных геномов
- \`formula.c\` — математические ядра
- \`ai_resonance.c\` — резонансное мышление

Хотите детальный отчёт по конкретному компоненту?`,
      
      документация: `Создаю документацию для проекта...

**Структура README.md:**

\`\`\`markdown
# Колибри OS

Платформа AGI на основе "Number-Thinking"

## Быстрый старт
\`\`\`bash
cmake -S . -B build && cmake --build build
./build/kolibri_node --genome auto_genome.dat
\`\`\`

## Архитектура
- 64-битные эволюционные геномы
- Резонансное мышление (Δ-orchestration)
- Swarm Protocol для распределённых вычислений
\`\`\`

Документация готова. Сохранить в README.md?`,
    };

    // Выбираем подходящий ответ
    const lowercaseMessage = userMessage.toLowerCase();
    if (lowercaseMessage.includes('анализ')) return responses.анализ;
    if (lowercaseMessage.includes('документац')) return responses.документация;
    return responses.default;
  }, [updateTask]);

  // Отправить сообщение
  const sendMessage = useCallback(async (content: string) => {
    if (!content.trim() || isProcessing) return;

    // Добавляем сообщение пользователя
    addUserMessage(content);
    
    // Начинаем обработку
    setIsProcessing(true);
    setIsThinking(true);
    setCurrentPhase('thinking');
    
    // Создаём AbortController для возможности отмены
    abortControllerRef.current = new AbortController();
    
    try {
      const response = await simulateAgentResponse(
        content,
        abortControllerRef.current.signal
      );
      
      // Добавляем ответ ассистента
      setIsThinking(false);
      addAssistantMessage(response);
    } catch (error) {
      if ((error as Error).message !== 'Aborted') {
        addAssistantMessage('Произошла ошибка при обработке запроса. Попробуйте ещё раз.');
      }
    } finally {
      setIsProcessing(false);
      setIsThinking(false);
      setCurrentPhase('idle');
      abortControllerRef.current = null;
    }
  }, [isProcessing, addUserMessage, addAssistantMessage, simulateAgentResponse]);

  // Остановить генерацию
  const stopGeneration = useCallback(() => {
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
      abortControllerRef.current = null;
    }
    setIsProcessing(false);
    setIsThinking(false);
    setCurrentPhase('idle');
    
    // Помечаем незавершённые задачи как отменённые
    setTasks((prev) =>
      prev.map((task) =>
        task.status === 'running' || task.status === 'pending'
          ? { ...task, status: 'failed' as const }
          : task
      )
    );
  }, []);

  // Очистить историю
  const clearHistory = useCallback(() => {
    setMessages([]);
    setTasks([]);
    setCurrentPhase('idle');
  }, []);

  return {
    messages,
    tasks,
    isProcessing,
    isThinking,
    currentPhase,
    sendMessage,
    stopGeneration,
    clearHistory,
  };
};

export default useManusAgent;
