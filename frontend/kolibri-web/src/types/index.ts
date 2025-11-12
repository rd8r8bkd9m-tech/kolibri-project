// User Types
export interface User {
  id: string;
  name: string;
  email: string;
  company: string;
  avatar?: string;
  createdAt: string;
  updatedAt: string;
}

// License Types
export interface License {
  id: string;
  name: string;
  tier: 'community' | 'professional' | 'business' | 'enterprise';
  status: 'active' | 'expiring' | 'expired';
  users: number;
  maxUsers: number;
  storage: string;
  maxStorage: string;
  expiry: string;
  daysLeft: number;
  price: string;
  purchasedDate: string;
  features: string[];
}

// Payment Types
export interface Payment {
  id: string;
  licenseId: string;
  amount: number;
  currency: string;
  method: string;
  status: 'pending' | 'completed' | 'failed';
  date: string;
  description: string;
}

export interface PaymentMethod {
  id: string;
  name: string;
  type: 'card' | 'bank' | 'digital_wallet';
  icon: string;
  isDefault: boolean;
}

// Auth Types
export interface LoginRequest {
  email: string;
  password: string;
}

export interface LoginResponse {
  user: User;
  token: string;
}

export interface RegisterRequest {
  email: string;
  password: string;
  name: string;
  company: string;
}

// API Response Types
export interface ApiResponse<T> {
  success: boolean;
  data: T;
  message?: string;
  error?: string;
}

export interface PaginatedResponse<T> {
  data: T[];
  total: number;
  page: number;
  limit: number;
  totalPages: number;
}

// Stats Types
export interface Stats {
  totalLicenses: number;
  totalUsers: number;
  totalStorage: string;
  daysUntilExpiry: number;
  monthlyExpenses: number;
}

// Notification Types
export interface Notification {
  id: string;
  title: string;
  message: string;
  type: 'info' | 'warning' | 'error' | 'success';
  read: boolean;
  createdAt: string;
}
