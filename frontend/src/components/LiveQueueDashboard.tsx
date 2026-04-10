import { useEffect, useMemo, useState } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import {
  BarChart3,
  Calendar,
  Check,
  CheckCheck,
  Clock3,
  FileSearch,
  Hash,
  PackageOpen,
  PencilLine,
  RefreshCcw,
  Search,
  X,
} from "lucide-react";
import {
  approveQuestion,
  bulkApproveQuestion,
  bulkRejectQuestion,
  editQuestion,
  rejectQuestion,
  searchQuestions,
  triggerExport,
} from "@/api/liveQueue";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { appQueryKeys, useLiveQueueAnalyticsQuery, useLiveQueuePendingQuery, useLiveQueueStatsQuery } from "@/features/workspace/query";
import { AppPanel, SectionTitle } from "@/features/ui-system/surface";
import { cn } from "@/lib/utils";
import type { LiveQueueItem, LiveQueueSearchResponse } from "@/types/liveQueue";

type ViewMode = "list" | "search" | "analytics";
type SortField = "created_at" | "id";
type SortOrder = "asc" | "desc";
type NotificationType = "success" | "error" | "info";

const PAGE_SIZE = 20;

function getErrorMessage(error: unknown, fallback: string): string {
  return error instanceof Error && error.message ? error.message : fallback;
}

function formatDate(value: string): string {
  return new Date(value).toLocaleString("ru-RU", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  });
}

function SummaryCard({
  label,
  value,
  tone = "default",
}: {
  label: string;
  value: string | number;
  tone?: "default" | "success" | "danger" | "accent";
}) {
  const toneClassName =
    tone === "success"
      ? "border-emerald-500/20 bg-emerald-500/10"
      : tone === "danger"
      ? "border-rose-500/20 bg-rose-500/10"
      : tone === "accent"
      ? "v3-accent-surface"
      : "border-foreground/8 bg-background";

  return (
    <div className={cn("rounded-2xl border px-4 py-3", toneClassName)}>
      <p className="text-[11px] font-semibold uppercase tracking-[0.16em] text-muted">{label}</p>
      <p className="mt-2 text-2xl font-semibold text-foreground">{value}</p>
    </div>
  );
}

function QueueItemCard({
  item,
  selected,
  checked,
  busy,
  onToggle,
  onSelect,
  onApprove,
  onReject,
}: {
  item: LiveQueueItem;
  selected: boolean;
  checked: boolean;
  busy: boolean;
  onToggle: (id: number) => void;
  onSelect: (item: LiveQueueItem) => void;
  onApprove: (id: number) => void;
  onReject: (id: number) => void;
}) {
  return (
    <AppPanel
      className={cn(
        "cursor-pointer px-4 py-4 transition-colors",
        selected ? "v3-accent-surface" : "hover:border-foreground/12",
      )}
    >
      <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex min-w-0 flex-1 items-start gap-3">
          <input
            type="checkbox"
            checked={checked}
            onChange={(event) => {
              event.stopPropagation();
              onToggle(item.id);
            }}
            className="mt-1 h-4 w-4 rounded border-foreground/20 bg-background"
          />
          <button type="button" className="min-w-0 flex-1 text-left" onClick={() => onSelect(item)}>
            <div className="flex flex-wrap items-center gap-2">
              <p className="truncate text-sm font-semibold text-foreground">{item.title}</p>
              <Badge className="bg-foreground/8 px-2 py-0.5 text-[10px] uppercase tracking-[0.16em] text-muted">
                #{item.id}
              </Badge>
            </div>
            <p className="mt-2 line-clamp-2 text-sm leading-6 text-muted">{item.content}</p>
            <div className="mt-3 flex flex-wrap items-center gap-2 text-xs text-muted">
              <span className="inline-flex items-center gap-1">
                <Hash className="h-3.5 w-3.5" />
                {item.source || "unknown"}
              </span>
              <span className="inline-flex items-center gap-1">
                <Calendar className="h-3.5 w-3.5" />
                {formatDate(item.created_at)}
              </span>
            </div>
          </button>
        </div>
        <div className="flex items-center gap-2 sm:ml-4">
          <Button type="button" variant="outline" className="rounded-xl" disabled={busy} onClick={() => onApprove(item.id)}>
            <Check className="mr-2 h-4 w-4" />
            Одобрить
          </Button>
          <Button type="button" variant="outline" className="rounded-xl" disabled={busy} onClick={() => onReject(item.id)}>
            <X className="mr-2 h-4 w-4" />
            Отклонить
          </Button>
        </div>
      </div>
    </AppPanel>
  );
}

export default function LiveQueueDashboard() {
  const queryClient = useQueryClient();
  const pendingQuery = useLiveQueuePendingQuery(100);
  const statsQuery = useLiveQueueStatsQuery();
  const [viewMode, setViewMode] = useState<ViewMode>("list");
  const analyticsQuery = useLiveQueueAnalyticsQuery(viewMode === "analytics");

  const pending = pendingQuery.data?.pending ?? [];
  const stats = statsQuery.data ?? { pending: 0, approved: 0, rejected: 0 };
  const analytics = analyticsQuery.data ?? null;
  const loading = pendingQuery.isLoading && pending.length === 0;
  const derivedApprovalRate =
    stats.approved + stats.rejected > 0 ? stats.approved / (stats.approved + stats.rejected) : null;
  const loadError =
    pendingQuery.error ??
    statsQuery.error ??
    (viewMode === "analytics" ? analyticsQuery.error : null);

  const [selectedItems, setSelectedItems] = useState<Set<number>>(new Set());
  const [selectedItemId, setSelectedItemId] = useState<number | null>(null);
  const [editMode, setEditMode] = useState(false);
  const [editedAnswer, setEditedAnswer] = useState("");
  const [notification, setNotification] = useState<{ type: NotificationType; message: string } | null>(null);
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResponse, setSearchResponse] = useState<LiveQueueSearchResponse | null>(null);
  const [searchLoading, setSearchLoading] = useState(false);
  const [sortField, setSortField] = useState<SortField>("created_at");
  const [sortOrder, setSortOrder] = useState<SortOrder>("desc");
  const [page, setPage] = useState(1);
  const [busyActionKey, setBusyActionKey] = useState<string | null>(null);

  const approveMutation = useMutation({ mutationFn: approveQuestion });
  const rejectMutation = useMutation({ mutationFn: rejectQuestion });
  const editMutation = useMutation({
    mutationFn: ({ id, answer }: { id: number; answer: string }) => editQuestion(id, answer),
  });
  const bulkApproveMutation = useMutation({ mutationFn: bulkApproveQuestion });
  const bulkRejectMutation = useMutation({ mutationFn: bulkRejectQuestion });
  const exportMutation = useMutation({ mutationFn: triggerExport });

  const selectedItem = useMemo(() => {
    const searchResults = searchResponse?.results ?? [];
    return pending.find((item) => item.id === selectedItemId) ?? searchResults.find((item) => item.id === selectedItemId) ?? null;
  }, [pending, searchResponse, selectedItemId]);

  const sortedPending = useMemo(() => {
    return [...pending].sort((left, right) => {
      const leftValue = sortField === "id" ? left.id : new Date(left.created_at).getTime();
      const rightValue = sortField === "id" ? right.id : new Date(right.created_at).getTime();
      return sortOrder === "asc" ? leftValue - rightValue : rightValue - leftValue;
    });
  }, [pending, sortField, sortOrder]);

  const totalPages = Math.max(1, Math.ceil(sortedPending.length / PAGE_SIZE));
  const paginatedPending = useMemo(() => {
    const start = (page - 1) * PAGE_SIZE;
    return sortedPending.slice(start, start + PAGE_SIZE);
  }, [page, sortedPending]);

  useEffect(() => {
    if (!notification) return undefined;
    const timer = window.setTimeout(() => setNotification(null), 3200);
    return () => window.clearTimeout(timer);
  }, [notification]);

  useEffect(() => {
    setPage((current) => Math.min(current, totalPages));
  }, [totalPages]);

  useEffect(() => {
    setSelectedItems((current) => {
      const allowed = new Set(pending.map((item) => item.id));
      return new Set([...current].filter((id) => allowed.has(id)));
    });
  }, [pending]);

  useEffect(() => {
    if (!selectedItem && selectedItemId !== null) {
      setSelectedItemId(null);
      setEditMode(false);
    }
  }, [selectedItem, selectedItemId]);

  const showNotification = (type: NotificationType, message: string) => {
    setNotification({ type, message });
  };

  const invalidateLiveQueue = async () => {
    await queryClient.invalidateQueries({ queryKey: appQueryKeys.liveQueueRoot });
  };

  const rerunSearchIfNeeded = async () => {
    const normalized = searchQuery.trim();
    if (!normalized) {
      setSearchResponse(null);
      return;
    }
    try {
      setSearchLoading(true);
      const next = await searchQuestions(normalized, "pending", 50);
      setSearchResponse(next);
    } catch (error) {
      showNotification("error", getErrorMessage(error, "Не удалось обновить результаты поиска."));
    } finally {
      setSearchLoading(false);
    }
  };

  const refreshAll = async () => {
    await invalidateLiveQueue();
    await rerunSearchIfNeeded();
  };

  const withBusyAction = async (key: string, task: () => Promise<void>) => {
    try {
      setBusyActionKey(key);
      await task();
    } finally {
      setBusyActionKey(null);
    }
  };

  const handleApprove = async (id: number) => {
    await withBusyAction(`approve-${id}`, async () => {
      try {
        await approveMutation.mutateAsync(id);
        await refreshAll();
        if (selectedItemId === id) {
          setSelectedItemId(null);
          setEditMode(false);
        }
        showNotification("success", `Вопрос #${id} одобрен.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось одобрить вопрос."));
      }
    });
  };

  const handleReject = async (id: number) => {
    await withBusyAction(`reject-${id}`, async () => {
      try {
        await rejectMutation.mutateAsync(id);
        await refreshAll();
        if (selectedItemId === id) {
          setSelectedItemId(null);
          setEditMode(false);
        }
        showNotification("success", `Вопрос #${id} отклонён.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось отклонить вопрос."));
      }
    });
  };

  const handleSaveEdit = async () => {
    const item = selectedItem;
    const answer = editedAnswer.trim();
    if (!item || !answer) return;

    await withBusyAction(`edit-${item.id}`, async () => {
      try {
        await editMutation.mutateAsync({ id: item.id, answer });
        await refreshAll();
        setEditMode(false);
        setSelectedItemId(null);
        showNotification("success", `Вопрос #${item.id} обновлён и одобрен.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось сохранить ответ."));
      }
    });
  };

  const handleBulkApprove = async () => {
    const ids = [...selectedItems];
    if (!ids.length) {
      showNotification("info", "Сначала выберите вопросы для модерации.");
      return;
    }

    await withBusyAction("bulk-approve", async () => {
      try {
        const result = await bulkApproveMutation.mutateAsync(ids);
        await refreshAll();
        setSelectedItems(new Set());
        setSelectedItemId(null);
        setEditMode(false);
        showNotification("success", `Одобрено ${result.approved ?? 0}, ошибок ${result.failed}.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось массово одобрить вопросы."));
      }
    });
  };

  const handleBulkReject = async () => {
    const ids = [...selectedItems];
    if (!ids.length) {
      showNotification("info", "Сначала выберите вопросы для модерации.");
      return;
    }

    await withBusyAction("bulk-reject", async () => {
      try {
        const result = await bulkRejectMutation.mutateAsync(ids);
        await refreshAll();
        setSelectedItems(new Set());
        setSelectedItemId(null);
        setEditMode(false);
        showNotification("success", `Отклонено ${result.rejected ?? 0}, ошибок ${result.failed}.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось массово отклонить вопросы."));
      }
    });
  };

  const handleExport = async () => {
    await withBusyAction("export", async () => {
      try {
        const result = await exportMutation.mutateAsync();
        showNotification("success", `Экспорт завершён, код выхода ${result.exit_code}.`);
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось запустить экспорт."));
      }
    });
  };

  const handleRefresh = async () => {
    await withBusyAction("refresh", async () => {
      try {
        await refreshAll();
        showNotification("success", "Live queue обновлён.");
      } catch (error) {
        showNotification("error", getErrorMessage(error, "Не удалось обновить данные."));
      }
    });
  };

  const handleSearchSubmit = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const normalized = searchQuery.trim();
    if (!normalized) {
      setSearchResponse(null);
      return;
    }

    try {
      setSearchLoading(true);
      const response = await searchQuestions(normalized, "pending", 50);
      setSearchResponse(response);
    } catch (error) {
      showNotification("error", getErrorMessage(error, "Не удалось выполнить поиск."));
    } finally {
      setSearchLoading(false);
    }
  };

  const toggleSelect = (id: number) => {
    setSelectedItems((current) => {
      const next = new Set(current);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const toggleSelectAll = () => {
    if (selectedItems.size === pending.length) {
      setSelectedItems(new Set());
      return;
    }
    setSelectedItems(new Set(pending.map((item) => item.id)));
  };

  const openDetails = (item: LiveQueueItem) => {
    setSelectedItemId(item.id);
    setEditMode(false);
    setEditedAnswer(item.content);
  };

  if (loading) {
    return (
      <AppPanel className="px-4 py-8">
        <p className="text-center text-sm text-muted">Загрузка live queue…</p>
      </AppPanel>
    );
  }

  return (
    <div className="space-y-4">
      {notification ? (
        <AppPanel
          className={cn(
            "px-4 py-3 text-sm",
            notification.type === "error"
              ? "border-rose-500/20 bg-rose-500/10 text-foreground"
              : notification.type === "success"
              ? "border-emerald-500/20 bg-emerald-500/10 text-foreground"
              : "v3-accent-surface text-foreground",
          )}
        >
          {notification.message}
        </AppPanel>
      ) : null}

      {loadError ? (
        <AppPanel className="border-rose-500/20 bg-rose-500/10 px-4 py-3 text-sm text-foreground">
          {getErrorMessage(loadError, "Не удалось загрузить данные live queue.")}
        </AppPanel>
      ) : null}

      <AppPanel className="px-4 py-4">
        <div className="flex flex-col gap-4">
          <div className="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
            <SectionTitle
              eyebrow="Live Queue"
              title="Модерация вопросов"
              description="Низкоуверенные ответы попадают сюда до валидации и последующего ingest в knowledge runtime."
            />
            <div className="flex flex-wrap gap-2">
              <Button type="button" variant="outline" className="rounded-xl" onClick={handleExport} disabled={busyActionKey === "export"}>
                <PackageOpen className="mr-2 h-4 w-4" />
                {busyActionKey === "export" ? "Экспорт…" : "Экспорт"}
              </Button>
              <Button type="button" variant="outline" className="rounded-xl" onClick={handleRefresh} disabled={busyActionKey === "refresh"}>
                <RefreshCcw className="mr-2 h-4 w-4" />
                {busyActionKey === "refresh" ? "Обновляю…" : "Обновить"}
              </Button>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-3 xl:grid-cols-4">
            <SummaryCard label="Ожидает" value={stats.pending} tone="default" />
            <SummaryCard label="Одобрено" value={stats.approved} tone="success" />
            <SummaryCard label="Отклонено" value={stats.rejected} tone="danger" />
            <SummaryCard
              label="Approval Rate"
              value={
                analytics
                  ? `${(analytics.overview.approval_rate * 100).toFixed(1)}%`
                  : derivedApprovalRate !== null
                  ? `${(derivedApprovalRate * 100).toFixed(1)}%`
                  : "—"
              }
              tone="accent"
            />
          </div>

          <div className="flex flex-wrap gap-2">
            {[
              { value: "list" as const, label: "Список", icon: CheckCheck },
              { value: "search" as const, label: "Поиск", icon: Search },
              { value: "analytics" as const, label: "Аналитика", icon: BarChart3 },
            ].map((tab) => {
              const Icon = tab.icon;
              return (
                <button
                  key={tab.value}
                  type="button"
                  onClick={() => setViewMode(tab.value)}
                  aria-pressed={viewMode === tab.value}
                  className={cn(
                    "inline-flex items-center gap-2 rounded-full border px-3 py-2 text-sm font-medium transition-all",
                    viewMode === tab.value
                      ? "v3-accent-solid"
                      : "border-foreground/8 bg-background text-muted hover:border-foreground/12 hover:text-foreground",
                  )}
                >
                  <Icon className="h-4 w-4" />
                  {tab.label}
                </button>
              );
            })}
          </div>
        </div>
      </AppPanel>

      {viewMode === "list" ? (
        <>
          <AppPanel className="px-4 py-4">
            <div className="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
              <div className="flex flex-wrap items-center gap-2">
                <Button type="button" variant="outline" className="rounded-xl" onClick={toggleSelectAll} disabled={!pending.length}>
                  {selectedItems.size === pending.length && pending.length > 0 ? "Снять всё" : "Выбрать всё"}
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  className="rounded-xl"
                  onClick={() => {
                    setSortField("created_at");
                    setSortOrder((current) => (sortField === "created_at" && current === "desc" ? "asc" : "desc"));
                  }}
                >
                  <Clock3 className="mr-2 h-4 w-4" />
                  По дате {sortField === "created_at" ? (sortOrder === "desc" ? "↓" : "↑") : ""}
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  className="rounded-xl"
                  onClick={() => {
                    setSortField("id");
                    setSortOrder((current) => (sortField === "id" && current === "desc" ? "asc" : "desc"));
                  }}
                >
                  <Hash className="mr-2 h-4 w-4" />
                  По ID {sortField === "id" ? (sortOrder === "desc" ? "↓" : "↑") : ""}
                </Button>
              </div>
              <p className="text-sm text-muted">
                {sortedPending.length ? `Показано ${(page - 1) * PAGE_SIZE + 1}-${Math.min(page * PAGE_SIZE, sortedPending.length)} из ${sortedPending.length}` : "Очередь пуста"}
              </p>
            </div>
          </AppPanel>

          {selectedItems.size > 0 ? (
            <AppPanel className="px-4 py-4">
              <div className="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
                <div>
                  <p className="text-sm font-semibold text-foreground">Выбрано вопросов: {selectedItems.size}</p>
                  <p className="mt-1 text-sm text-muted">Массовые действия применяются только к pending-элементам.</p>
                </div>
                <div className="flex flex-wrap gap-2">
                  <Button type="button" variant="outline" className="rounded-xl" onClick={handleBulkApprove} disabled={busyActionKey === "bulk-approve"}>
                    <Check className="mr-2 h-4 w-4" />
                    {busyActionKey === "bulk-approve" ? "Одобряю…" : "Одобрить"}
                  </Button>
                  <Button type="button" variant="outline" className="rounded-xl" onClick={handleBulkReject} disabled={busyActionKey === "bulk-reject"}>
                    <X className="mr-2 h-4 w-4" />
                    {busyActionKey === "bulk-reject" ? "Отклоняю…" : "Отклонить"}
                  </Button>
                </div>
              </div>
            </AppPanel>
          ) : null}

          <div className="space-y-3">
            {paginatedPending.length ? (
              paginatedPending.map((item) => (
                <QueueItemCard
                  key={item.id}
                  item={item}
                  selected={selectedItemId === item.id}
                  checked={selectedItems.has(item.id)}
                  busy={busyActionKey === `approve-${item.id}` || busyActionKey === `reject-${item.id}`}
                  onToggle={toggleSelect}
                  onSelect={openDetails}
                  onApprove={(id) => void handleApprove(id)}
                  onReject={(id) => void handleReject(id)}
                />
              ))
            ) : (
              <AppPanel className="px-4 py-8">
                <p className="text-center text-sm text-muted">Все вопросы уже обработаны.</p>
              </AppPanel>
            )}
          </div>

          {totalPages > 1 ? (
            <AppPanel className="px-4 py-4">
              <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
                <p className="text-sm text-muted">Страница {page} из {totalPages}</p>
                <div className="flex gap-2">
                  <Button type="button" variant="outline" className="rounded-xl" onClick={() => setPage((current) => Math.max(1, current - 1))} disabled={page === 1}>
                    Назад
                  </Button>
                  <Button type="button" variant="outline" className="rounded-xl" onClick={() => setPage((current) => Math.min(totalPages, current + 1))} disabled={page === totalPages}>
                    Далее
                  </Button>
                </div>
              </div>
            </AppPanel>
          ) : null}
        </>
      ) : null}

      {viewMode === "search" ? (
        <>
          <AppPanel className="px-4 py-4">
            <SectionTitle
              eyebrow="Search"
              title="Поиск по pending queue"
              description="Запрос идёт в backend search endpoint и не конфликтует с общей V3-навигацией."
            />
            <form onSubmit={handleSearchSubmit} className="mt-4 flex flex-col gap-3 sm:flex-row">
              <input
                value={searchQuery}
                onChange={(event) => setSearchQuery(event.target.value)}
                placeholder="Например: архитектура, pricing, deployment"
                className="v3-input flex-1"
              />
              <Button type="submit" className="rounded-2xl" disabled={searchLoading}>
                <FileSearch className="mr-2 h-4 w-4" />
                {searchLoading ? "Ищу…" : "Найти"}
              </Button>
            </form>
            {searchResponse ? (
              <p className="mt-3 text-sm text-muted">
                Найдено {searchResponse.count} из {searchResponse.total}.
              </p>
            ) : null}
          </AppPanel>

          <div className="space-y-3">
            {(searchResponse?.results ?? []).length ? (
              (searchResponse?.results ?? []).map((item) => (
                <QueueItemCard
                  key={item.id}
                  item={item}
                  selected={selectedItemId === item.id}
                  checked={selectedItems.has(item.id)}
                  busy={busyActionKey === `approve-${item.id}` || busyActionKey === `reject-${item.id}`}
                  onToggle={toggleSelect}
                  onSelect={openDetails}
                  onApprove={(id) => void handleApprove(id)}
                  onReject={(id) => void handleReject(id)}
                />
              ))
            ) : searchResponse ? (
              <AppPanel className="px-4 py-8">
                <p className="text-center text-sm text-muted">По этому запросу pending-вопросы не найдены.</p>
              </AppPanel>
            ) : null}
          </div>
        </>
      ) : null}

      {viewMode === "analytics" ? (
        analyticsQuery.isLoading ? (
          <AppPanel className="px-4 py-8">
            <p className="text-center text-sm text-muted">Загрузка аналитики…</p>
          </AppPanel>
        ) : analytics ? (
          <>
            <div className="grid gap-4 lg:grid-cols-2">
              <AppPanel className="px-4 py-4">
                <SectionTitle eyebrow="Overview" title="Общая статистика" />
                <div className="mt-4 space-y-3 text-sm">
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Ожидает</span>
                    <span className="font-semibold text-foreground">{analytics.overview.pending}</span>
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Одобрено</span>
                    <span className="font-semibold text-foreground">{analytics.overview.approved}</span>
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Отклонено</span>
                    <span className="font-semibold text-foreground">{analytics.overview.rejected}</span>
                  </div>
                  <div className="flex items-center justify-between border-t border-foreground/6 pt-3">
                    <span className="text-muted">Approval Rate</span>
                    <span className="font-semibold text-foreground">
                      {(analytics.overview.approval_rate * 100).toFixed(1)}%
                    </span>
                  </div>
                </div>
              </AppPanel>

              <AppPanel className="px-4 py-4">
                <SectionTitle eyebrow="Today" title="Сегодня" />
                <div className="mt-4 space-y-3 text-sm">
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Новых вопросов</span>
                    <span className="font-semibold text-foreground">{analytics.today.pending}</span>
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Одобрено</span>
                    <span className="font-semibold text-foreground">{analytics.today.approved}</span>
                  </div>
                  <div className="flex items-center justify-between">
                    <span className="text-muted">Отклонено</span>
                    <span className="font-semibold text-foreground">{analytics.today.rejected}</span>
                  </div>
                </div>
              </AppPanel>
            </div>

            <AppPanel className="px-4 py-4">
              <SectionTitle eyebrow="Trend" title="Прогресс модерации" />
              <div className="mt-4">
                <div className="h-4 w-full overflow-hidden rounded-full bg-foreground/8">
                  <div
                    className="h-full rounded-full bg-[color:var(--accent)] transition-all"
                    style={{ width: `${Math.max(0, Math.min(analytics.overview.approval_rate * 100, 100))}%` }}
                  />
                </div>
                <p className="mt-3 text-sm text-muted">
                  {analytics.overview.approval_rate >= 0.8
                    ? "Поток выглядит стабильным: большая часть вопросов проходит модерацию."
                    : analytics.overview.approval_rate >= 0.6
                    ? "Поток в норме, но стоит проверить формулировки и качество источников."
                    : "Approval rate низкий: есть смысл проверить source quality и дедупликацию очереди."}
                </p>
              </div>
            </AppPanel>
          </>
        ) : (
          <AppPanel className="px-4 py-8">
            <p className="text-center text-sm text-muted">Аналитика пока недоступна.</p>
          </AppPanel>
        )
      ) : null}

      {selectedItem ? (
        <AppPanel className="px-4 py-4">
          <div className="flex flex-col gap-4">
            <div className="flex items-start justify-between gap-4">
              <SectionTitle
                eyebrow="Details"
                title={editMode ? "Редактирование ответа" : "Детали вопроса"}
                description={`ID #${selectedItem.id} • источник ${selectedItem.source || "unknown"}`}
              />
              <Button
                type="button"
                variant="ghost"
                size="icon"
                className="h-9 w-9 rounded-full"
                onClick={() => {
                  setSelectedItemId(null);
                  setEditMode(false);
                }}
              >
                <X className="h-4 w-4" />
              </Button>
            </div>

            <div className="space-y-4">
              <div>
                <p className="text-[11px] font-semibold uppercase tracking-[0.16em] text-muted">Вопрос</p>
                <p className="mt-2 text-sm leading-6 text-foreground">{selectedItem.title}</p>
              </div>

              {editMode ? (
                <div>
                  <p className="text-[11px] font-semibold uppercase tracking-[0.16em] text-muted">Новый ответ</p>
                  <textarea
                    value={editedAnswer}
                    onChange={(event) => setEditedAnswer(event.target.value)}
                    className="v3-input mt-2 min-h-[11rem] resize-y"
                    placeholder="Сформулируйте итоговый ответ для базы знаний"
                  />
                </div>
              ) : (
                <div>
                  <p className="text-[11px] font-semibold uppercase tracking-[0.16em] text-muted">Текущий ответ</p>
                  <div className="mt-2 rounded-2xl border border-foreground/6 bg-background px-4 py-3 text-sm leading-6 text-foreground">
                    {selectedItem.content}
                  </div>
                </div>
              )}

              <div className="flex flex-wrap items-center gap-3 text-xs text-muted">
                <span className="inline-flex items-center gap-1">
                  <Hash className="h-3.5 w-3.5" />
                  {selectedItem.source || "unknown"}
                </span>
                <span className="inline-flex items-center gap-1">
                  <Calendar className="h-3.5 w-3.5" />
                  {formatDate(selectedItem.created_at)}
                </span>
              </div>
            </div>

            <div className="flex flex-wrap gap-2">
              {editMode ? (
                <>
                  <Button
                    type="button"
                    className="rounded-xl"
                    onClick={() => void handleSaveEdit()}
                    disabled={!editedAnswer.trim() || busyActionKey === `edit-${selectedItem.id}`}
                  >
                    <Check className="mr-2 h-4 w-4" />
                    {busyActionKey === `edit-${selectedItem.id}` ? "Сохраняю…" : "Сохранить и одобрить"}
                  </Button>
                  <Button
                    type="button"
                    variant="outline"
                    className="rounded-xl"
                    onClick={() => {
                      setEditMode(false);
                      setEditedAnswer(selectedItem.content);
                    }}
                  >
                    Отмена
                  </Button>
                </>
              ) : (
                <>
                  <Button type="button" className="rounded-xl" onClick={() => setEditMode(true)}>
                    <PencilLine className="mr-2 h-4 w-4" />
                    Редактировать
                  </Button>
                  <Button
                    type="button"
                    variant="outline"
                    className="rounded-xl"
                    onClick={() => void handleApprove(selectedItem.id)}
                    disabled={busyActionKey === `approve-${selectedItem.id}`}
                  >
                    <Check className="mr-2 h-4 w-4" />
                    Одобрить
                  </Button>
                  <Button
                    type="button"
                    variant="outline"
                    className="rounded-xl"
                    onClick={() => void handleReject(selectedItem.id)}
                    disabled={busyActionKey === `reject-${selectedItem.id}`}
                  >
                    <X className="mr-2 h-4 w-4" />
                    Отклонить
                  </Button>
                </>
              )}
            </div>
          </div>
        </AppPanel>
      ) : null}
    </div>
  );
}
