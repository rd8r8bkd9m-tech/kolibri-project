import React, { useState, useEffect, useRef, useMemo } from 'react';
import { Terminal, Cpu, Database, Activity, Settings, X, Minus, Square, Wifi, Battery, Volume2 } from 'lucide-react';

// --- Types ---
type WindowId = 'terminal' | 'genome' | 'monitor' | 'settings';

interface WindowState {
  id: WindowId;
  title: string;
  isOpen: boolean;
  isMinimized: boolean;
  zIndex: number;
  content: React.ReactNode;
}

// --- Components ---

const TopBar = () => {
  const [time, setTime] = useState(new Date());
  
  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  return (
    <div className="h-7 bg-black/80 text-white flex items-center justify-between px-4 text-sm font-medium backdrop-blur-md select-none sticky top-0 z-50">
      <div className="flex items-center gap-4">
        <span className="font-bold">Activities</span>
        <span className="text-gray-400 hover:text-white cursor-pointer transition-colors">Kolibri OS</span>
      </div>
      <div className="absolute left-1/2 -translate-x-1/2">
        {time.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })}
      </div>
      <div className="flex items-center gap-3">
        <Wifi size={16} />
        <Volume2 size={16} />
        <Battery size={16} />
      </div>
    </div>
  );
};

const Dock = ({ onOpen, activeWindows }: { onOpen: (id: WindowId) => void, activeWindows: WindowId[] }) => {
  const apps = [
    { id: 'terminal', icon: Terminal, color: 'text-orange-500', label: 'Terminal' },
    { id: 'genome', icon: Database, color: 'text-blue-500', label: 'Genome Viewer' },
    { id: 'monitor', icon: Activity, color: 'text-green-500', label: 'System Monitor' },
    { id: 'settings', icon: Settings, color: 'text-gray-400', label: 'Settings' },
  ];

  return (
    <div className="fixed left-2 top-1/2 -translate-y-1/2 bg-black/40 backdrop-blur-xl p-2 rounded-2xl border border-white/10 flex flex-col gap-4 z-50">
      {apps.map((app) => (
        <button
          key={app.id}
          onClick={() => onOpen(app.id as WindowId)}
          className="relative group p-3 hover:bg-white/10 rounded-xl transition-all duration-200"
          title={app.label}
        >
          <app.icon size={28} className={app.color} />
          {activeWindows.includes(app.id as WindowId) && (
            <div className="absolute left-0 top-1/2 -translate-y-1/2 w-1 h-1 bg-white rounded-full -ml-1"></div>
          )}
          <div className="absolute left-full ml-4 px-2 py-1 bg-gray-800 text-white text-xs rounded opacity-0 group-hover:opacity-100 transition-opacity whitespace-nowrap pointer-events-none">
            {app.label}
          </div>
        </button>
      ))}
    </div>
  );
};

const WindowFrame = ({ 
  window, 
  onClose, 
  onFocus,
  children 
}: { 
  window: WindowState, 
  onClose: (id: WindowId) => void,
  onFocus: (id: WindowId) => void,
  children: React.ReactNode 
}) => {
  if (!window.isOpen || window.isMinimized) return null;

  return (
    <div 
      className="absolute bg-[#1e1e1e] rounded-lg shadow-2xl border border-white/10 flex flex-col overflow-hidden animate-in fade-in zoom-in duration-200"
      style={{ 
        zIndex: window.zIndex,
        left: window.id === 'terminal' ? '100px' : window.id === 'genome' ? '150px' : '200px',
        top: window.id === 'terminal' ? '50px' : window.id === 'genome' ? '80px' : '110px',
        width: '800px',
        height: '500px'
      }}
      onMouseDown={() => onFocus(window.id)}
    >
      <div className="h-9 bg-[#2d2d2d] flex items-center justify-between px-3 select-none">
        <div className="font-medium text-gray-300 text-sm flex items-center gap-2">
          {window.id === 'terminal' && <Terminal size={14} />}
          {window.id === 'genome' && <Database size={14} />}
          {window.id === 'monitor' && <Activity size={14} />}
          {window.title}
        </div>
        <div className="flex items-center gap-2">
          <button className="p-1 hover:bg-white/10 rounded"><Minus size={14} className="text-gray-400" /></button>
          <button className="p-1 hover:bg-white/10 rounded"><Square size={14} className="text-gray-400" /></button>
          <button onClick={() => onClose(window.id)} className="p-1 hover:bg-red-500/80 rounded group">
            <X size={14} className="text-gray-400 group-hover:text-white" />
          </button>
        </div>
      </div>
      <div className="flex-1 overflow-auto bg-[#0d0d0d] p-0 relative">
        {children}
      </div>
    </div>
  );
};

// --- Applications ---

// API Helper
const API_URL = "";

const TerminalApp = () => {
  const [lines, setLines] = useState<string[]>([
    "Booting Kolibri/Ubuntu Interface...",
    "Connecting to local kernel bridge (localhost:3000)...",
    "-----------------------------------------------------"
  ]);

  // Initial connection check
  useEffect(() => {
    fetch(`${API_URL}/api/system/stats`)
        .then(() => setLines(prev => [...prev, "Connection ESTABLISHED.", "Welcome to Ubuntu 24.04 LTS (Container Environment)", "Root access: GRANTED", "", "Type 'ls -la', 'uname -a', or 'cat /etc/os-release' to verify.", ""]))
        .catch(() => setLines(prev => [...prev, "Connection FAILED. Is the python bridge running?", ""]));
  }, []);

  const [input, setInput] = useState("");
  const endRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [lines]);

  const handleCommand = async (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      const cmd = input.trim();
      const newLines = [...lines, `root@kolibri:~# ${cmd}`];
      
      setLines(newLines);
      setInput("");

      if (cmd === 'help') {
        setLines(prev => [...prev, "Available commands:", "  kolibri_gen <seed>   - Run C Generator", "  status               - Check Bridge Status", "  clear                - Clear screen"]);
        return;
      }
      
      if (cmd === 'clear') {
        setLines([]);
        return;
      }

      // Send to Real Backend
      try {
          const res = await fetch(`${API_URL}/api/terminal/exec`, {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ cmd })
          });
          const data = await res.json();
          setLines(prev => [...prev, data.output]);
      } catch (err) {
          setLines(prev => [...prev, "Error connecting to backend bridge."]);
      }
    }
  };

  return (
    <div className="p-4 font-mono text-sm text-green-400 h-full overflow-y-auto" onClick={() => document.getElementById('term-input')?.focus()}>
      {lines.map((line, i) => (
        <div key={i} className="whitespace-pre-wrap mb-1">{line}</div>
      ))}
      <div className="flex items-center gap-2">
        <span className="text-blue-400">root@kolibri:~#</span>
        <input 
          id="term-input"
          type="text" 
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleCommand}
          className="bg-transparent border-none outline-none text-green-400 flex-1 font-mono"
          autoFocus
        />
      </div>
      <div ref={endRef} />
    </div>
  );
};

const GenomeApp = () => {
    const [content, setContent] = useState("Loading real genome...");

    useEffect(() => {
        fetch(`${API_URL}/api/fs/genome`)
            .then(res => res.json())
            .then(data => setContent(data.content))
            .catch(() => setContent("Failed to load genome file."));
    }, []);

    return (
        <div className="font-mono text-xs text-gray-300 p-2 h-full flex flex-col">
            <div className="border-b border-gray-700 pb-2 mb-2 flex gap-4 text-gray-500">
                <span>File: ./kolibri.genome</span>
                <span>Mode: READ-ONLY</span>
            </div>
            <pre className="flex-1 overflow-auto text-orange-300 whitespace-pre-wrap">
                {content}
            </pre>
        </div>
    );
};

const MonitorApp = () => {
    const [stats, setStats] = useState({ cpu: 0, memory: 0, memory_used_gb: 0, uptime: 0, processes: 0 });

    useEffect(() => {
        const interval = setInterval(() => {
            fetch(`${API_URL}/api/system/stats`)
                .then(res => res.json())
                .then(data => setStats(data))
                .catch(err => console.error(err));
        }, 1000);
        return () => clearInterval(interval);
    }, []);

    return (
        <div className="p-6 text-white h-full flex flex-col gap-6">
            <div className="grid grid-cols-2 gap-4">
                <div className="bg-white/5 p-4 rounded-lg border border-white/10">
                    <h3 className="text-gray-400 text-sm mb-2 flex items-center gap-2"><Cpu size={16}/> CPU Usage (Real)</h3>
                    <div className="text-3xl font-bold font-mono text-green-400">{stats.cpu}%</div>
                    <div className="h-1 bg-gray-700 mt-2 rounded-full overflow-hidden">
                        <div className="h-full bg-green-500 transition-all duration-500" style={{width: `${stats.cpu}%`}}></div>
                    </div>
                </div>
                <div className="bg-white/5 p-4 rounded-lg border border-white/10">
                    <h3 className="text-gray-400 text-sm mb-2 flex items-center gap-2"><Database size={16}/> RAM Usage (Real)</h3>
                    <div className="text-3xl font-bold font-mono text-blue-400">{stats.memory_used_gb} GB</div>
                    <div className="h-1 bg-gray-700 mt-2 rounded-full overflow-hidden">
                        <div className="h-full bg-blue-500 transition-all duration-500" style={{width: `${stats.memory}%`}}></div>
                    </div>
                </div>
            </div>
            
            <div className="bg-white/5 p-4 rounded-lg border border-white/10 flex-1">
                 <h3 className="text-gray-400 text-sm mb-4">System Details</h3>
                 <div className="font-mono text-sm space-y-2 text-gray-300">
                     <div className="flex justify-between"><span>OS:</span> <span>Ubuntu 24.04 LTS</span></div>
                     <div className="flex justify-between"><span>Processes:</span> <span>{stats.processes}</span></div>
                     <div className="flex justify-between"><span>Uptime:</span> <span>{stats.uptime} sec</span></div>
                 </div>
            </div>
        </div>
    );
};

// --- Main Desktop ---

export default function DesktopOS() {
  const [windows, setWindows] = useState<WindowState[]>([
    { id: 'terminal', title: 'Terminal', isOpen: true, isMinimized: false, zIndex: 1, content: <TerminalApp /> }
  ]);

  const bringToFront = (id: WindowId) => {
    setWindows(prev => prev.map(w => ({
      ...w,
      zIndex: w.id === id ? 10 : 1
    })));
  };

  const openApp = (id: WindowId) => {
    setWindows(prev => {
        const exists = prev.find(w => w.id === id);
        if (exists) {
            return prev.map(w => w.id === id ? { ...w, isOpen: true, isMinimized: false, zIndex: 10 } : { ...w, zIndex: 1 });
        }
        
        // Add new window
        let content = null;
        let title = "";
        
        switch(id) {
            case 'terminal': content = <TerminalApp />; title = "Terminal"; break;
            case 'genome': content = <GenomeApp />; title = "Genome Hex Viewer"; break;
            case 'monitor': content = <MonitorApp />; title = "System Resources"; break;
            default: content = <div className="p-4 text-white">Not implemented</div>; title = "App";
        }
        
        return [...prev.map(w => ({...w, zIndex: 1})), { id, title, isOpen: true, isMinimized: false, zIndex: 10, content }];
    });
  };

  const closeApp = (id: WindowId) => {
      setWindows(prev => prev.map(w => w.id === id ? { ...w, isOpen: false } : w));
  };

  return (
    <div className="h-screen w-screen bg-cover bg-center overflow-hidden font-sans relative text-gray-100"
        style={{ backgroundColor: '#0f172a', backgroundImage: 'radial-gradient(circle at center, #1e293b 0%, #0f172a 100%)' }}
    >
      {/* Background Pattern */}
      <div className="absolute inset-0 opacity-20 pointer-events-none" 
           style={{ backgroundImage: 'url("data:image/svg+xml,%3Csvg width=\'60\' height=\'60\' viewBox=\'0 0 60 60\' xmlns=\'http://www.w3.org/2000/svg\'%3E%3Cg fill=\'none\' fill-rule=\'evenodd\'%3E%3Cg fill=\'%239C92AC\' fill-opacity=\'0.2\'%3E%3Cpath d=\'M36 34v-4h-2v4h-4v2h4v4h2v-4h4v-2h-4zm0-30V0h-2v4h-4v2h4v4h2V6h4V4h-4zM6 34v-4H4v4H0v2h4v4h2v-4h4v-2H6zM6 4V0H4v4H0v2h4v4h2V6h4V4H6z\'/%3E%3C/g%3E%3C/g%3E%3C/svg%3E")' }} />

      <TopBar />
      <Dock onOpen={openApp} activeWindows={windows.filter(w => w.isOpen).map(w => w.id)} />
      
      {windows.map(w => (
          <WindowFrame 
            key={w.id} 
            window={w} 
            onClose={closeApp} 
            onFocus={bringToFront}
          >
             {w.content}
          </WindowFrame>
      ))}

      {/* Desktop Icons */}
      <div className="absolute bottom-10 right-10 flex flex-col items-end gap-6 p-4">
         <div className="flex flex-col items-center gap-1 group cursor-pointer w-20">
             <div className="w-16 h-16 bg-white/10 group-hover:bg-white/20 rounded-lg flex items-center justify-center backdrop-blur-sm border border-white/5 transition-all">
                <Database size={32} className="text-orange-400 drop-shadow-lg" />
             </div>
             <span className="text-white text-xs font-medium drop-shadow-md bg-black/50 px-2 rounded">Genome.dat</span>
         </div>
          <div className="flex flex-col items-center gap-1 group cursor-pointer w-20">
             <div className="w-16 h-16 bg-white/10 group-hover:bg-white/20 rounded-lg flex items-center justify-center backdrop-blur-sm border border-white/5 transition-all">
                <Wifi size={32} className="text-blue-400 drop-shadow-lg" />
             </div>
             <span className="text-white text-xs font-medium drop-shadow-md bg-black/50 px-2 rounded">Network</span>
         </div>
      </div>
    </div>
  );
}
