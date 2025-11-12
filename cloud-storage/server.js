import express from 'express';
import multer from 'multer';
import cors from 'cors';
import fs from 'fs/promises';
import path from 'path';
import { fileURLToPath } from 'url';
import { v4 as uuidv4 } from 'uuid';
import jwt from 'jsonwebtoken';
import KolibriCompressor from './kompressor.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3001;
const JWT_SECRET = process.env.JWT_SECRET || 'test-secret-key-change-in-production';
const UPLOAD_DIR = path.join(__dirname, 'uploads');
const COMPRESSED_DIR = path.join(__dirname, 'compressed');
const DB_FILE = path.join(__dirname, 'data.json');

// Инициализация Колибри компрессора
const kompressor = new KolibriCompressor();
console.log(`🚀 Kolibri Kompressor инициализирован: ${kompressor.getStats().algorithm}`);

// Middleware
app.use(cors());
app.use(express.json());

// Storage configuration
const storage = multer.diskStorage({
  destination: async (req, file, cb) => {
    await fs.mkdir(UPLOAD_DIR, { recursive: true });
    cb(null, UPLOAD_DIR);
  },
  filename: (req, file, cb) => {
    const uniqueName = `${uuidv4()}-${Date.now()}${path.extname(file.originalname)}`;
    cb(null, uniqueName);
  }
});

const upload = multer({ 
  storage,
  limits: { fileSize: 100 * 1024 * 1024 } // 100MB
});

// Database functions
async function getDatabase() {
  try {
    const data = await fs.readFile(DB_FILE, 'utf-8');
    return JSON.parse(data);
  } catch {
    return {
      users: {},
      files: [],
      sessions: {}
    };
  }
}

async function saveDatabase(data) {
  await fs.writeFile(DB_FILE, JSON.stringify(data, null, 2));
}

// Auth middleware
function verifyToken(req, res, next) {
  const token = req.headers.authorization?.split(' ')[1];
  
  if (!token) {
    return res.status(401).json({ error: 'No token provided' });
  }
  
  try {
    const decoded = jwt.verify(token, JWT_SECRET);
    req.userId = decoded.userId;
    req.username = decoded.username;
    next();
  } catch {
    res.status(401).json({ error: 'Invalid token' });
  }
}

// Routes

// Auth: Register
app.post('/api/auth/register', async (req, res) => {
  const { username, password } = req.body;
  
  if (!username || !password) {
    return res.status(400).json({ error: 'Username and password required' });
  }
  
  const db = await getDatabase();
  
  if (db.users[username]) {
    return res.status(409).json({ error: 'User already exists' });
  }
  
  const userId = uuidv4();
  const hashedPassword = Buffer.from(password).toString('base64'); // Simple hash for demo
  
  db.users[username] = {
    id: userId,
    username,
    password: hashedPassword,
    createdAt: new Date().toISOString(),
    storage: {
      used: 0,
      limit: 10 * 1024 * 1024 * 1024 // 10GB
    }
  };
  
  await saveDatabase(db);
  
  const token = jwt.sign({ userId, username }, JWT_SECRET, { expiresIn: '7d' });
  
  res.status(201).json({
    user: { id: userId, username },
    token,
    storage: db.users[username].storage
  });
});

// Auth: Login
app.post('/api/auth/login', async (req, res) => {
  const { username, password } = req.body;
  
  if (!username || !password) {
    return res.status(400).json({ error: 'Username and password required' });
  }
  
  const db = await getDatabase();
  const user = db.users[username];
  
  if (!user || user.password !== Buffer.from(password).toString('base64')) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }
  
  const token = jwt.sign({ userId: user.id, username }, JWT_SECRET, { expiresIn: '7d' });
  
  res.json({
    user: { id: user.id, username },
    token,
    storage: user.storage
  });
});

// Storage: Upload file
app.post('/api/storage/upload', verifyToken, upload.single('file'), async (req, res) => {
  if (!req.file) {
    return res.status(400).json({ error: 'No file provided' });
  }
  
  try {
    const db = await getDatabase();
    const user = db.users[req.username];
    
    // Читаем исходный файл
    const originalData = await fs.readFile(req.file.path);
    const originalSize = originalData.length;
    
    // Применяем Колибри сжатие
    const compressionResult = await kompressor.compress(originalData);
    const compressedData = compressionResult.compressed;
    
    // Проверяем, есть ли место в хранилище
    const availableStorage = user.storage.limit - user.storage.used;
    if (compressedData.length > availableStorage) {
      await fs.unlink(req.file.path);
      return res.status(413).json({ 
        error: 'Storage limit exceeded',
        available: availableStorage,
        required: compressedData.length
      });
    }
    
    // Сохраняем сжатый файл
    await fs.mkdir(COMPRESSED_DIR, { recursive: true });
    const compressedFilename = `${uuidv4()}.kolibri`;
    const compressedPath = path.join(COMPRESSED_DIR, compressedFilename);
    await fs.writeFile(compressedPath, compressedData);
    
    // Вычисляем хеш для проверки целостности
    const hash = kompressor.calculateHash(originalData);
    
    const fileRecord = {
      id: uuidv4(),
      userId: req.userId,
      originalName: req.file.originalname,
      filename: req.file.filename,
      compressedFilename: compressedFilename,
      mimeType: req.file.mimetype,
      uploadedAt: new Date().toISOString(),
      originalSize: originalSize,
      compressedSize: compressedData.length,
      compressionRatio: compressionResult.ratio,
      hash: hash,
      kolibriMetadata: {
        algorithm: 'DECIMAL10X v1.0',
        blockSize: kompressor.blockSize,
        patterns: compressionResult.patterns || 0
      }
    };
    
    // Удаляем временный исходный файл
    await fs.unlink(req.file.path);
    
    db.files.push(fileRecord);
    user.storage.used += compressedData.length;
    await saveDatabase(db);
    
    res.status(201).json({
      file: fileRecord,
      storage: user.storage,
      compression: {
        ratio: compressionResult.ratio,
        saved: originalSize - compressedData.length,
        message: `📦 Файл сжат на ${compressionResult.ratio}% (Колибри DECIMAL10X)`
      }
    });
  } catch (error) {
    console.error('Upload error:', error);
    res.status(500).json({ 
      error: `Upload failed: ${error.message}` 
    });
  }
});

// Storage: List files
app.get('/api/storage/files', verifyToken, async (req, res) => {
  const db = await getDatabase();
  const userFiles = db.files.filter(f => f.userId === req.userId);
  const user = db.users[req.username];
  
  res.json({
    files: userFiles.map(f => ({
      id: f.id,
      name: f.originalName,
      originalSize: f.originalSize,
      compressedSize: f.compressedSize,
      type: f.mimeType,
      uploadedAt: f.uploadedAt,
      compressionRatio: f.compressionRatio,
      kolibri: {
        algorithm: f.kolibriMetadata.algorithm,
        saved: f.originalSize - f.compressedSize,
        savedPercent: ((1 - f.compressedSize / f.originalSize) * 100).toFixed(1)
      }
    })),
    storage: user.storage,
    total: userFiles.length,
    kolibriStats: {
      totalSaved: userFiles.reduce((sum, f) => sum + (f.originalSize - f.compressedSize), 0),
      avgRatio: userFiles.length > 0 
        ? (userFiles.reduce((sum, f) => sum + f.compressionRatio, 0) / userFiles.length).toFixed(2)
        : 0
    }
  });
});

// Storage: Download file
app.get('/api/storage/download/:fileId', verifyToken, async (req, res) => {
  try {
    const db = await getDatabase();
    const file = db.files.find(f => f.id === req.params.fileId && f.userId === req.userId);
    
    if (!file) {
      return res.status(404).json({ error: 'File not found' });
    }
    
    // Читаем сжатый файл
    const compressedPath = path.join(COMPRESSED_DIR, file.compressedFilename);
    const compressedData = await fs.readFile(compressedPath);
    
    // Распаковываем с помощью Колибри
    const originalData = await kompressor.decompress(compressedData);
    
    // Отправляем оригинальный файл
    res.setHeader('Content-Type', file.mimeType);
    res.setHeader('Content-Disposition', `attachment; filename="${file.originalName}"`);
    res.setHeader('X-Kolibri-Compressed', `true`);
    res.setHeader('X-Kolibri-Ratio', `${file.compressionRatio}%`);
    res.setHeader('X-Kolibri-Algorithm', file.kolibriMetadata.algorithm);
    
    res.send(originalData);
  } catch (error) {
    console.error('Download error:', error);
    res.status(500).json({ 
      error: `Download failed: ${error.message}` 
    });
  }
});

// Storage: Delete file
app.delete('/api/storage/files/:fileId', verifyToken, async (req, res) => {
  try {
    const db = await getDatabase();
    const fileIndex = db.files.findIndex(f => f.id === req.params.fileId && f.userId === req.userId);
    
    if (fileIndex === -1) {
      return res.status(404).json({ error: 'File not found' });
    }
    
    const file = db.files[fileIndex];
    const user = db.users[req.username];
    
    // Удаляем сжатый файл
    try {
      const compressedPath = path.join(COMPRESSED_DIR, file.compressedFilename);
      await fs.unlink(compressedPath);
    } catch (err) {
      console.error('Error deleting compressed file:', err);
    }
    
    user.storage.used -= file.compressedSize;
    db.files.splice(fileIndex, 1);
    await saveDatabase(db);
    
    res.json({
      message: 'File deleted',
      storage: user.storage,
      freed: `${(file.compressedSize / 1024).toFixed(2)} KB (от ${(file.originalSize / 1024).toFixed(2)} KB оригинала)`
    });
  } catch (error) {
    console.error('Delete error:', error);
    res.status(500).json({ 
      error: `Delete failed: ${error.message}` 
    });
  }
});

// Storage: Get storage info
app.get('/api/storage/info', verifyToken, async (req, res) => {
  const db = await getDatabase();
  const user = db.users[req.username];
  const userFiles = db.files.filter(f => f.userId === req.userId);
  
  // Статистика сжатия
  let totalOriginalSize = 0;
  let totalCompressedSize = 0;
  let totalSaved = 0;
  
  userFiles.forEach(file => {
    totalOriginalSize += file.originalSize || 0;
    totalCompressedSize += file.compressedSize || 0;
    totalSaved += (file.originalSize || 0) - (file.compressedSize || 0);
  });
  
  const avgRatio = userFiles.length > 0 
    ? (totalCompressedSize / totalOriginalSize * 100).toFixed(2)
    : 0;
  
  res.json({
    username: req.username,
    storage: user.storage,
    filesCount: userFiles.length,
    usagePercent: Math.round((user.storage.used / user.storage.limit) * 100),
    kolibri: {
      algorithm: 'DECIMAL10X v1.0',
      totalOriginalSize: totalOriginalSize,
      totalCompressedSize: totalCompressedSize,
      totalSaved: totalSaved,
      averageCompressionRatio: parseFloat(avgRatio),
      compressionMessage: `💾 Сохранено ${(totalSaved / 1024 / 1024).toFixed(2)} МБ с помощью Колибри!`
    }
  });
});

// Health check
app.get('/api/health', (req, res) => {
  res.json({ status: 'ok', service: 'kolibri-cloud-storage' });
});

// Error handling
app.use((err, req, res, next) => {
  console.error(err);
  res.status(500).json({ error: 'Internal server error' });
});

// Start server
app.listen(PORT, () => {
  console.log(`
╔════════════════════════════════════════╗
║   Kolibri Cloud Storage API            ║
║   Running on http://localhost:${PORT}      ║
╚════════════════════════════════════════╝

📚 API Endpoints:
  POST   /api/auth/register
  POST   /api/auth/login
  POST   /api/storage/upload
  GET    /api/storage/files
  GET    /api/storage/download/:fileId
  DELETE /api/storage/files/:fileId
  GET    /api/storage/info
  GET    /api/health

🧪 Test with:
  npm run test
  `);
});

export default app;
