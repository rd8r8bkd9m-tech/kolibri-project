export interface Norm {
  id: string
  code: string
  standard: 'ФЕР' | 'ГЭСН' | 'ТЕР'
  name: string
  description: string
  unit: string
  basePrice: number
  materials?: any
  labor?: any
  equipment?: any
  coefficients?: any
  category?: string
  subcategory?: string
  year: number
  region?: string
  isActive: boolean
  isCustom: boolean
  createdAt: string
  updatedAt: string
}

export interface SmetaPosition {
  id: string
  smetaId: string
  normId?: string
  norm?: Norm
  orderNumber: number
  workDescription: string
  unit: string
  quantity: number
  unitPrice: number
  totalPrice: number
  calculations?: any
  coefficients?: any
  notes?: string
}

export interface Smeta {
  id: string
  name: string
  description?: string
  objectAddress?: string
  clientName?: string
  region?: string
  totalCost: number
  metadata?: any
  positions: SmetaPosition[]
  status: 'draft' | 'approved' | 'archived'
  userId?: string
  isTemplate: boolean
  createdAt: string
  updatedAt: string
}

export interface VolumeCalculation {
  length: number
  width: number
  height: number
  floorArea: number
  wallArea: number
  volume: number
}

export interface AiAnalysis {
  description: string
  workTypes: string[]
  suggestedNorms: Norm[]
  volumes?: VolumeCalculation
  confidence: number
}

export interface Coefficients {
  regional?: number
  seasonal?: number
  complexity?: number
}

export interface OfflinePackage {
  version: string
  norms: Norm[]
  coefficients: {
    regional: Record<string, number>
    seasonal: Record<string, number>
  }
  templates: Array<{
    name: string
    positions: Array<{
      normCode: string
      unit: string
      quantity: number
    }>
  }>
}
