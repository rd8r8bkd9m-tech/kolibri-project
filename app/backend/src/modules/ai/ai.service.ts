import { Injectable } from '@nestjs/common';
import { NormsService } from '../norms/norms.service';

@Injectable()
export class AiService {
  constructor(private readonly normsService: NormsService) {}

  async analyzeWorkDescription(description: string): Promise<any> {
    // AI анализ описания работ
    // В production здесь будет TensorFlow.js модель
    
    const workTypes = this.classifyWorkType(description);
    const suggestedNorms = await this.normsService.aiMatchNorm(description);
    const volumes = this.extractVolumes(description);
    
    return {
      description,
      workTypes,
      suggestedNorms: suggestedNorms.slice(0, 5),
      volumes,
      confidence: 0.85,
    };
  }

  async analyzeImage(imageBuffer: Buffer): Promise<any> {
    // AI анализ изображения
    // В production здесь будет компьютерное зрение (TensorFlow)
    
    // Заглушка для демонстрации
    return {
      detectedObjects: [
        { type: 'wall', confidence: 0.92 },
        { type: 'floor', confidence: 0.88 },
        { type: 'ceiling', confidence: 0.85 },
      ],
      estimatedDimensions: {
        width: 5.0,
        length: 4.0,
        height: 2.7,
      },
      suggestedWorks: [
        'Штукатурка стен',
        'Устройство стяжки пола',
        'Окраска потолка',
      ],
    };
  }

  async generateSmeta(prompt: string): Promise<any> {
    // AI генерация сметы по текстовому описанию
    
    const analysis = await this.analyzeWorkDescription(prompt);
    
    const positions = [];
    for (const norm of analysis.suggestedNorms) {
      positions.push({
        normId: norm.id,
        workDescription: norm.name,
        unit: norm.unit,
        quantity: analysis.volumes?.wallArea || 10.0,
        unitPrice: norm.basePrice,
        totalPrice: (analysis.volumes?.wallArea || 10.0) * Number(norm.basePrice),
      });
    }
    
    return {
      name: `Смета: ${prompt.substring(0, 50)}...`,
      description: prompt,
      positions,
      totalCost: positions.reduce((sum, p) => sum + p.totalPrice, 0),
      aiGenerated: true,
    };
  }

  async parseBimFile(bimData: any): Promise<any> {
    // Парсинг BIM/IFC файлов
    // В production здесь будет полноценный парсер IFC
    
    return {
      elements: [],
      volumes: {},
      materials: [],
      message: 'BIM/IFC парсинг в разработке',
    };
  }

  private classifyWorkType(description: string): string[] {
    const types = [];
    const lower = description.toLowerCase();
    
    if (lower.includes('штукатурка') || lower.includes('стен')) types.push('Штукатурные работы');
    if (lower.includes('окраска') || lower.includes('покраска')) types.push('Малярные работы');
    if (lower.includes('стяжка') || lower.includes('пол')) types.push('Полы');
    if (lower.includes('кровля') || lower.includes('крыша')) types.push('Кровельные работы');
    if (lower.includes('фундамент') || lower.includes('бетон')) types.push('Бетонные работы');
    
    return types.length > 0 ? types : ['Общестроительные работы'];
  }

  private extractVolumes(description: string): any {
    // Извлечение размеров из текста
    const roomMatch = description.match(/(\d+(?:\.\d+)?)\s*[×x]\s*(\d+(?:\.\d+)?)/);
    const heightMatch = description.match(/высота\s+(\d+(?:\.\d+)?)/i);
    
    if (roomMatch) {
      const length = parseFloat(roomMatch[1]);
      const width = parseFloat(roomMatch[2]);
      const height = heightMatch ? parseFloat(heightMatch[1]) : 2.7;
      
      return {
        length,
        width,
        height,
        floorArea: Math.round(length * width * 100) / 100,
        wallArea: Math.round(2 * (length + width) * height * 100) / 100,
        volume: Math.round(length * width * height * 100) / 100,
      };
    }
    
    return null;
  }
}
