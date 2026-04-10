import React, { useState, useEffect } from 'react';
import { fetchLiveQueuePending, approveQuestion, rejectQuestion, fetchLiveQueueStats } from '../api/liveQueue';
import { LiveQueueItem, LiveQueueStats } from '../types/liveQueue';

const LiveQueueDrawer: React.FC = () => {
  const [pending, setPending] = useState<LiveQueueItem[]>([]);
  const [stats, setStats] = useState<LiveQueueStats>({ pending: 0, approved: 0, rejected: 0 });
  const [loading, setLoading] = useState(true);
  const [selectedItem, setSelectedItem] = useState<LiveQueueItem | null>(null);
  const [editMode, setEditMode] = useState(false);
  const [editedAnswer, setEditedAnswer] = useState('');

  const loadData = async () => {
    try {
      setLoading(true);
      const [queueData, statsData] = await Promise.all([
        fetchLiveQueuePending(50),
        fetchLiveQueueStats(),
      ]);
      setPending(queueData.pending);
      setStats(statsData);
    } catch (error) {
      console.error('Failed to load live queue:', error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
    const interval = setInterval(loadData, 30000); // Refresh every 30 seconds
    return () => clearInterval(interval);
  }, []);

  const handleApprove = async (id: number) => {
    try {
      await approveQuestion(id);
      await loadData();
    } catch (error) {
      console.error('Failed to approve:', error);
    }
  };

  const handleReject = async (id: number) => {
    try {
      await rejectQuestion(id);
      await loadData();
    } catch (error) {
      console.error('Failed to reject:', error);
    }
  };

  const handleEdit = async (id: number, answer: string) => {
    try {
      const response = await fetch('/api/v1/live-queue/edit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id, answer }),
      });

      if (!response.ok) {
        throw new Error(`Failed to edit question: ${response.statusText}`);
      }

      await loadData();
      setEditMode(false);
      setSelectedItem(null);
    } catch (error) {
      console.error('Failed to edit:', error);
    }
  };

  if (loading && pending.length === 0) {
    return (
      <div className="p-4">
        <div className="text-center text-gray-500">Загрузка очереди...</div>
      </div>
    );
  }

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="p-4 border-b border-gray-200">
        <h2 className="text-lg font-semibold text-gray-900">Live Queue</h2>
        <p className="text-sm text-gray-600 mt-1">
          Неподтверждённые вопросы для модерации
        </p>
        <div className="flex gap-4 mt-3 text-xs">
          <span className="px-2 py-1 bg-yellow-100 text-yellow-800 rounded">
            Ожидает: {stats.pending}
          </span>
          <span className="px-2 py-1 bg-green-100 text-green-800 rounded">
            Одобрено: {stats.approved}
          </span>
          <span className="px-2 py-1 bg-red-100 text-red-800 rounded">
            Отклонено: {stats.rejected}
          </span>
        </div>
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto">
        {pending.length === 0 ? (
          <div className="p-8 text-center text-gray-500">
            <div className="text-4xl mb-2">✓</div>
            <div>Все вопросы обработаны</div>
          </div>
        ) : (
          <div className="divide-y divide-gray-200">
            {pending.map((item) => (
              <div
                key={item.id}
                className={`p-4 hover:bg-gray-50 cursor-pointer transition-colors ${
                  selectedItem?.id === item.id ? 'bg-blue-50' : ''
                }`}
                onClick={() => setSelectedItem(item)}
              >
                <div className="flex items-start justify-between gap-2">
                  <div className="flex-1 min-w-0">
                    <div className="text-sm font-medium text-gray-900 truncate">
                      {item.title}
                    </div>
                    <div className="text-xs text-gray-500 mt-1">
                      {new Date(item.created_at).toLocaleString('ru-RU')}
                    </div>
                  </div>
                  <div className="flex gap-2">
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        handleApprove(item.id);
                      }}
                      className="px-3 py-1 text-xs font-medium bg-green-600 text-white rounded hover:bg-green-700 transition-colors"
                    >
                      ✓
                    </button>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        handleReject(item.id);
                      }}
                      className="px-3 py-1 text-xs font-medium bg-red-600 text-white rounded hover:bg-red-700 transition-colors"
                    >
                      ✕
                    </button>
                  </div>
                </div>
                <div className="text-sm text-gray-600 mt-2 line-clamp-2">
                  {item.content}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Detail Panel */}
      {selectedItem && (
        <div className="border-t border-gray-200 p-4 bg-gray-50">
          <div className="flex items-start justify-between mb-3">
            <h3 className="text-sm font-semibold text-gray-900">
              {editMode ? 'Редактировать ответ' : 'Детали вопроса'}
            </h3>
            <button
              onClick={() => {
                setSelectedItem(null);
                setEditMode(false);
              }}
              className="text-gray-400 hover:text-gray-600"
            >
              ✕
            </button>
          </div>
          <div className="space-y-2">
            <div>
              <label className="text-xs text-gray-500">Вопрос:</label>
              <div className="text-sm text-gray-900 mt-1">{selectedItem.title}</div>
            </div>
            {editMode ? (
              <div>
                <label className="text-xs text-gray-500">Новый ответ:</label>
                <textarea
                  value={editedAnswer}
                  onChange={(e) => setEditedAnswer(e.target.value)}
                  className="w-full mt-1 p-2 text-sm border border-gray-300 rounded focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                  rows={6}
                  placeholder="Введите ответ..."
                />
              </div>
            ) : (
              <div>
                <label className="text-xs text-gray-500">Ответ:</label>
                <div className="text-sm text-gray-900 mt-1 whitespace-pre-wrap">
                  {selectedItem.content}
                </div>
              </div>
            )}
            <div>
              <label className="text-xs text-gray-500">Источник:</label>
              <div className="text-sm text-gray-700 mt-1">{selectedItem.source}</div>
            </div>
            <div className="flex gap-2 mt-4">
              {editMode ? (
                <>
                  <button
                    onClick={() => handleEdit(selectedItem.id, editedAnswer)}
                    disabled={!editedAnswer.trim()}
                    className="flex-1 px-4 py-2 text-sm font-medium bg-blue-600 text-white rounded hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                  >
                    Сохранить и одобрить
                  </button>
                  <button
                    onClick={() => {
                      setEditMode(false);
                      setEditedAnswer('');
                    }}
                    className="flex-1 px-4 py-2 text-sm font-medium text-gray-700 bg-white border border-gray-300 rounded hover:bg-gray-50 transition-colors"
                  >
                    Отмена
                  </button>
                </>
              ) : (
                <>
                  <button
                    onClick={() => {
                      setEditMode(true);
                      setEditedAnswer(selectedItem.content);
                    }}
                    className="flex-1 px-4 py-2 text-sm font-medium bg-blue-600 text-white rounded hover:bg-blue-700 transition-colors"
                  >
                    ✏️ Редактировать
                  </button>
                  <button
                    onClick={() => handleApprove(selectedItem.id)}
                    className="flex-1 px-4 py-2 text-sm font-medium bg-green-600 text-white rounded hover:bg-green-700 transition-colors"
                  >
                    Одобрить
                  </button>
                  <button
                    onClick={() => handleReject(selectedItem.id)}
                    className="flex-1 px-4 py-2 text-sm font-medium bg-red-600 text-white rounded hover:bg-red-700 transition-colors"
                  >
                    Отклонить
                  </button>
                </>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Refresh Button */}
      <div className="p-3 border-t border-gray-200">
        <button
          onClick={loadData}
          disabled={loading}
          className="w-full px-4 py-2 text-sm font-medium text-gray-700 bg-white border border-gray-300 rounded hover:bg-gray-50 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
        >
          {loading ? 'Обновление...' : 'Обновить'}
        </button>
      </div>
    </div>
  );
};

export default LiveQueueDrawer;
