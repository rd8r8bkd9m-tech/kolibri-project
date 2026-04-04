import { useMemo, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Activity, Download, Link2, PackageOpen, RefreshCcw, Rocket, Upload, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Sheet, SheetContent } from "@/components/ui/sheet";
import {
  downloadSwarmKpack,
  exportSwarmKpack,
  importSwarmKpack,
  ingestSwarmText,
  ingestSwarmUrl,
  runSwarmComparison,
  startSwarmRuntime,
  getKnowledgeAnalytics,
  getKnowledgeGraphData,
} from "@/api";
import { appQueryKeys, useQualityHistoryQuery, useSwarmStatusQuery } from "@/features/workspace/query";
import { AppPanel, SectionTitle } from "@/features/ui-system/surface";
import { cn } from "@/lib/utils";
import { useShellStore } from "@/store/useShellStore";
import type { WorkspaceSurface } from "@/types";
import { KnowledgeGraphViz } from "@/features/workspace/KnowledgeGraphViz";
import { LearningDashboard } from "@/features/workspace/LearningDashboard";

const surfaces: Array<{ value: WorkspaceSurface; label: string }> = [
  { value: "swarm", label: "Рой" },
  { value: "packs", label: "Пакеты" },
  { value: "teach", label: "Обучение" },
  { value: "quality", label: "Качество" },
  { value: "knowledge", label: "Граф знаний" },
  { value: "learning", label: "Обучение ИИ" },
];

export function WorkspaceDrawerV3() {
  const open = useShellStore((s) => s.workspaceOpen);
  const closeWorkspace = useShellStore((s) => s.closeWorkspace);
  const workspaceSurface = useShellStore((s) => s.workspaceSurface);
  const setWorkspaceSurface = useShellStore((s) => s.setWorkspaceSurface);
  const queryClient = useQueryClient();
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const [exportTitle, setExportTitle] = useState("Kolibri Knowledge Pack");
  const [exportDomain, setExportDomain] = useState("");
  const [teachText, setTeachText] = useState("");
  const [teachTitle, setTeachTitle] = useState("");
  const [teachUrl, setTeachUrl] = useState("");
  const [workspaceMessage, setWorkspaceMessage] = useState("");
  const swarmQuery = useSwarmStatusQuery();
  const qualityQuery = useQualityHistoryQuery(12);

  const refreshQueries = async () => {
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: appQueryKeys.swarmStatus }),
      queryClient.invalidateQueries({ queryKey: appQueryKeys.qualityHistory(12) }),
    ]);
  };

  const startMutation = useMutation({
    mutationFn: startSwarmRuntime,
    onSuccess: async () => {
      setWorkspaceMessage("Роевой контур запущен.");
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось запустить роевой контур.");
    },
  });

  const refreshMutation = useMutation({
    mutationFn: runSwarmComparison,
    onSuccess: async () => {
      setWorkspaceMessage("Сравнение роевого контура обновлено.");
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось пересчитать рой.");
    },
  });

  const exportMutation = useMutation({
    mutationFn: async () => {
      const title = exportTitle.trim() || "Kolibri Knowledge Pack";
      const packageId =
        title
          .toLowerCase()
          .replace(/[^0-9a-zа-яё._-]+/giu, "-")
          .replace(/^-+|-+$/g, "")
          .slice(0, 80) || "kolibri-pack";
      const pack = await exportSwarmKpack({
        package_id: packageId,
        title,
        language: "ru",
        domains: exportDomain.trim() ? [exportDomain.trim()] : [],
      });
      await downloadSwarmKpack({ download_url: pack.download_url, filename: pack.filename });
      return pack;
    },
    onSuccess: async (pack) => {
      setWorkspaceMessage(`.kpack готов: ${pack.filename}, документов ${pack.documents}.`);
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось экспортировать .kpack.");
    },
  });

  const teachTextMutation = useMutation({
    mutationFn: async () =>
      ingestSwarmText({
        title: teachTitle.trim(),
        text: teachText.trim(),
        category: "workspace",
        source: "kolibri-v3",
      }),
    onSuccess: async (status) => {
      setWorkspaceMessage(status.ingest?.message ?? "Текст добавлен в live-memory.");
      setTeachText("");
      setTeachTitle("");
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось добавить текст.");
    },
  });

  const teachUrlMutation = useMutation({
    mutationFn: async () => ingestSwarmUrl({ url: teachUrl.trim() }),
    onSuccess: async (status) => {
      setWorkspaceMessage(status.ingest?.message ?? "URL отправлен на ingest.");
      setTeachUrl("");
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось добавить URL.");
    },
  });

  const importMutation = useMutation({
    mutationFn: async (file: File) => importSwarmKpack(file, { refresh: true, refresh_timeout_sec: 180 }),
    onSuccess: async (status) => {
      const imported = status.import?.imported_documents ?? 0;
      setWorkspaceMessage(imported > 0 ? `.kpack импортирован: ${imported} документов.` : "Импорт завершён без новых документов.");
      await refreshQueries();
    },
    onError: (error) => {
      setWorkspaceMessage(error instanceof Error ? error.message : "Не удалось импортировать .kpack.");
    },
  });

  const latestDemo = swarmQuery.data?.latest_demo;
  const topology = swarmQuery.data?.swarm_topology;
  const swarmNodes = swarmQuery.data?.swarm_nodes ?? topology?.nodes ?? [];
  const comparisonTargets = topology?.comparison_targets ?? [];
  const targetNodeCount = topology?.target_node_count ?? swarmQuery.data?.latest_knowledge?.node_count ?? 10;
  const anchorNodeCount = topology?.anchor_node_count ?? swarmNodes.filter((node) => node.role === "anchor").length;
  const learnerNodeCount = topology?.learner_node_count ?? swarmNodes.filter((node) => node.role === "learner").length;
  const validatorNodeCount = topology?.validator_node_count ?? swarmNodes.filter((node) => node.role === "validator").length;
  const comparisonLabel = useMemo(() => {
    if (!comparisonTargets.length) return `1 vs ${targetNodeCount}`;
    return comparisonTargets.map((target) => target.node_count).join(" vs ");
  }, [comparisonTargets, targetNodeCount]);
  const qualityTrend = qualityQuery.data?.trend;
  const workspaceBusy =
    startMutation.isPending ||
    refreshMutation.isPending ||
    exportMutation.isPending ||
    teachTextMutation.isPending ||
    teachUrlMutation.isPending ||
    importMutation.isPending;
  const qualityTop = useMemo(() => {
    const weighted = qualityQuery.data?.trend?.category_weighted_pass_rate_avg ?? {};
    return Object.entries(weighted)
      .sort((left, right) => Number(right[1]) - Number(left[1]))
      .slice(0, 6);
  }, [qualityQuery.data]);

  return (
    <Sheet open={open} onOpenChange={(next) => (!next ? closeWorkspace() : undefined)}>
      <SheetContent
        title="Рабочая область"
        description="Рой, качество, пакеты знаний и обучение как вторичные рабочие поверхности"
        className="left-auto right-0 z-50 w-full max-w-[34rem] border-l border-r-0 bg-background px-5 pb-5 pt-5 max-lg:inset-x-0 max-lg:left-0 max-lg:right-0 max-lg:top-auto max-lg:h-[88dvh] max-lg:max-h-[88svh] max-lg:max-w-none max-lg:rounded-t-[1.9rem] max-lg:border-l-0 max-lg:border-t max-lg:px-4 max-lg:pb-[calc(env(safe-area-inset-bottom)+1rem)]"
      >
        <div className="flex h-full min-h-0 flex-col gap-5">
          <div className="flex items-start justify-between gap-4 border-b border-foreground/6 pb-4">
            <SectionTitle
              eyebrow="Рабочая область"
              title="Вторичные инструменты"
              description="Рой, пакеты знаний, обучение и качество убраны из основной навигации и живут как отдельные инструменты."
            />
            <Button type="button" variant="ghost" size="icon" className="h-10 w-10 rounded-full" onClick={closeWorkspace}>
              <X className="h-4.5 w-4.5" />
            </Button>
          </div>

          <div className="flex items-center gap-2 overflow-x-auto">
            {surfaces.map((surface) => (
              <button
                key={surface.value}
                type="button"
                onClick={() => setWorkspaceSurface(surface.value)}
                aria-pressed={workspaceSurface === surface.value}
                className={cn(
                  "rounded-full border px-3 py-2 text-sm font-medium transition",
                  workspaceSurface === surface.value
                    ? "v3-accent-solid"
                    : "border-foreground/8 bg-background text-muted hover:text-foreground",
                )}
              >
                {surface.label}
              </button>
            ))}
          </div>

          {workspaceMessage ? (
            <div className="v3-accent-surface rounded-2xl border px-4 py-3 text-sm text-foreground">
              {workspaceMessage}
            </div>
          ) : null}

          <div className="min-h-0 flex-1 overflow-y-auto space-y-4 pr-1">
            {workspaceSurface === "swarm" ? (
              <>
                <div className="grid grid-cols-2 gap-3">
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Контур</p>
                    <p className="mt-2 text-2xl font-semibold">{swarmQuery.data?.running ? "ON" : "OFF"}</p>
                    <p className="mt-1 text-xs text-muted">{targetNodeCount}-узловой роевой контур</p>
                  </AppPanel>
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Память</p>
                    <p className="mt-2 text-2xl font-semibold">{swarmQuery.data?.live_memory_document_count ?? "—"}</p>
                    <p className="mt-1 text-xs text-muted">документов в live-memory</p>
                  </AppPanel>
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Consensus</p>
                    <p className="mt-2 text-2xl font-semibold">
                      {topology?.consensus_score?.toFixed(3) ?? "—"}
                    </p>
                    <p className="mt-1 text-xs text-muted">
                      quorum {topology?.validator_quorum ?? "—"} из {validatorNodeCount || "—"} validator-узлов
                    </p>
                  </AppPanel>
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Преимущество</p>
                    <p className="mt-2 text-2xl font-semibold">
                      {swarmQuery.data?.latest_knowledge?.comparison?.swarm_vs_single_delta?.toFixed(3) ?? "—"}
                    </p>
                    <p className="mt-1 text-xs text-muted">{targetNodeCount} узлов против 1 узла</p>
                  </AppPanel>
                </div>

                <div className="grid grid-cols-2 gap-3">
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Топология</p>
                    <p className="mt-2 text-2xl font-semibold">{targetNodeCount}</p>
                    <p className="mt-1 text-xs text-muted">
                      {anchorNodeCount} anchor • {learnerNodeCount} learner • {validatorNodeCount} validator
                    </p>
                  </AppPanel>
                  <AppPanel className="px-4 py-4">
                    <p className="text-[11px] uppercase tracking-[0.16em] text-muted">Сравнение</p>
                    <p className="mt-2 text-2xl font-semibold">{comparisonLabel}</p>
                    <p className="mt-1 text-xs text-muted">
                      active {topology?.active_node_count ?? swarmNodes.length} • healthy {topology?.healthy_node_count ?? "—"}
                    </p>
                  </AppPanel>
                </div>

                <div className="grid grid-cols-2 gap-3">
                  <Button
                    type="button"
                    variant="outline"
                    className="rounded-2xl"
                    disabled={workspaceBusy}
                    onClick={() => startMutation.mutate()}
                  >
                    <Rocket className="mr-2 h-4 w-4" />
                    {startMutation.isPending ? "Запускаю…" : "Запустить"}
                  </Button>
                  <Button
                    type="button"
                    variant="outline"
                    className="rounded-2xl"
                    disabled={workspaceBusy}
                    onClick={() => refreshMutation.mutate()}
                  >
                    <RefreshCcw className="mr-2 h-4 w-4" />
                    {refreshMutation.isPending ? "Считаю…" : "Пересчитать"}
                  </Button>
                </div>

                {latestDemo ? (
                  <AppPanel className="px-4 py-4">
                    <SectionTitle
                      eyebrow="Последний demo"
                      title={latestDemo.title}
                      description={latestDemo.message}
                    />
                    {latestDemo.comparison_summary ? (
                      <div className="mt-4 grid grid-cols-2 gap-3 text-sm">
                        <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
                          <p className="text-muted">1 узел</p>
                          <p className="mt-1 font-semibold">{latestDemo.comparison_summary.single_hit_after.toFixed(3)}</p>
                        </div>
                        <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
                          <p className="text-muted">{targetNodeCount} узлов</p>
                          <p className="mt-1 font-semibold">{latestDemo.comparison_summary.swarm_hit_after.toFixed(3)}</p>
                        </div>
                      </div>
                    ) : null}
                  </AppPanel>
                ) : null}
              </>
            ) : null}

            {workspaceSurface === "packs" ? (
              <>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Export" title="Knowledge Pack" description="Упакуй live-memory в переносимую единицу знаний." />
                  <div className="mt-4 space-y-3">
                    <input value={exportTitle} onChange={(event) => setExportTitle(event.target.value)} className="v3-input" placeholder="Название пакета" />
                    <input value={exportDomain} onChange={(event) => setExportDomain(event.target.value)} className="v3-input" placeholder="Домен, например law" />
                    <Button
                      type="button"
                      className="w-full rounded-2xl"
                      disabled={workspaceBusy || !exportTitle.trim()}
                      onClick={() => exportMutation.mutate()}
                    >
                      <Download className="mr-2 h-4 w-4" />
                      {exportMutation.isPending ? "Экспортирую…" : "Экспортировать .kpack"}
                    </Button>
                  </div>
                </AppPanel>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Import" title="Подключить knowledge pack" description="Импортировать пакет в live-memory и сразу обновить рой." />
                  <input
                    ref={fileInputRef}
                    type="file"
                    accept=".kpack,application/zip"
                    className="hidden"
                    onChange={(event) => {
                      const file = event.target.files?.[0];
                      if (!file) return;
                      void importMutation.mutate(file);
                      event.target.value = "";
                    }}
                  />
                  <Button
                    type="button"
                    variant="outline"
                    className="mt-4 w-full rounded-2xl"
                    disabled={workspaceBusy}
                    onClick={() => fileInputRef.current?.click()}
                  >
                    <Upload className="mr-2 h-4 w-4" />
                    {importMutation.isPending ? "Импортирую…" : "Импортировать .kpack"}
                  </Button>
                </AppPanel>
              </>
            ) : null}

            {workspaceSurface === "teach" ? (
              <>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Teach text" title="Прямой ingest текста" description="Добавь материал в live-memory без выхода из shell." />
                  <div className="mt-4 space-y-3">
                    <input value={teachTitle} onChange={(event) => setTeachTitle(event.target.value)} className="v3-input" placeholder="Название" />
                    <textarea
                      value={teachText}
                      onChange={(event) => setTeachText(event.target.value)}
                      className="v3-input min-h-[12rem] resize-y"
                      placeholder="Текст для формульной памяти"
                    />
                    <Button
                      type="button"
                      className="w-full rounded-2xl"
                      disabled={workspaceBusy || teachText.trim().length < 10}
                      onClick={() => teachTextMutation.mutate()}
                    >
                      {teachTextMutation.isPending ? "Добавляю текст…" : "Добавить текст"}
                    </Button>
                  </div>
                </AppPanel>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Teach URL" title="Web ingest" description="Передай URL в роевой ingest и обнови знания." />
                  <div className="mt-4 space-y-3">
                    <input value={teachUrl} onChange={(event) => setTeachUrl(event.target.value)} className="v3-input" placeholder="https://..." />
                    <Button
                      type="button"
                      variant="outline"
                      className="w-full rounded-2xl"
                      disabled={workspaceBusy || !teachUrl.trim()}
                      onClick={() => teachUrlMutation.mutate()}
                    >
                      <Link2 className="mr-2 h-4 w-4" />
                      {teachUrlMutation.isPending ? "Добавляю URL…" : "Добавить URL"}
                    </Button>
                  </div>
                </AppPanel>
              </>
            ) : null}

            {workspaceSurface === "quality" ? (
              <>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Quality" title="AI benchmark history" description="Серверные метрики держатся в query-layer, а не в первичной навигации." />
                  <div className="mt-4 grid grid-cols-2 gap-3">
                    <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
                      <p className="text-muted">Score avg</p>
                      <p className="mt-1 font-semibold">{qualityTrend?.score_avg?.toFixed(3) ?? "—"}</p>
                    </div>
                    <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
                      <p className="text-muted">Latency p95</p>
                      <p className="mt-1 font-semibold">{qualityTrend?.latency_p95_ms_avg ? Math.round(qualityTrend.latency_p95_ms_avg) : "—"} ms</p>
                    </div>
                  </div>
                </AppPanel>
                <AppPanel className="px-4 py-4">
                  <SectionTitle eyebrow="Categories" title="Лучшие категории" description="Взвешенный pass-rate по quality benchmark." />
                  <div className="mt-4 space-y-2">
                    {qualityTop.length ? qualityTop.map(([name, value]) => (
                      <div key={name} className="flex items-center justify-between rounded-2xl border border-foreground/6 bg-background px-3 py-3 text-sm">
                        <span className="font-medium text-foreground">{name}</span>
                        <span className="text-muted">{(Number(value) * 100).toFixed(1)}%</span>
                      </div>
                    )) : <p className="text-sm text-muted">Нет достаточных данных benchmark.</p>}
                  </div>
                </AppPanel>
              </>
            ) : null}

            {workspaceSurface === "knowledge" ? (
              <KnowledgeGraphTab />
            ) : null}

            {workspaceSurface === "learning" ? (
              <LearningTab />
            ) : null}
          </div>
        </div>
      </SheetContent>
    </Sheet>
  );
}

// ============================================================================
// Knowledge Graph Tab
// ============================================================================

function KnowledgeGraphTab() {
  const analyticsQuery = useQuery({
    queryKey: ["knowledge-analytics"],
    queryFn: getKnowledgeAnalytics,
  });

  const graphDataQuery = useQuery({
    queryKey: ["knowledge-graph"],
    queryFn: () => getKnowledgeGraphData(),
  });

  return (
    <>
      <AppPanel className="px-4 py-4">
        <SectionTitle
          eyebrow="Knowledge Graph"
          title="Граф знаний"
          description="Интерактивная визуализация связей между концептами"
        />
        <div className="mt-4 h-[500px] rounded-2xl border border-foreground/6 bg-background overflow-hidden">
          <KnowledgeGraphViz
            data={graphDataQuery.data ?? { nodes: [], links: [] }}
            loading={graphDataQuery.isLoading}
          />
        </div>
      </AppPanel>

      {analyticsQuery.data && (
        <AppPanel className="px-4 py-4">
          <SectionTitle
            eyebrow="Analytics"
            title="Аналитика"
            description="Статистика по всем подсистемам знаний"
          />
          <div className="mt-4 grid grid-cols-2 gap-3">
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Паттерны</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.knowledge_graph.patterns}</p>
            </div>
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Рёбра</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.knowledge_graph.edges}</p>
            </div>
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Формулы</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.formula_pool.size}</p>
            </div>
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Embeddings</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.embeddings.vocab_size}</p>
            </div>
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Документы</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.knowledge_graph.documents}</p>
            </div>
            <div className="rounded-2xl border border-foreground/6 bg-background px-3 py-3">
              <p className="text-muted">Токены</p>
              <p className="mt-1 font-semibold">{analyticsQuery.data.knowledge_graph.tokens}</p>
            </div>
          </div>
        </AppPanel>
      )}
    </>
  );
}

// ============================================================================
// Learning Tab
// ============================================================================

function LearningTab() {
  return (
    <div className="h-full">
      <LearningDashboard />
    </div>
  );
}
