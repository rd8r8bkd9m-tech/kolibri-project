import React, { useState, useEffect } from 'react';
import { Download, Trash2, Calendar, HardDrive, Zap } from 'lucide-react';
import api from '../services/api';

interface FileItem {
  id: string;
  name: string;
  originalSize: number;
  compressedSize: number;
  type: string;
  uploadedAt: string;
  compressionRatio: number;
  kolibri: {
    algorithm: string;
    saved: number;
    savedPercent: string;
  };
}

interface FileListProps {
  refreshTrigger?: number;
  onFileDeleted?: () => void;
}

export const FileList: React.FC<FileListProps> = ({ refreshTrigger, onFileDeleted }) => {
  const [files, setFiles] = useState<FileItem[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState('');
  const [deleting, setDeleting] = useState<string | null>(null);

  const loadFiles = async () => {
    try {
      setIsLoading(true);
      const response = await api.listFiles();
      setFiles(response.data.files || []);
      setError('');
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : 'Ошибка загрузки файлов';
      setError(errorMessage);
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    loadFiles();
  }, [refreshTrigger]);

  const handleDownload = async (fileId: string, fileName: string) => {
    try {
      const response = await api.downloadFile(fileId);
      const blob = new Blob([response.data]);
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = fileName;
      document.body.appendChild(a);
      a.click();
      window.URL.revokeObjectURL(url);
      document.body.removeChild(a);
    } catch (err) {
      console.error('Download error:', err);
    }
  };

  const handleDelete = async (fileId: string) => {
    if (!window.confirm('Удалить файл? Это действие необратимо.')) return;

    setDeleting(fileId);
    try {
      await api.deleteFile(fileId);
      setFiles(files.filter(f => f.id !== fileId));
      onFileDeleted?.();
    } catch (err) {
      console.error('Delete error:', err);
    } finally {
      setDeleting(null);
    }
  };

  const formatSize = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatDate = (dateString: string): string => {
    return new Date(dateString).toLocaleString('ru-RU');
  };

  if (error) {
    return (
      <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-4">
        <p className="text-red-700 dark:text-red-400">❌ {error}</p>
      </div>
    );
  }

  if (isLoading) {
    return (
      <div className="flex items-center justify-center p-8">
        <div className="animate-spin">
          <Zap className="w-8 h-8 text-blue-500" />
        </div>
        <p className="ml-3 text-gray-600 dark:text-gray-400">Загрузка файлов...</p>
      </div>
    );
  }

  if (files.length === 0) {
    return (
      <div className="text-center py-12 bg-gray-50 dark:bg-gray-800/50 rounded-lg">
        <HardDrive className="w-12 h-12 text-gray-400 dark:text-gray-600 mx-auto mb-3" />
        <p className="text-gray-600 dark:text-gray-400">Нет загруженных файлов</p>
      </div>
    );
  }

  return (
    <div className="space-y-3">
      {files.map((file) => (
        <div
          key={file.id}
          className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-4 hover:shadow-md transition-shadow"
        >
          <div className="flex items-center justify-between">
            <div className="flex-1 min-w-0">
              <h3 className="font-semibold text-gray-900 dark:text-white truncate">
                {file.name}
              </h3>

              <div className="mt-2 flex flex-wrap gap-3 text-sm text-gray-600 dark:text-gray-400">
                <div className="flex items-center gap-1">
                  <HardDrive className="w-4 h-4" />
                  <span>{formatSize(file.originalSize)} → {formatSize(file.compressedSize)}</span>
                </div>

                <div className="flex items-center gap-1 px-2 py-1 bg-blue-50 dark:bg-blue-900/30 rounded">
                  <Zap className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                  <span className="text-blue-700 dark:text-blue-400 font-semibold">
                    {file.kolibri.savedPercent}% сжато
                  </span>
                </div>

                <div className="flex items-center gap-1">
                  <Calendar className="w-4 h-4" />
                  <span>{formatDate(file.uploadedAt)}</span>
                </div>

                <div className="flex items-center gap-1 text-xs bg-gray-100 dark:bg-gray-700 px-2 py-1 rounded">
                  <span className="font-mono">{file.kolibri.algorithm}</span>
                </div>
              </div>

              <div className="mt-2 text-xs text-gray-500 dark:text-gray-500">
                💾 Сохранено: {formatSize(file.kolibri.saved)}
              </div>
            </div>

            <div className="flex gap-2 ml-4">
              <button
                onClick={() => handleDownload(file.id, file.name)}
                className="p-2 text-gray-600 dark:text-gray-400 hover:text-blue-600 dark:hover:text-blue-400 rounded hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                title="Скачать файл"
              >
                <Download className="w-5 h-5" />
              </button>

              <button
                onClick={() => handleDelete(file.id)}
                disabled={deleting === file.id}
                className="p-2 text-gray-600 dark:text-gray-400 hover:text-red-600 dark:hover:text-red-400 rounded hover:bg-red-50 dark:hover:bg-red-900/20 transition-colors disabled:opacity-50"
                title="Удалить файл"
              >
                <Trash2 className="w-5 h-5" />
              </button>
            </div>
          </div>
        </div>
      ))}
    </div>
  );
};
