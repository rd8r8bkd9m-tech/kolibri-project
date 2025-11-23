import axios from 'axios'

const API_URL = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:3000'

export const apiClient = axios.create({
  baseURL: API_URL,
  headers: {
    'Content-Type': 'application/json',
  },
})

// Request interceptor
apiClient.interceptors.request.use(
  (config) => {
    // Add auth token if exists
    const token = localStorage.getItem('auth_token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => Promise.reject(error)
)

// Response interceptor
apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      // Handle unauthorized
      localStorage.removeItem('auth_token')
      window.location.href = '/login'
    }
    return Promise.reject(error)
  }
)

// Smeta API
export const smetaApi = {
  getAll: () => apiClient.get('/api/smeta'),
  getOne: (id: string) => apiClient.get(`/api/smeta/${id}`),
  create: (data: any) => apiClient.post('/api/smeta', data),
  update: (id: string, data: any) => apiClient.patch(`/api/smeta/${id}`, data),
  delete: (id: string) => apiClient.delete(`/api/smeta/${id}`),
  addPosition: (id: string, position: any) => 
    apiClient.post(`/api/smeta/${id}/positions`, position),
  removePosition: (positionId: string) => 
    apiClient.delete(`/api/smeta/positions/${positionId}`),
  calculateVolumes: (description: string) =>
    apiClient.post('/api/smeta/calculate-volumes', { description }),
}

// Norms API
export const normsApi = {
  getAll: (filters?: any) => apiClient.get('/api/norms', { params: filters }),
  search: (query: string) => apiClient.get('/api/norms/search', { params: { q: query } }),
  getByCode: (code: string) => apiClient.get(`/api/norms/code/${code}`),
  create: (data: any) => apiClient.post('/api/norms', data),
  import: (csvData: string) => apiClient.post('/api/norms/import', { csvData }),
  aiMatch: (description: string) => 
    apiClient.post('/api/norms/ai-match', { description }),
  applyCoefficients: (id: string, coefficients: any) =>
    apiClient.post(`/api/norms/${id}/apply-coefficients`, coefficients),
}

// AI API
export const aiApi = {
  analyzeText: (description: string) => 
    apiClient.post('/api/ai/analyze-text', { description }),
  analyzeImage: (file: File) => {
    const formData = new FormData()
    formData.append('image', file)
    return apiClient.post('/api/ai/analyze-image', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    })
  },
  generateSmeta: (prompt: string) => 
    apiClient.post('/api/ai/generate-smeta', { prompt }),
  parseBim: (data: any) => apiClient.post('/api/ai/parse-bim', data),
}

// Export API
export const exportApi = {
  toPdf: (id: string) => apiClient.get(`/api/export/${id}/pdf`, { responseType: 'blob' }),
  toExcel: (id: string) => apiClient.get(`/api/export/${id}/excel`, { responseType: 'blob' }),
  toWord: (id: string) => apiClient.get(`/api/export/${id}/word`, { responseType: 'blob' }),
  toJson: (id: string) => apiClient.get(`/api/export/${id}/json`),
}

// Sync API
export const syncApi = {
  fromDevice: (data: any) => apiClient.post('/api/sync/from-device', data),
  toDevice: (userId: string, lastSync?: string) =>
    apiClient.get('/api/sync/to-device', { params: { userId, lastSync } }),
  getOfflinePackage: () => apiClient.get('/api/sync/offline-package'),
  resolveConflicts: (conflicts: any[]) =>
    apiClient.post('/api/sync/resolve-conflicts', { conflicts }),
}
