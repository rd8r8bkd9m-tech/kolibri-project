import axios, { AxiosInstance, AxiosError } from 'axios';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000/api';
const STORAGE_API_URL = import.meta.env.VITE_STORAGE_URL || 'http://localhost:3001/api';

class ApiService {
  private client: AxiosInstance;
  private storageClient: AxiosInstance;

  constructor() {
    this.client = axios.create({
      baseURL: API_BASE_URL,
      timeout: 10000,
      headers: {
        'Content-Type': 'application/json',
      },
    });

    this.storageClient = axios.create({
      baseURL: STORAGE_API_URL,
      timeout: 30000,
    });

    // Add auth token to storage requests
    this.storageClient.interceptors.request.use(
      (config) => {
        const token = localStorage.getItem('storageToken');
        if (token) {
          config.headers.Authorization = `Bearer ${token}`;
        }
        return config;
      },
      (error) => Promise.reject(error)
    );

    this.storageClient.interceptors.response.use(
      (response) => response,
      (error: AxiosError) => {
        if (error.response?.status === 401) {
          localStorage.removeItem('storageToken');
        }
        return Promise.reject(error);
      }
    );

    // Add auth token to requests
    this.client.interceptors.request.use(
      (config) => {
        const token = localStorage.getItem('authToken');
        if (token) {
          config.headers.Authorization = `Bearer ${token}`;
        }
        return config;
      },
      (error) => Promise.reject(error)
    );

    // Handle errors
    this.client.interceptors.response.use(
      (response) => response,
      (error: AxiosError) => {
        if (error.response?.status === 401) {
          localStorage.removeItem('authToken');
          window.location.href = '/login';
        }
        return Promise.reject(error);
      }
    );
  }

  // Auth endpoints
  async login(email: string, password: string) {
    return this.client.post('/auth/login', { email, password });
  }

  async register(email: string, password: string, name: string) {
    return this.client.post('/auth/register', { email, password, name });
  }

  async logout() {
    return this.client.post('/auth/logout');
  }

  // License endpoints
  async getLicenses() {
    return this.client.get('/licenses');
  }

  async getLicense(id: string) {
    return this.client.get(`/licenses/${id}`);
  }

  async createLicense(data: unknown) {
    return this.client.post('/licenses', data);
  }

  async updateLicense(id: string, data: unknown) {
    return this.client.patch(`/licenses/${id}`, data);
  }

  async deleteLicense(id: string) {
    return this.client.delete(`/licenses/${id}`);
  }

  // Payment endpoints
  async getPayments() {
    return this.client.get('/payments');
  }

  async createPayment(data: unknown) {
    return this.client.post('/payments', data);
  }

  async getPaymentMethods() {
    return this.client.get('/payments/methods');
  }

  // User endpoints
  async getProfile() {
    return this.client.get('/user/profile');
  }

  async updateProfile(data: unknown) {
    return this.client.patch('/user/profile', data);
  }

  async changePassword(oldPassword: string, newPassword: string) {
    return this.client.post('/user/password', { oldPassword, newPassword });
  }

  // Stats endpoints
  async getStats() {
    return this.client.get('/stats');
  }

  // Cloud Storage endpoints
  async storageRegister(username: string, password: string) {
    return this.storageClient.post('/auth/register', { username, password });
  }

  async storageLogin(username: string, password: string) {
    const response = await this.storageClient.post('/auth/login', { username, password });
    if (response.data.token) {
      localStorage.setItem('storageToken', response.data.token);
    }
    return response;
  }

  async uploadFile(file: File) {
    const formData = new FormData();
    formData.append('file', file);
    return this.storageClient.post('/storage/upload', formData, {
      headers: {
        'Content-Type': 'multipart/form-data',
      },
    });
  }

  async listFiles() {
    return this.storageClient.get('/storage/files');
  }

  async downloadFile(fileId: string) {
    return this.storageClient.get(`/storage/download/${fileId}`, {
      responseType: 'blob',
    });
  }

  async deleteFile(fileId: string) {
    return this.storageClient.delete(`/storage/files/${fileId}`);
  }

  async getStorageInfo() {
    return this.storageClient.get('/storage/info');
  }

  async getStorageHealth() {
    return this.storageClient.get('/health');
  }
}

export default new ApiService();
