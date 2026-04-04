/**
 * KnowledgeGraphViz.tsx — Интерактивная визуализация графа знаний Колибри
 * 
 * Использует D3.js force-directed layout для отображения:
 * - Узлы = концепты/слова
 * - Рёбра = связи (co-occurrence)
 * - Размер узла = frequency
 * - Цвет = domain/category
 * 
 * Интерактив:
 * - Зум/пан
 * - Клик по узлу → детали
 * - Drag узлов
 * - Фильтр по домену
 */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import * as d3 from "d3";
import { motion, AnimatePresence } from "framer-motion";
import { X, ZoomIn, ZoomOut, Maximize2, Filter } from "lucide-react";
import { Button } from "@/components/ui/button";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";

// ============================================================================
// Types
// ============================================================================

interface GraphNode {
  id: string;
  label: string;
  frequency: number;
  fitness: number;
  domain: string;
  x?: number;
  y?: number;
  fx?: number | null;
  fy?: number | null;
}

interface GraphEdge {
  source: string;
  target: string;
  weight: number;
}

interface GraphData {
  nodes: GraphNode[];
  links: GraphEdge[];
}

interface KnowledgeGraphVizProps {
  data: GraphData;
  loading?: boolean;
  onNodeClick?: (node: GraphNode) => void;
}

// ============================================================================
// Color scheme
// ============================================================================

const DOMAIN_COLORS: Record<string, string> = {
  ai: "#38bdf8",
  math: "#a78bfa",
  physics: "#f472b6",
  biology: "#34d399",
  programming: "#fbbf24",
  history: "#fb923c",
  tech: "#60a5fa",
  research: "#c084fc",
  default: "#94a3b8",
};

function getDomainColor(domain: string): string {
  return DOMAIN_COLORS[domain.toLowerCase()] || DOMAIN_COLORS.default;
}

// ============================================================================
// Component
// ============================================================================

export function KnowledgeGraphViz({ data, loading, onNodeClick }: KnowledgeGraphVizProps) {
  const svgRef = useRef<SVGSVGElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [selectedNode, setSelectedNode] = useState<GraphNode | null>(null);
  const [zoomLevel, setZoomLevel] = useState(1);
  const [filterDomain, setFilterDomain] = useState<string | null>(null);
  const [showFilter, setShowFilter] = useState(false);

  // Extract unique domains
  const domains = useMemo(() => {
    const domainSet = new Set(data.nodes.map((n) => n.domain));
    return Array.from(domainSet).sort();
  }, [data.nodes]);

  // Filter data by domain
  const filteredData = useMemo(() => {
    if (!filterDomain) return data;
    const nodes = data.nodes.filter((n) => n.domain === filterDomain);
    const nodeIds = new Set(nodes.map((n) => n.id));
    const links = data.links.filter(
      (l) => nodeIds.has(l.source as string) && nodeIds.has(l.target as string)
    );
    return { nodes, links };
  }, [data, filterDomain]);

  // D3 visualization
  const renderGraph = useCallback(() => {
    if (!svgRef.current || !containerRef.current) return;
    if (filteredData.nodes.length === 0) return;

    const container = containerRef.current;
    const width = container.clientWidth;
    const height = container.clientHeight;

    // Clear previous
    d3.select(svgRef.current).selectAll("*").remove();

    const svg = d3
      .select(svgRef.current)
      .attr("width", width)
      .attr("height", height)
      .attr("viewBox", [0, 0, width, height]);

    // Zoom behavior
    const zoom = d3
      .zoom<SVGSVGElement, unknown>()
      .scaleExtent([0.1, 4])
      .on("zoom", (event) => {
        g.attr("transform", event.transform);
        setZoomLevel(event.transform.k);
      });

    svg.call(zoom);

    const g = svg.append("g");

    // Force simulation
    const simulation = d3
      .forceSimulation(filteredData.nodes)
      .force(
        "link",
        d3
          .forceLink(filteredData.links)
          .id((d: any) => d.id)
          .distance(100)
          .strength((d) => Math.sqrt(d.weight))
      )
      .force("charge", d3.forceManyBody().strength(-300))
      .force("center", d3.forceCenter(width / 2, height / 2))
      .force("collision", d3.forceCollide().radius(30));

    // Edges
    const link = g
      .selectAll("line")
      .data(filteredData.links)
      .join("line")
      .attr("stroke", "#475569")
      .attr("stroke-opacity", 0.4)
      .attr("stroke-width", (d) => Math.sqrt(d.weight) * 2);

    // Nodes
    const node = g
      .selectAll<SVGGElement, GraphNode>("g")
      .data(filteredData.nodes)
      .join("g")
      .attr("cursor", "pointer")
      .call(
        d3
          .drag<SVGGElement, GraphNode>()
          .on("start", (event, d) => {
            if (!event.active) simulation.alphaTarget(0.3).restart();
            d.fx = d.x;
            d.fy = d.y;
          })
          .on("drag", (event, d) => {
            d.fx = event.x;
            d.fy = event.y;
          })
          .on("end", (event, d) => {
            if (!event.active) simulation.alphaTarget(0);
            d.fx = null;
            d.fy = null;
          })
      );

    // Node circles
    node
      .append("circle")
      .attr("r", (d) => Math.max(8, Math.min(30, Math.sqrt(d.frequency) * 3)))
      .attr("fill", (d) => getDomainColor(d.domain))
      .attr("stroke", "#1e293b")
      .attr("stroke-width", 2)
      .on("click", (event, d) => {
        setSelectedNode(d);
        onNodeClick?.(d);
      });

    // Node labels
    node
      .append("text")
      .text((d) => d.label)
      .attr("x", (d) => Math.max(8, Math.min(30, Math.sqrt(d.frequency) * 3)) + 5)
      .attr("y", 4)
      .attr("fill", "#e2e8f0")
      .attr("font-size", "11px")
      .attr("font-family", "system-ui, -apple-system, sans-serif")
      .attr("pointer-events", "none")
      .attr("text-shadow", "0 1px 3px rgba(0,0,0,0.8)");

    // Tick
    simulation.on("tick", () => {
      link
        .attr("x1", (d: any) => d.source.x)
        .attr("y1", (d: any) => d.source.y)
        .attr("x2", (d: any) => d.target.x)
        .attr("y2", (d: any) => d.target.y);

      node.attr("transform", (d) => `translate(${d.x},${d.y})`);
    });

    // Cleanup
    return () => {
      simulation.stop();
    };
  }, [filteredData, onNodeClick]);

  useEffect(() => {
    const cleanup = renderGraph();
    return () => {
      cleanup?.();
    };
  }, [renderGraph]);

  // Zoom controls
  const handleZoomIn = () => {
    if (!svgRef.current) return;
    const svg = d3.select(svgRef.current);
    svg.transition().duration(300).call(
      d3.zoom<SVGSVGElement, unknown>().transform,
      d3.zoomIdentity.scale(zoomLevel * 1.3)
    );
  };

  const handleZoomOut = () => {
    if (!svgRef.current) return;
    const svg = d3.select(svgRef.current);
    svg.transition().duration(300).call(
      d3.zoom<SVGSVGElement, unknown>().transform,
      d3.zoomIdentity.scale(zoomLevel * 0.7)
    );
  };

  const handleFitToScreen = () => {
    if (!svgRef.current || !containerRef.current) return;
    const svg = d3.select(svgRef.current);
    const container = containerRef.current;
    svg.transition().duration(500).call(
      d3.zoom<SVGSVGElement, unknown>().transform,
      d3.zoomIdentity.translate(container.clientWidth / 2, container.clientHeight / 2).scale(0.8)
    );
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-full text-slate-400">
        <div className="animate-pulse">Загрузка графа...</div>
      </div>
    );
  }

  if (data.nodes.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center h-full text-slate-400 gap-4">
        <p>Граф знаний пуст</p>
        <p className="text-sm text-slate-500">
          Обучите модель на текстах, чтобы увидеть визуализацию
        </p>
      </div>
    );
  }

  return (
    <div className="relative w-full h-full flex flex-col">
      {/* Toolbar */}
      <div className="absolute top-2 left-2 right-2 z-10 flex items-center justify-between gap-2">
        <div className="flex items-center gap-1">
          <Button variant="outline" size="icon" onClick={handleZoomIn} title="Приблизить">
            <ZoomIn className="h-4 w-4" />
          </Button>
          <Button variant="outline" size="icon" onClick={handleZoomOut} title="Отдалить">
            <ZoomOut className="h-4 w-4" />
          </Button>
          <Button variant="outline" size="icon" onClick={handleFitToScreen} title="Вместить">
            <Maximize2 className="h-4 w-4" />
          </Button>
        </div>
        <div className="flex items-center gap-1">
          <Button
            variant="outline"
            size="icon"
            onClick={() => setShowFilter(!showFilter)}
            title="Фильтр по домену"
          >
            <Filter className="h-4 w-4" />
          </Button>
          <span className="text-xs text-slate-400 bg-slate-800/80 px-2 py-1 rounded">
            {data.nodes.length} узлов, {data.links.length} связей
          </span>
        </div>
      </div>

      {/* Filter panel */}
      <AnimatePresence>
        {showFilter && (
          <motion.div
            initial={{ opacity: 0, y: -10 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -10 }}
            className="absolute top-12 left-2 z-10 bg-slate-900/95 backdrop-blur border border-slate-700 rounded-lg p-3 shadow-xl"
          >
            <div className="flex items-center justify-between mb-2">
              <span className="text-sm font-medium text-slate-200">Домен</span>
              <Button variant="ghost" size="icon" onClick={() => setShowFilter(false)}>
                <X className="h-3 w-3" />
              </Button>
            </div>
            <div className="flex flex-wrap gap-1">
              <Button
                variant={filterDomain ? "outline" : "default"}
                onClick={() => setFilterDomain(null)}
                className="text-xs h-7 px-2"
              >
                Все
              </Button>
              {domains.map((domain) => (
                <Button
                  key={domain}
                  variant={filterDomain === domain ? "default" : "outline"}
                  onClick={() => setFilterDomain(domain)}
                  className="text-xs h-7 px-2"
                  style={{
                    borderColor: filterDomain === domain ? getDomainColor(domain) : undefined,
                    backgroundColor:
                      filterDomain === domain ? getDomainColor(domain) + "33" : undefined,
                  }}
                >
                  {domain}
                </Button>
              ))}
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* SVG container */}
      <div ref={containerRef} className="flex-1 w-full h-full">
        <svg ref={svgRef} className="w-full h-full" />
      </div>

      {/* Node details panel */}
      <AnimatePresence>
        {selectedNode && (
          <motion.div
            initial={{ opacity: 0, x: 300 }}
            animate={{ opacity: 1, x: 0 }}
            exit={{ opacity: 0, x: 300 }}
            className="absolute top-2 right-2 w-72 bg-slate-900/95 backdrop-blur border border-slate-700 rounded-lg shadow-xl overflow-hidden"
          >
            <div className="flex items-center justify-between p-3 border-b border-slate-700">
              <h3 className="font-semibold text-slate-100">{selectedNode.label}</h3>
              <Button variant="ghost" size="icon" onClick={() => setSelectedNode(null)}>
                <X className="h-4 w-4" />
              </Button>
            </div>
            <ScrollArea className="h-48">
              <div className="p-3 space-y-2">
                <div className="flex justify-between text-sm">
                  <span className="text-slate-400">Домен</span>
                  <span
                    className="font-medium"
                    style={{ color: getDomainColor(selectedNode.domain) }}
                  >
                    {selectedNode.domain}
                  </span>
                </div>
                <div className="flex justify-between text-sm">
                  <span className="text-slate-400">Частота</span>
                  <span className="font-medium text-slate-200">{selectedNode.frequency}</span>
                </div>
                <div className="flex justify-between text-sm">
                  <span className="text-slate-400">Fitness</span>
                  <span className="font-medium text-slate-200">
                    {selectedNode.fitness.toFixed(4)}
                  </span>
                </div>
              </div>
            </ScrollArea>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}
