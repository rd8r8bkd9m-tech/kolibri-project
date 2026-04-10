export interface LiveQueueItem {
  id: number;
  title: string;
  content: string;
  source: string;
  created_at: string;
}

export interface LiveQueueStats {
  pending: number;
  approved: number;
  rejected: number;
}

export interface LiveQueueListResponse {
  pending: LiveQueueItem[];
  count: number;
}

export interface LiveQueueSearchResponse {
  results: LiveQueueItem[];
  count: number;
  total: number;
}

export interface LiveQueueAnalyticsResponse {
  overview: {
    pending: number;
    approved: number;
    rejected: number;
    approval_rate: number;
  };
  today: {
    pending: number;
    approved: number;
    rejected: number;
  };
}

export interface LiveQueueEditResponse {
  status: string;
  id: number;
}

export interface LiveQueueBulkActionResponse {
  status: string;
  approved?: number;
  rejected?: number;
  failed: number;
}

export interface LiveQueueExportResponse {
  status: string;
  exit_code: number;
}
