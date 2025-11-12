import { create } from 'zustand';

interface License {
  id: string;
  name: string;
  tier: string;
  status: 'active' | 'expiring' | 'expired';
  users: number;
  maxUsers: number;
  storage: string;
  maxStorage: string;
  expiry: string;
  daysLeft: number;
  price: string;
}

interface LicenseState {
  licenses: License[];
  loading: boolean;
  error: string | null;
  setLicenses: (licenses: License[]) => void;
  addLicense: (license: License) => void;
  updateLicense: (id: string, license: Partial<License>) => void;
  removeLicense: (id: string) => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
}

export const useLicenseStore = create<LicenseState>((set) => ({
  licenses: [
    {
      id: '1',
      name: 'Professional Plus',
      tier: 'Professional',
      status: 'active',
      users: 45,
      maxUsers: 100,
      storage: '8.5 TB',
      maxStorage: '10 TB',
      expiry: '12.01.2025',
      daysLeft: 245,
      price: '$15,000/year',
    },
    {
      id: '2',
      name: 'Business Standard',
      tier: 'Business',
      status: 'active',
      users: 120,
      maxUsers: 200,
      storage: '18 TB',
      maxStorage: '20 TB',
      expiry: '15.03.2025',
      daysLeft: 280,
      price: '$25,000/year',
    },
  ],
  loading: false,
  error: null,

  setLicenses: (licenses) => set({ licenses }),

  addLicense: (license) =>
    set((state) => ({
      licenses: [...state.licenses, license],
    })),

  updateLicense: (id, updates) =>
    set((state) => ({
      licenses: state.licenses.map((license) =>
        license.id === id ? { ...license, ...updates } : license
      ),
    })),

  removeLicense: (id) =>
    set((state) => ({
      licenses: state.licenses.filter((license) => license.id !== id),
    })),

  setLoading: (loading) => set({ loading }),
  setError: (error) => set({ error }),
}));
