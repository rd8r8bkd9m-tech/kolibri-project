/**
 * LearningDashboard.tsx — Панель управления непрерывным обучением Колибри
 * 
 * Показывает:
 * - Статус демона обучения
 * - Метрики (corpus, formulas, embeddings, dialogue)
 * - Curriculum level
 * - Запущенные задачи
 * - Контролы (старт/стоп/advance curriculum)
 */
import { useEffect, useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { motion, AnimatePresence } from "framer-motion";
import {
  Play,
  Square,
  TrendingUp,
  Brain,
  Database,
  MessageSquare,
  BookOpen,
  Activity,
  RefreshCw,
  AlertTriangle,
  CheckCircle2,
  Clock,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Badge } from "@/components/ui/badge";
import {
  getLearningStatus,
  startLearning,
  stopLearning,
  advanceCurriculum,
  type LearningStatusResponse,
} from "@/api";

// ============================================================================
// Component
// ============================================================================

export function LearningDashboard() {
  const queryClient = useQueryClient();
  const [autoRefresh, setAutoRefresh] = useState(true);

  const { data: status, isLoading } = useQuery({
    queryKey: ["learning-status"],
    queryFn: getLearningStatus,
    refetchInterval: autoRefresh ? 5000 : false,
  });

  const startMutation = useMutation({
    mutationFn: startLearning,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["learning-status"] });
    },
  });

  const stopMutation = useMutation({
    mutationFn: stopLearning,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["learning-status"] });
    },
  });

  const advanceMutation = useMutation({
    mutationFn: advanceCurriculum,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["learning-status"] });
    },
  });

  const uptimeFormatted = status?.metrics?.total_uptime
    ? formatUptime(status?.metrics?.total_uptime)
    : "0s";

  return (
    <div className="w-full h-full flex flex-col gap-4 p-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Brain className="h-6 w-6 text-sky-400" />
          <div>
            <h2 className="text-lg font-semibold text-slate-100">Непрерывное обучение</h2>
            <p className="text-sm text-slate-400">
              Фоновое обучение • Краулинг • Эволюция формул • Извлечение знаний
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <Button
            variant="outline"
            onClick={() => setAutoRefresh(!autoRefresh)}
            className={autoRefresh ? "border-sky-500 text-sky-400 h-8" : "h-8"}
          >
            <RefreshCw className={`h-3 w-3 mr-1 ${autoRefresh ? "animate-spin" : ""}`} />
            Авто
          </Button>
          {!status?.running ? (
            <Button
              variant="default"
              onClick={() => startMutation.mutate()}
              disabled={startMutation.isPending}
              className="h-8"
            >
              <Play className="h-3 w-3 mr-1" />
              Запуск
            </Button>
          ) : (
            <Button
              variant="outline"
              onClick={() => stopMutation.mutate()}
              disabled={stopMutation.isPending}
              className="h-8 text-red-400 border-red-500"
            >
              <Square className="h-3 w-3 mr-1" />
              Стоп
            </Button>
          )}
        </div>
      </div>

      {/* Status badges */}
      <div className="flex items-center gap-3">
        <Badge className={status?.running ? "bg-green-500/20 text-green-400" : "bg-slate-500/20 text-slate-400"}>
          {status?.running ? (
            <>
              <CheckCircle2 className="h-3 w-3 mr-1" />
              Работает
            </>
          ) : (
            <>
              <Square className="h-3 w-3 mr-1" />
              Остановлен
            </>
          )}
        </Badge>
        <Badge className="bg-slate-500/10">
          <Clock className="h-3 w-3 mr-1" />
          {uptimeFormatted}
        </Badge>
        {status && (
          <Badge className="bg-sky-500/10">
            <TrendingUp className="h-3 w-3 mr-1" />
            Curriculum: {status?.metrics?.curriculum?.level ?? 0} ({status?.metrics?.curriculum?.source ?? 'unknown'})
          </Badge>
        )}
      </div>

      {/* Metrics cards */}
      {status && (
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          <MetricCard
            icon={<Database className="h-5 w-5" />}
            label="Корпус"
            value={`${status?.metrics?.corpus?.patterns ?? 0} паттернов`}
            subvalue={`${status?.metrics?.corpus?.edges ?? 0} связей, ${status?.metrics?.corpus?.documents ?? 0} док.`}
            color="text-blue-400"
          />
          <MetricCard
            icon={<Brain className="h-5 w-5" />}
            label="Формулы"
            value={`${status?.metrics?.formulas?.pool_size ?? 0} формул`}
            subvalue={`Fitness: ${status?.metrics?.formulas?.best_fitness?.toFixed(4) ?? '0.0000'}`}
            color="text-purple-400"
          />
          <MetricCard
            icon={<Activity className="h-5 w-5" />}
            label="Embeddings"
            value={`${status?.metrics?.embeddings?.vocab_size ?? 0} слов`}
            subvalue={`Loss: ${status?.metrics?.embeddings?.loss?.toFixed(4) ?? '0.0000'}`}
            color="text-green-400"
          />
          <MetricCard
            icon={<MessageSquare className="h-5 w-5" />}
            label="Диалоги"
            value={`${status?.metrics?.dialogue?.processed ?? 0} обработано`}
            subvalue={`${status?.metrics?.dialogue?.facts_extracted ?? 0} фактов`}
            color="text-amber-400"
          />
        </div>
      )}

      {/* Tasks list */}
      <div className="flex-1 min-h-0 flex flex-col">
        <h3 className="text-sm font-medium text-slate-300 mb-2">Задачи обучения</h3>
        <ScrollArea className="flex-1">
          <div className="space-y-2">
            {(status?.tasks ?? []).map((task) => (
              <TaskCard key={task.name} task={task} />
            ))}
            {isLoading && (
              <div className="text-center py-8 text-slate-500">Загрузка...</div>
            )}
          </div>
        </ScrollArea>
      </div>

      {/* Curriculum control */}
      <div className="border border-slate-700 rounded-lg p-3 bg-slate-900/50">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-2">
            <BookOpen className="h-4 w-4 text-slate-400" />
            <span className="text-sm font-medium text-slate-200">Curriculum Learning</span>
          </div>
          <Button
            variant="outline"
            onClick={() => advanceMutation.mutate()}
            disabled={advanceMutation.isPending}
            className="h-8"
          >
            <TrendingUp className="h-3 w-3 mr-1" />
            Повысить
          </Button>
        </div>
        <CurriculumProgress level={status?.metrics?.curriculum?.level ?? 0} />
      </div>
    </div>
  );
}

// ============================================================================
// Sub-components
// ============================================================================

function MetricCard({
  icon,
  label,
  value,
  subvalue,
  color,
}: {
  icon: React.ReactNode;
  label: string;
  value: string;
  subvalue: string;
  color: string;
}) {
  return (
    <div className="border border-slate-700 rounded-lg p-3 bg-slate-900/50">
      <div className="flex items-center gap-2 mb-1">
        <span className={color}>{icon}</span>
        <span className="text-xs text-slate-400">{label}</span>
      </div>
      <div className="text-sm font-semibold text-slate-100">{value}</div>
      <div className="text-xs text-slate-500">{subvalue}</div>
    </div>
  );
}

function TaskCard({ task }: { task: LearningStatusResponse["tasks"][0] }) {
  const priorityColor = {
    high: "border-red-500",
    medium: "border-amber-500",
    low: "border-blue-500",
    idle: "border-slate-500",
  };

  const priorityLabel = {
    high: "Высокий",
    medium: "Средний",
    low: "Низкий",
    idle: "Фоновый",
  };

  return (
    <div
      className={`border-l-2 ${priorityColor[task.priority as keyof typeof priorityColor] || priorityColor.idle} bg-slate-900/50 rounded-r-lg p-3`}
    >
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="text-sm font-medium text-slate-200">{task.name}</span>
          <span className="text-xs px-2 py-0.5 rounded-full bg-slate-500/20 text-slate-300">
            {priorityLabel[task.priority as keyof typeof priorityLabel] || task.priority}
          </span>
        </div>
        <div className="flex items-center gap-3 text-xs text-slate-400">
          <span>{task.run_count} запусков</span>
          <span>{task.total_time.toFixed(1)}s</span>
          {task.error_count > 0 && (
            <span className="text-red-400 flex items-center gap-1">
              <AlertTriangle className="h-3 w-3" />
              {task.error_count}
            </span>
          )}
        </div>
      </div>
      {task.last_error && (
        <div className="mt-2 text-xs text-red-400 bg-red-950/30 rounded px-2 py-1">
          {task.last_error.slice(0, 150)}
        </div>
      )}
    </div>
  );
}

function CurriculumProgress({ level }: { level: number }) {
  const levels = [
    { name: "Простой", desc: "Определения, детские энциклопедии" },
    { name: "Средний", desc: "Статьи Википедии, документация" },
    { name: "Сложный", desc: "Научные статьи, технические документы" },
  ];

  return (
    <div className="space-y-2">
      <div className="flex gap-2">
        {levels.map((l, i) => (
          <div
            key={l.name}
            className={`flex-1 rounded-full h-2 ${
              i <= level ? "bg-sky-500" : "bg-slate-700"
            }`}
          />
        ))}
      </div>
      <div className="flex justify-between text-xs text-slate-500">
        {levels.map((l, i) => (
          <span key={l.name} className={i === level ? "text-sky-400 font-medium" : ""}>
            {l.name}
          </span>
        ))}
      </div>
      {levels[level] && (
        <p className="text-xs text-slate-400 mt-1">{levels[level].desc}</p>
      )}
    </div>
  );
}

function formatUptime(seconds: number): string {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  if (seconds < 3600) return `${Math.round(seconds / 60)}m`;
  if (seconds < 86400) return `${Math.round(seconds / 3600)}h`;
  return `${Math.round(seconds / 86400)}d`;
}
