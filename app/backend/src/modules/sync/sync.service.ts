import { Injectable } from '@nestjs/common';
import { SmetaService } from '../smeta/smeta.service';
import { NormsService } from '../norms/norms.service';

@Injectable()
export class SyncService {
  constructor(
    private readonly smetaService: SmetaService,
    private readonly normsService: NormsService,
  ) {}

  async syncFromDevice(userId: string, data: any): Promise<any> {
    // Синхронизация данных с устройства на сервер
    const synced = {
      smetas: [],
      errors: [],
    };
    
    // Синхронизация смет
    if (data.smetas && Array.isArray(data.smetas)) {
      for (const smetaData of data.smetas) {
        try {
          let smeta;
          if (smetaData.id) {
            // Обновление существующей сметы
            smeta = await this.smetaService.update(smetaData.id, {
              ...smetaData,
              userId,
            });
          } else {
            // Создание новой сметы
            smeta = await this.smetaService.create({
              ...smetaData,
              userId,
            });
          }
          synced.smetas.push(smeta);
        } catch (error) {
          synced.errors.push({
            type: 'smeta',
            data: smetaData,
            error: error.message,
          });
        }
      }
    }
    
    return synced;
  }

  async syncToDevice(userId: string, lastSyncTime?: Date): Promise<any> {
    // Синхронизация данных с сервера на устройство
    const data: any = {
      timestamp: new Date(),
      smetas: [],
      norms: [],
    };
    
    // Получить сметы пользователя
    const smetas = await this.smetaService.findAll(userId);
    
    if (lastSyncTime) {
      // Только измененные с последней синхронизации
      data.smetas = smetas.filter(s => s.updatedAt > lastSyncTime);
    } else {
      data.smetas = smetas;
    }
    
    // Получить базовые нормы (ограниченный набор для оффлайна)
    data.norms = await this.normsService.findAll({ limit: 1000 });
    
    return data;
  }

  async getOfflinePackage(): Promise<any> {
    // Пакет данных для полной оффлайн работы
    return {
      version: '1.0.0',
      norms: await this.normsService.findAll({ limit: 5000 }),
      coefficients: {
        regional: {
          moscow: 1.15,
          spb: 1.12,
          kazan: 0.95,
          novosibirsk: 0.98,
        },
        seasonal: {
          winter: 1.1,
          summer: 1.0,
        },
      },
      templates: [
        {
          name: 'Штукатурка стен',
          positions: [
            { normCode: 'ФЕР15-01-001-1', unit: 'м2', quantity: 0 },
            { normCode: 'ФЕР15-01-002-1', unit: 'м2', quantity: 0 },
          ],
        },
      ],
    };
  }

  async resolveConflicts(conflicts: any[]): Promise<any> {
    // Разрешение конфликтов при синхронизации
    const resolved = [];
    
    for (const conflict of conflicts) {
      // Стратегия: последнее изменение побеждает
      if (conflict.serverUpdatedAt > conflict.localUpdatedAt) {
        resolved.push({
          id: conflict.id,
          resolution: 'server_wins',
          data: conflict.serverData,
        });
      } else {
        resolved.push({
          id: conflict.id,
          resolution: 'local_wins',
          data: conflict.localData,
        });
      }
    }
    
    return resolved;
  }
}
