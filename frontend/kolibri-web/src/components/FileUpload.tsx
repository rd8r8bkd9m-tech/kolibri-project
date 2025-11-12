import React, { useState } from 'react';
import { Upload, CheckCircle, AlertCircle } from 'lucide-react';
import api from '../services/api';

interface FileUploadProps {
  onSuccess?: () => void;
  onError?: (error: string) => void;
}

export const FileUpload: React.FC<FileUploadProps> = ({ onSuccess, onError }) => {
  const [isDragActive, setIsDragActive] = useState(false);
  const [isUploading, setIsUploading] = useState(false);
  const [uploadStatus, setUploadStatus] = useState<'idle' | 'uploading' | 'success' | 'error'>('idle');
  const [statusMessage, setStatusMessage] = useState('');
  const fileInputRef = React.useRef<HTMLInputElement>(null);

  const handleDrag = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    if (e.type === 'dragenter' || e.type === 'dragover') {
      setIsDragActive(true);
    } else if (e.type === 'dragleave') {
      setIsDragActive(false);
    }
  };

  const uploadFiles = async (files: FileList | null) => {
    if (!files || files.length === 0) return;

    setIsUploading(true);
    setUploadStatus('uploading');
    setStatusMessage('Загрузка файлов...');

    try {
      for (let i = 0; i < files.length; i++) {
        const file = files[i];
        const response = await api.uploadFile(file);
        
        if (response.status === 201) {
          setStatusMessage(`✓ Файл "${file.name}" успешно загружен`);
          setUploadStatus('success');
          setTimeout(() => {
            setUploadStatus('idle');
            setStatusMessage('');
            onSuccess?.();
          }, 2000);
        }
      }
    } catch (error: unknown) {
      const errorMessage = error instanceof Error ? error.message : 'Ошибка при загрузке файла';
      setStatusMessage(`✗ ${errorMessage}`);
      setUploadStatus('error');
      onError?.(errorMessage);
      setTimeout(() => {
        setUploadStatus('idle');
        setStatusMessage('');
      }, 3000);
    } finally {
      setIsUploading(false);
      setIsDragActive(false);
    }
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragActive(false);
    uploadFiles(e.dataTransfer.files);
  };

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    uploadFiles(e.target.files);
  };

  const handleClick = () => {
    fileInputRef.current?.click();
  };

  return (
    <div className="w-full">
      <div
        onDragEnter={handleDrag}
        onDragLeave={handleDrag}
        onDragOver={handleDrag}
        onDrop={handleDrop}
        onClick={handleClick}
        className={`relative border-2 border-dashed rounded-lg p-8 text-center cursor-pointer transition-all ${
          isDragActive
            ? 'border-blue-500 bg-blue-50 dark:bg-blue-900/20'
            : 'border-gray-300 dark:border-gray-600 hover:border-blue-400 dark:hover:border-blue-500'
        } ${isUploading ? 'opacity-75' : ''}`}
      >
        <input
          ref={fileInputRef}
          type="file"
          onChange={handleChange}
          multiple
          disabled={isUploading}
          className="hidden"
          accept="*/*"
        />

        <div className="flex flex-col items-center justify-center gap-3">
          {uploadStatus === 'success' ? (
            <>
              <CheckCircle className="w-12 h-12 text-green-500" />
              <p className="text-green-600 dark:text-green-400 font-semibold">{statusMessage}</p>
            </>
          ) : uploadStatus === 'error' ? (
            <>
              <AlertCircle className="w-12 h-12 text-red-500" />
              <p className="text-red-600 dark:text-red-400 font-semibold">{statusMessage}</p>
            </>
          ) : (
            <>
              <Upload className={`w-12 h-12 ${isUploading ? 'text-blue-500 animate-pulse' : 'text-gray-400 dark:text-gray-500'}`} />
              <div>
                <p className="text-base font-semibold text-gray-700 dark:text-gray-300">
                  {isUploading ? 'Загрузка...' : 'Перетащите файлы сюда или кликните'}
                </p>
                <p className="text-sm text-gray-500 dark:text-gray-400 mt-1">
                  Максимум 100 МБ на файл
                </p>
              </div>
            </>
          )}
        </div>
      </div>

      {statusMessage && (
        <div className={`mt-3 p-3 rounded-lg text-sm font-medium flex items-center gap-2 ${
          uploadStatus === 'success'
            ? 'bg-green-50 dark:bg-green-900/20 text-green-700 dark:text-green-400'
            : uploadStatus === 'error'
            ? 'bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-400'
            : 'bg-blue-50 dark:bg-blue-900/20 text-blue-700 dark:text-blue-400'
        }`}>
          {uploadStatus === 'success' && <CheckCircle className="w-4 h-4" />}
          {uploadStatus === 'error' && <AlertCircle className="w-4 h-4" />}
          {statusMessage}
        </div>
      )}
    </div>
  );
};
