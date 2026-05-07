import { useState, useEffect } from "react";
import {
  BarChart3,
  Bot,
  CheckCircle,
  FileText,
  Layers,
  PenTool,
  Play,
  RefreshCw,
  Save,
  Search,
  Share2,
  Video,
  X,
  Zap,
} from "lucide-react";
import type { LucideIcon } from "lucide-react";

// --- Types ---
interface ContentItem {
    id: string;
    topic: string;
    status: string;
    content: string;
    analysis_report: string;
    platform: string;
    views: number;
    engagement_rate: number;
    romi: number;
}

interface TrendInsight {
    id: string;
    niche: string;
    title: string;
    score: number;
    rationale: string;
    source: string;
}

interface VideoReference {
    id: string;
    niche: string;
    title: string;
    url: string;
    channel: string;
    views: number;
    engagement_rate: number;
    reason: string;
}

interface NodeType {
  id: string;
  type: "event" | "agent" | "service" | "output";
  label: string;
  subLabel?: string;
  icon: LucideIcon;
  x: number;
  y: number;
  color: string;
  status: "idle" | "working";
}

// --- Components ---
const TopBar = () => (
  <div className="flex z-50 h-14 items-center justify-between border-b border-slate-700 bg-[#14151a] px-4 text-sm font-medium text-slate-300">
    <div className="flex items-center gap-6">
      <div className="flex items-center gap-2 text-rose-500">
        <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-gradient-to-br from-rose-500 to-orange-500 font-bold text-white shadow-lg shadow-orange-500/20">
            <span className="text-lg">K</span>
        </div>
        <span className="font-bold text-white text-lg">Контент‑фабрика Колибри</span>
      </div>
    </div>
    <div className="flex items-center gap-4">
       <div className="flex items-center gap-2 px-3 py-1 rounded bg-slate-800 text-slate-400 text-xs">
           <div className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse" />
           Система активна
       </div>
    </div>
  </div>
);

const ContentEditorModal = ({ item, onClose, onSave }: { item: ContentItem, onClose: () => void, onSave: (id: string, content: string) => void }) => {
    const [localContent, setLocalContent] = useState(item.content);
    const [activeTab, setActiveTab] = useState<'content' | 'analysis'>('content');

    return (
        <div className="fixed inset-0 z-[100] flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
            <div className="w-full max-w-2xl bg-slate-900 border border-slate-700 rounded-xl shadow-2xl flex flex-col max-h-[90vh]">
                <div className="flex items-center justify-between p-4 border-b border-slate-700">
                    <h3 className="text-white font-bold flex items-center gap-2">
                        <PenTool size={16} className="text-indigo-400" />
                        Редактор: <span className="text-indigo-300 truncate max-w-[300px]">{item.topic}</span>
                    </h3>
                    <button onClick={onClose} className="text-slate-400 hover:text-white transition-colors">
                        <X size={20} />
                    </button>
                </div>
                
                {/* Tabs */}
                <div className="flex border-b border-slate-700 px-4">
                    <button 
                        onClick={() => setActiveTab('content')}
                        className={`py-3 px-4 text-sm font-medium border-b-2 transition-colors ${activeTab === 'content' ? 'border-indigo-500 text-white' : 'border-transparent text-slate-400 hover:text-slate-200'}`}
                    >
                        Контент / Сценарий
                    </button>
                    {item.analysis_report && (
                        <button 
                            onClick={() => setActiveTab('analysis')}
                            className={`py-3 px-4 text-sm font-medium border-b-2 transition-colors ${activeTab === 'analysis' ? 'border-emerald-500 text-white' : 'border-transparent text-slate-400 hover:text-slate-200'}`}
                        >
                            Отчет об анализе
                        </button>
                    )}
                </div>

                <div className="p-4 flex-1 overflow-hidden flex flex-col bg-slate-800/50">
                    {activeTab === 'content' ? (
                        <textarea 
                            className="flex-1 w-full bg-slate-800 border border-slate-600 rounded-lg p-3 text-slate-200 font-mono text-sm leading-relaxed resize-none focus:ring-2 focus:ring-indigo-500 focus:border-transparent outline-none"
                            value={localContent}
                            onChange={(e) => setLocalContent(e.target.value)}
                            placeholder="Контент пуст..."
                        />
                    ) : (
                        <div className="flex-1 overflow-y-auto bg-slate-900 rounded-lg p-4 border border-slate-700">
                            <pre className="text-slate-300 font-mono text-sm whitespace-pre-wrap font-sans">
                                {item.analysis_report}
                            </pre>
                        </div>
                    )}
                </div>

                <div className="p-4 border-t border-slate-700 flex justify-end gap-3">
                    <button onClick={onClose} className="px-4 py-2 text-slate-400 hover:text-white text-sm font-medium transition-colors">
                        Отмена
                    </button>
                    <button 
                        onClick={() => onSave(item.id, localContent)}
                        className="px-4 py-2 bg-indigo-600 hover:bg-indigo-500 text-white rounded-lg text-sm font-medium flex items-center gap-2 shadow-lg shadow-indigo-500/20 transition-all hover:scale-105"
                    >
                        <Save size={16} />
                        Сохранить изменения
                    </button>
                </div>
            </div>
        </div>
    );
};

export function DevDashboard() {
  const CX = 800;
  const CY = 400;

  const [items, setItems] = useState<ContentItem[]>([]);
  const [loading, setLoading] = useState(false);
    const [niche, setNiche] = useState("Новости ИИ");
  const [editingItem, setEditingItem] = useState<ContentItem | null>(null);
    const [trends, setTrends] = useState<TrendInsight[]>([]);
    const [videos, setVideos] = useState<VideoReference[]>([]);
    const [trendLoading, setTrendLoading] = useState(false);
    const [videoLoading, setVideoLoading] = useState(false);

  const [nodes] = useState<NodeType[]>([
    { id: "analysis", type: "event", label: "Анализ", icon: Search, x: CX - 400, y: CY, color: "bg-emerald-600 border-emerald-400", status: "idle" },
    { id: "idea", type: "agent", label: "Идеи", icon: Zap, x: CX - 250, y: CY, color: "bg-amber-600 border-amber-400", status: "idle" },
    { id: "approval_1", type: "agent", label: "Согласование", icon: CheckCircle, x: CX - 100, y: CY, color: "bg-indigo-600 border-indigo-400", status: "idle" },
    { id: "production", type: "agent", label: "Производство", icon: Bot, x: CX + 50, y: CY, color: "bg-blue-600 border-blue-400", status: "idle" },
    { id: "publishing", type: "output", label: "Публикация", icon: Share2, x: CX + 200, y: CY, color: "bg-purple-600 border-purple-400", status: "idle" },
    { id: "analytics", type: "service", label: "Аналитика", icon: BarChart3, x: CX + 350, y: CY, color: "bg-rose-600 border-rose-400", status: "idle" },
  ]);

  const fetchItems = async () => {
      try {
          const res = await fetch("/api/factory/items");
          if(res.ok) {
              const data = (await res.json()) as ContentItem[];
              setItems(data);
          }
      } catch (e) {
          console.error("Failed to fetch items", e);
      }
  };

  useEffect(() => {
      fetchItems();
      const interval = setInterval(fetchItems, 5000); 
      return () => clearInterval(interval);
  }, []);

  const startCycle = async () => {
      setLoading(true);
      try {
          await fetch("/api/factory/start_cycle", {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ niche, count: 3 })
          });
          await fetchItems();
      } finally {
          setLoading(false);
      }
  };

  const handleAction = async (id: string, action: string) => {
      await fetch(`/api/factory/items/${id}/${action}`, { method: "POST" });
      await fetchItems();
  };

  const handleSaveContent = async (id: string, content: string) => {
      try {
          await fetch(`/api/factory/items/${id}/content`, { 
              method: "PUT",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ content })
          });
          setEditingItem(null);
          await fetchItems();
      } catch(e) {
          console.error("Failed to save", e);
      }
  };

  const handleAnalyzeTrends = async () => {
      setTrendLoading(true);
      try {
          const res = await fetch("/api/factory/trends/analyze", {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ niche, limit: 5 })
          });
          if (res.ok) {
              const data = (await res.json()) as TrendInsight[];
              setTrends(data);
          }
      } finally {
          setTrendLoading(false);
      }
  };

  const handleFindVideos = async () => {
      setVideoLoading(true);
      try {
          const res = await fetch("/api/factory/videos/best", {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ niche, limit: 5 })
          });
          if (res.ok) {
              const data = (await res.json()) as VideoReference[];
              setVideos(data);
          }
      } finally {
          setVideoLoading(false);
      }
  };

  const getStatusColor = (status: string) => {
      switch(status) {
          case 'analytics': return 'bg-rose-500';
          case 'publishing': return 'bg-purple-500';
          case 'idea_approval': return 'bg-amber-500';
          case 'content_approval': return 'bg-indigo-500';
          default: return 'bg-slate-500';
      }
  };

  return (
    <div className="flex h-screen w-full flex-col bg-[#14151a] overflow-hidden">
      <TopBar />
      
      {editingItem && (
          <ContentEditorModal 
              item={editingItem} 
              onClose={() => setEditingItem(null)} 
              onSave={handleSaveContent} 
          />
      )}

      <div className="relative flex-1 bg-[#14151a] overflow-hidden flex">
        
        {/* LEFT PANEL */}
        <div className="w-96 bg-slate-900/95 backdrop-blur border-r border-slate-700 p-4 z-40 overflow-y-auto flex flex-col gap-4 shadow-2xl">
             
             {/* Creating New */}
             <div className="p-4 bg-slate-800 rounded-lg border border-slate-700 shadow-md">
                 <h3 className="text-white font-bold mb-3 flex items-center gap-2">
                     <Zap size={16} className="text-emerald-500" />
                     Новая кампания
                 </h3>
                 <input 
                    className="w-full bg-slate-900 border border-slate-700 rounded p-2 text-white text-sm mb-2 focus:border-emerald-500 outline-none transition-colors"
                    value={niche}
                    onChange={(e) => setNiche(e.target.value)}
                    placeholder="Введите нишу (например, Крипто)"
                 />
                 <button 
                    onClick={startCycle}
                    disabled={loading}
                    className="w-full py-2 bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center justify-center gap-2 transition-all">
                     {loading ? <RefreshCw className="animate-spin" size={14} /> : <Play size={14} />}
                     Запуск анализа и идей
                 </button>
             </div>

             {/* Trend Agent */}
             <div className="p-4 bg-slate-800 rounded-lg border border-slate-700 shadow-md">
                 <h3 className="text-white font-bold mb-3 flex items-center gap-2">
                     <Search size={16} className="text-emerald-500" />
                     Trend Agent
                 </h3>
                 <button
                     onClick={handleAnalyzeTrends}
                     disabled={trendLoading}
                     className="w-full py-2 bg-emerald-600/20 text-emerald-400 hover:bg-emerald-600 hover:text-white rounded text-sm font-medium flex items-center justify-center gap-2 transition-all"
                 >
                     {trendLoading ? <RefreshCw className="animate-spin" size={14} /> : <Play size={14} />}
                     Анализ трендов
                 </button>
                 {trends.length > 0 && (
                     <div className="mt-3 space-y-2">
                         {trends.map((trend) => (
                             <div key={trend.id} className="bg-slate-900/60 border border-slate-700 rounded p-2">
                                 <div className="text-xs text-slate-400">Скоринг: {trend.score}</div>
                                 <div className="text-sm text-white font-medium">{trend.title}</div>
                                 <div className="text-xs text-slate-500 mt-1">{trend.rationale}</div>
                             </div>
                         ))}
                     </div>
                 )}
             </div>

             {/* Best Video Finder */}
             <div className="p-4 bg-slate-800 rounded-lg border border-slate-700 shadow-md">
                 <h3 className="text-white font-bold mb-3 flex items-center gap-2">
                     <Video size={16} className="text-emerald-500" />
                     Best Video Finder
                 </h3>
                 <button
                     onClick={handleFindVideos}
                     disabled={videoLoading}
                     className="w-full py-2 bg-emerald-600/20 text-emerald-400 hover:bg-emerald-600 hover:text-white rounded text-sm font-medium flex items-center justify-center gap-2 transition-all"
                 >
                     {videoLoading ? <RefreshCw className="animate-spin" size={14} /> : <Play size={14} />}
                     Поиск лучших видео
                 </button>
                 {videos.length > 0 && (
                     <div className="mt-3 space-y-2">
                         {videos.map((video) => (
                             <div key={video.id} className="bg-slate-900/60 border border-slate-700 rounded p-2">
                                 <div className="text-xs text-slate-400">Просмотры: {video.views}</div>
                                 <div className="text-sm text-white font-medium">{video.title}</div>
                                 <div className="text-xs text-slate-500">{video.channel}</div>
                                 <a href={video.url} target="_blank" rel="noreferrer" className="text-xs text-emerald-400 hover:text-emerald-300">
                                     Открыть видео
                                 </a>
                             </div>
                         ))}
                     </div>
                 )}
             </div>

             {/* Items List */}
             <div className="space-y-3 pb-8">
                 <h4 className="text-slate-400 text-xs font-semibold uppercase tracking-wider flex items-center gap-2">
                     <Layers size={12} />
                     Активный конвейер
                 </h4>
                 {items.length === 0 && <div className="text-slate-600 text-sm text-center py-4 italic">Нет контента в конвейере</div>}
                 
                 {items.map(item => (
                     <div key={item.id} className="bg-slate-800 p-3 rounded border border-slate-700 hover:border-slate-500 transition-all group relative overflow-hidden shadow-sm">
                         
                         <div className={`absolute left-0 top-0 bottom-0 w-1 ${getStatusColor(item.status)}`} />
                         
                         <div className="pl-3">
                            <div className="flex justify-between items-start mb-1">
                                <span className="text-[10px] font-mono text-slate-400 bg-slate-900 px-1 py-0.5 rounded border border-slate-700">
                                    {item.status.replace('_', ' ')}
                                </span>
                                {item.status === 'analytics' && (
                                    <div className="flex items-center gap-1 text-[10px] text-emerald-400 font-bold">
                                        <BarChart3 size={10} />
                                        ROMI: {item.romi}%
                                    </div>
                                )}
                            </div>
                            <div className="text-white text-sm font-medium leading-snug mb-2 pr-4">{item.topic}</div>
                            
                            <div className="mb-2">
                                 <button 
                                    onClick={() => setEditingItem(item)}
                                    className="text-[10px] flex items-center gap-1 text-slate-400 hover:text-indigo-400 transition-colors"
                                 >
                                     <FileText size={10} />
                                     Просмотр анализа / контента
                                 </button>
                            </div>

                            {/* Actions */}
                            <div className="flex gap-2 mt-2">
                                {item.status === 'idea_approval' && (
                                    <button onClick={() => handleAction(item.id, 'approve_idea')} className="flex-1 py-1.5 bg-amber-600/20 text-amber-500 border border-amber-600/50 hover:bg-amber-600 hover:text-white rounded text-xs font-medium transition-colors">
                                        Утвердить идею
                                    </button>
                                )}
                                {item.status === 'production' && (
                                    <button onClick={() => handleAction(item.id, 'produce')} className="flex-1 py-1.5 bg-blue-600/20 text-blue-500 border border-blue-600/50 hover:bg-blue-600 hover:text-white rounded text-xs font-medium transition-colors">
                                        Сгенерировать сценарий
                                    </button>
                                )}
                                {item.status === 'content_approval' && (
                                    <button onClick={() => handleAction(item.id, 'approve_content')} className="flex-1 py-1.5 bg-indigo-600/20 text-indigo-500 border border-indigo-600/50 hover:bg-indigo-600 hover:text-white rounded text-xs font-medium transition-colors">
                                        Утвердить сценарий
                                    </button>
                                )}
                                {item.status === 'publishing' && (
                                    <button onClick={() => handleAction(item.id, 'publish')} className="flex-1 py-1.5 bg-purple-600/20 text-purple-500 border border-purple-600/50 hover:bg-purple-600 hover:text-white rounded text-xs font-medium transition-colors">
                                        Опубликовать
                                    </button>
                                )}
                                {item.status === 'analytics' && (
                                    <button onClick={() => handleAction(item.id, 'refresh_analytics')} className="flex-1 py-1.5 bg-slate-700 text-slate-400 hover:bg-slate-600 hover:text-white rounded text-xs font-medium transition-colors flex justify-center items-center gap-1">
                                        <RefreshCw size={10} /> Обновить
                                    </button>
                                )}
                            </div>
                         </div>
                     </div>
                 ))}
             </div>
        </div>

        {/* RIGHT PANEL: Visualization (Graph) */}
        <div className="flex-1 relative bg-[#0f1014]">
            <div 
                className="absolute inset-0 opacity-20"
                style={{
                    backgroundImage: `radial-gradient(circle at 1px 1px, #475569 1px, transparent 0)`,
                    backgroundSize: '24px 24px'
                }}
            />
            
            {/* Visual Nodes */}
            {nodes.map(node => (
                <div 
                key={node.id}
                className="absolute flex flex-col items-center justify-center pointer-events-none"
                style={{ left: node.x, top: node.y, transform: `translate(-50%, -50%)` }}
                >
                    <div className={`relative flex items-center justify-center shadow-2xl ${
                        node.type === "event" ? "h-16 w-16 rounded-full " + node.color :
                        "h-14 w-14 rounded-lg bg-slate-800 border-2 " + node.color.split(' ')[1]
                    }`}>
                        <node.icon size={24} className="text-white" />
                    </div>
                    <div className="mt-2 text-[10px] font-bold text-slate-500 uppercase tracking-widest">
                        {node.label}
                    </div>
                </div>
            ))}
            
            {/* Connecting Lines */}
            <svg className="absolute inset-0 w-full h-full pointer-events-none opacity-50">
                <line x1={CX - 400 + 32} y1={CY} x2={CX - 250 - 28} y2={CY} stroke="#334155" strokeWidth="2" />
                <line x1={CX - 250 + 28} y1={CY} x2={CX - 100 - 28} y2={CY} stroke="#334155" strokeWidth="2" />
                <line x1={CX - 100 + 28} y1={CY} x2={CX + 50 - 28} y2={CY} stroke="#334155" strokeWidth="2" />
                <line x1={CX + 50 + 28} y1={CY} x2={CX + 200 - 28} y2={CY} stroke="#334155" strokeWidth="2" />
                <line x1={CX + 200 + 28} y1={CY} x2={CX + 350 - 28} y2={CY} stroke="#334155" strokeWidth="2" />
            </svg>

        </div>
      </div>
    </div>
  );
}
