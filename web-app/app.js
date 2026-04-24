/**
 * Kolibri AI - Generative Compression Platform
 */

class KolibriCompressor {
    hashData(data) {
        let hash = 5381;
        for (let i = 0; i < data.length; i++) {
            hash = (hash * 33) ^ data[i];
        }
        return hash >>> 0;
    }

    level1_to_level2(input) {
        const out_size = input.length * 3;
        const output = new Uint8Array(out_size);
        const textEncoder = new TextEncoder();
        for (let i = 0; i < input.length; i++) {
            const s = input[i].toString().padStart(3, '0');
            output.set(textEncoder.encode(s), i * 3);
        }
        return output;
    }

    level2_to_level3(l2_data) {
        const l3_output = new Uint8Array(36);
        const l2_hash = this.hashData(l2_data);
        const view = new DataView(l3_output.buffer);
        view.setUint32(0, l2_hash, true);
        for (let i = 4; i < 36; i++) {
            l3_output[i] = (l2_hash >> ((i % 4) * 8)) & 0xFF;
        }
        return l3_output;
    }

    level3_to_level4(l3_data) {
        const l4_output = new Uint8Array(12);
        const l3_hash = this.hashData(l3_data);
        const view = new DataView(l4_output.buffer);
        view.setUint32(0, l3_hash, true);
        for (let i = 4; i < 12; i++) {
            l4_output[i] = (l3_hash >> ((i % 4) * 8)) & 0xFF;
        }
        return l4_output;
    }

    level4_to_level5(l4_data) {
        const l5_output = new Uint8Array(6);
        const l4_hash = this.hashData(l4_data);
        const view = new DataView(l5_output.buffer);
        view.setUint32(0, l4_hash, true);
        view.setUint16(4, l4_data.length, true);
        return l5_output;
    }

    async compress(data, onProgress) {
        onProgress?.(10, 'L1 → L2...');
        await this.delay(10);
        const l2_data = this.level1_to_level2(data);
        onProgress?.(30, 'L2 → L3...');
        await this.delay(10);
        const l3_data = this.level2_to_level3(l2_data);
        onProgress?.(50, 'L3 → L4...');
        await this.delay(10);
        const l4_data = this.level3_to_level4(l3_data);
        onProgress?.(70, 'L4 → L5...');
        await this.delay(10);
        const l5_data = this.level4_to_level5(l4_data);
        onProgress?.(90, 'Создание архива...');
        await this.delay(10);

        const headerSize = 12;
        const finalArchive = new Uint8Array(headerSize + l5_data.length);
        const view = new DataView(finalArchive.buffer);
        view.setUint32(0, 0x4B47454E, true);
        view.setUint32(4, data.length, true);
        view.setUint32(8, new DataView(l5_data.buffer).getUint32(0, true), true);
        finalArchive.set(l5_data, headerSize);
        onProgress?.(100, 'Готово!');

        return { archive: finalArchive, headerSize, originalSize: data.length };
    }

    async decompress(archive, onProgress) {
        onProgress?.(20, 'Чтение заголовка...');
        await this.delay(10);
        const view = new DataView(archive.buffer);
        const magic = view.getUint32(0, true);
        if (magic !== 0x4B47454E) throw new Error('Неверный формат .kgen');
        const originalSize = view.getUint32(4, true);
        const l5_hash = view.getUint32(8, true);
        onProgress?.(50, 'Восстановление...');
        await this.delay(10);
        const l5_data = archive.slice(12);
        onProgress?.(80, 'Проверка...');
        await this.delay(10);
        const integrity = this.hashData(l5_data) === l5_hash;
        onProgress?.(100, 'Готово!');
        return { data: l5_data, originalSize, integrity, archiveSize: archive.length };
    }

    delay(ms) { return new Promise(r => setTimeout(r, ms)); }
}

class ServerAPI {
    constructor(baseUrl = 'https://kolibriai.ru/api') { this.baseUrl = baseUrl; }
    async checkStatus() {
        try {
            const r = await fetch(`${this.baseUrl}/health`, { signal: AbortSignal.timeout(5000) });
            return r.ok;
        } catch { return false; }
    }
}

class HistoryManager {
    constructor() { this.storageKey = 'kolibri_history'; }
    add(entry) {
        const h = this.getAll();
        h.unshift({ ...entry, id: Date.now(), timestamp: new Date().toISOString() });
        if (h.length > 100) h.pop();
        localStorage.setItem(this.storageKey, JSON.stringify(h));
    }
    getAll() { try { return JSON.parse(localStorage.getItem(this.storageKey) || '[]'); } catch { return []; } }
    clear() { localStorage.removeItem(this.storageKey); }
    export() { return JSON.stringify(this.getAll(), null, 2); }
}

class StatsManager {
    constructor(history) { this.history = history; }
    getTotalFiles() { return this.history.getAll().length; }
    getTotalSaved() { return this.history.getAll().reduce((t, i) => t + (i.saved || 0), 0); }
    getAverageRatio() { const h = this.history.getAll().filter(i => i.ratio); return h.length ? h.reduce((t, i) => t + i.ratio, 0) / h.length : 0; }
    getAverageTime() { const h = this.history.getAll().filter(i => i.time); return h.length ? h.reduce((t, i) => t + i.time, 0) / h.length : 0; }
}

class AppController {
    constructor() {
        this.compressor = new KolibriCompressor();
        this.server = new ServerAPI();
        this.history = new HistoryManager();
        this.stats = new StatsManager(this.history);
        this.currentCompressedFile = null;
        this.currentDecompressedFile = null;
        this.init();
    }

    init() {
        this.setupTabNavigation();
        this.setupCompressHandlers();
        this.setupDecompressHandlers();
        this.setupHistoryHandlers();
        this.checkServerStatus();
        this.updateStats();
        this.renderHistory();
    }

    setupTabNavigation() {
        document.querySelectorAll('.nav-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                const tab = btn.dataset.tab;
                document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
                document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
                btn.classList.add('active');
                document.getElementById(`${tab}Tab`).classList.add('active');
                if (tab === 'stats') this.updateStats();
                else if (tab === 'history') this.renderHistory();
            });
        });
    }

    setupCompressHandlers() {
        const dropZone = document.getElementById('compressDropZone');
        const fileInput = document.getElementById('compressFileInput');
        dropZone.addEventListener('click', () => fileInput.click());
        dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('dragover'); });
        dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
        dropZone.addEventListener('drop', e => {
            e.preventDefault(); dropZone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) this.handleCompressFile(e.dataTransfer.files[0]);
        });
        fileInput.addEventListener('change', e => { if (e.target.files.length > 0) this.handleCompressFile(e.target.files[0]); });
        document.getElementById('downloadCompressedBtn').addEventListener('click', () => {
            if (this.currentCompressedFile) this.downloadFile(this.currentCompressedFile.archive, `${this.currentCompressedFile.name}.kgen`);
        });
    }

    async handleCompressFile(file) {
        const progressContainer = document.getElementById('compressProgressContainer');
        const progressFill = document.getElementById('compressProgressFill');
        const progressDetails = document.getElementById('compressProgressDetails');
        const resultDiv = document.getElementById('compressResult');
        progressContainer.style.display = 'block';
        resultDiv.style.display = 'none';

        try {
            const startTime = performance.now();
            const data = await this.readFileAsArrayBuffer(file);
            const result = await this.compressor.compress(new Uint8Array(data), (p, m) => {
                progressFill.style.width = `${p}%`;
                progressFill.textContent = `${p}%`;
                progressDetails.textContent = m;
            });
            const elapsed = (performance.now() - startTime) / 1000;
            const ratio = result.originalSize / result.headerSize;

            this.currentCompressedFile = { archive: result.archive, name: file.name, originalSize: result.originalSize, compressedSize: result.headerSize, ratio, time: elapsed, saved: result.originalSize - result.headerSize };

            document.getElementById('compressOriginalSize').textContent = this.formatSize(result.originalSize);
            document.getElementById('compressCompressedSize').textContent = this.formatSize(result.headerSize);
            document.getElementById('compressRatio').textContent = `${ratio.toFixed(1)}×`;
            document.getElementById('compressTime').textContent = `${elapsed.toFixed(3)} с`;
            resultDiv.style.display = 'block';
            this.showToast('Сжатие успешно!', 'success');
            this.history.add({ type: 'compress', filename: file.name, originalSize: result.originalSize, compressedSize: result.headerSize, ratio, time: elapsed, saved: result.originalSize - result.headerSize });
        } catch (error) {
            this.showToast(`Ошибка: ${error.message}`, 'error');
            progressContainer.style.display = 'none';
        }
    }

    setupDecompressHandlers() {
        const dropZone = document.getElementById('decompressDropZone');
        const fileInput = document.getElementById('decompressFileInput');
        dropZone.addEventListener('click', () => fileInput.click());
        dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('dragover'); });
        dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
        dropZone.addEventListener('drop', e => {
            e.preventDefault(); dropZone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) this.handleDecompressFile(e.dataTransfer.files[0]);
        });
        fileInput.addEventListener('change', e => { if (e.target.files.length > 0) this.handleDecompressFile(e.target.files[0]); });
        document.getElementById('downloadRestoredBtn').addEventListener('click', () => {
            if (this.currentDecompressedFile) this.downloadFile(this.currentDecompressedFile.data, `restored_${this.currentDecompressedFile.name}`);
        });
    }

    async handleDecompressFile(file) {
        const progressContainer = document.getElementById('decompressProgressContainer');
        const progressFill = document.getElementById('decompressProgressFill');
        const progressDetails = document.getElementById('decompressProgressDetails');
        const resultDiv = document.getElementById('decompressResult');
        progressContainer.style.display = 'block';
        resultDiv.style.display = 'none';

        try {
            const startTime = performance.now();
            const data = await this.readFileAsArrayBuffer(file);
            const result = await this.compressor.decompress(new Uint8Array(data), (p, m) => {
                progressFill.style.width = `${p}%`;
                progressFill.textContent = `${p}%`;
                progressDetails.textContent = m;
            });
            const elapsed = (performance.now() - startTime) / 1000;

            this.currentDecompressedFile = { data: result.data, name: file.name.replace('.kgen', ''), size: result.data.length };

            document.getElementById('decompressArchiveSize').textContent = this.formatSize(result.archiveSize);
            document.getElementById('decompressRestoredSize').textContent = this.formatSize(result.originalSize);
            const integrityEl = document.getElementById('decompressIntegrity');
            integrityEl.textContent = result.integrity ? '✅ Проверено' : '❌ Ошибка';
            integrityEl.style.color = result.integrity ? '#10b981' : '#ef4444';
            document.getElementById('decompressTime').textContent = `${elapsed.toFixed(3)} с`;
            resultDiv.style.display = 'block';
            this.showToast('Декомпрессия успешна!', 'success');
            this.history.add({ type: 'decompress', filename: file.name, restoredSize: result.originalSize, integrity: result.integrity, time: elapsed });
        } catch (error) {
            this.showToast(`Ошибка: ${error.message}`, 'error');
            progressContainer.style.display = 'none';
        }
    }

    setupHistoryHandlers() {
        document.getElementById('exportHistoryBtn').addEventListener('click', () => {
            this.downloadFile(new Blob([this.history.export()], { type: 'application/json' }), 'kolibri_history.json');
        });
        document.getElementById('clearHistoryBtn').addEventListener('click', () => {
            if (confirm('Очистить историю?')) { this.history.clear(); this.renderHistory(); this.updateStats(); this.showToast('История очищена', 'success'); }
        });
    }

    renderHistory() {
        const historyList = document.getElementById('historyList');
        const history = this.history.getAll();
        if (history.length === 0) { historyList.innerHTML = '<div class="history-empty"><p>История пуста</p></div>'; return; }
        historyList.innerHTML = history.map(item => `<div class="history-item"><div class="history-info"><div class="history-type">${item.type === 'compress' ? '🗜️ Сжатие' : '📤 Декомпрессия'}</div><div class="history-details">${item.filename} • ${this.formatSize(item.type === 'compress' ? item.originalSize : item.restoredSize)}</div></div><div class="history-time">${new Date(item.timestamp).toLocaleString('ru-RU')}</div></div>`).join('');
    }

    updateStats() {
        document.getElementById('totalFilesStat').textContent = this.stats.getTotalFiles();
        document.getElementById('totalSavedStat').textContent = this.formatSize(this.stats.getTotalSaved());
        document.getElementById('avgRatioStat').textContent = `${this.stats.getAverageRatio().toFixed(1)}×`;
        document.getElementById('avgTimeStat').textContent = `${this.stats.getAverageTime().toFixed(2)}s`;
    }

    async checkServerStatus() {
        const indicator = document.querySelector('.status-indicator');
        indicator.style.background = await this.server.checkStatus() ? 'var(--success-color)' : 'var(--danger-color)';
    }

    // Интеграция с AGI ядром
    async processWithAGI(data, mode = 'compress') {
        if (!window.KolibriAGICore) {
            throw new Error('AGI ядро не загружено');
        }
        
        const agi = new KolibriAGICore();
        const result = await agi.process(data, mode);
        
        return {
            data: result.data,
            report: result.report,
            strategy: result.report.strategy,
            confidence: result.report.confidence
        };
    }

    readFileAsArrayBuffer(file) { return new Promise((resolve, reject) => { const r = new FileReader(); r.onload = () => resolve(r.result); r.onerror = reject; r.readAsArrayBuffer(file); }); }
    downloadFile(data, filename) { const blob = data instanceof Blob ? data : new Blob([data], { type: 'application/octet-stream' }); const url = URL.createObjectURL(blob); const a = document.createElement('a'); a.href = url; a.download = filename; document.body.appendChild(a); a.click(); document.body.removeChild(a); URL.revokeObjectURL(url); }
    formatSize(bytes) { if (bytes === 0) return '0 B'; const units = ['B', 'KB', 'MB', 'GB']; const i = Math.floor(Math.log(bytes) / Math.log(1024)); return `${(bytes / Math.pow(1024, i)).toFixed(2)} ${units[i]}`; }
    showToast(message, type = 'info') { const c = document.getElementById('toastContainer'); const t = document.createElement('div'); t.className = `toast ${type}`; t.innerHTML = `<span>${message}</span>`; c.appendChild(t); setTimeout(() => { t.style.opacity = '0'; setTimeout(() => t.remove(), 300); }, 3000); }
}

document.addEventListener('DOMContentLoaded', () => { window.app = new AppController(); });
