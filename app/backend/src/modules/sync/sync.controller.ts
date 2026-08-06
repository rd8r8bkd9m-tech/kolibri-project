import { Controller, Post, Get, Body, Query } from '@nestjs/common';
import { ApiTags, ApiOperation, ApiResponse } from '@nestjs/swagger';
import { SyncService } from './sync.service';

@ApiTags('sync')
@Controller('api/sync')
export class SyncController {
  constructor(private readonly syncService: SyncService) {}

  @Post('from-device')
  @ApiOperation({ summary: 'Синхронизация данных с устройства' })
  @ApiResponse({ status: 200, description: 'Данные синхронизированы' })
  syncFromDevice(@Body() data: any) {
    const userId = data.userId || 'default';
    return this.syncService.syncFromDevice(userId, data);
  }

  @Get('to-device')
  @ApiOperation({ summary: 'Синхронизация данных на устройство' })
  @ApiResponse({ status: 200, description: 'Данные получены' })
  syncToDevice(
    @Query('userId') userId: string,
    @Query('lastSync') lastSync?: string,
  ) {
    const lastSyncDate = lastSync ? new Date(lastSync) : undefined;
    return this.syncService.syncToDevice(userId, lastSyncDate);
  }

  @Get('offline-package')
  @ApiOperation({ summary: 'Получить пакет данных для оффлайн работы' })
  @ApiResponse({ status: 200, description: 'Пакет сформирован' })
  getOfflinePackage() {
    return this.syncService.getOfflinePackage();
  }

  @Post('resolve-conflicts')
  @ApiOperation({ summary: 'Разрешить конфликты синхронизации' })
  @ApiResponse({ status: 200, description: 'Конфликты разрешены' })
  resolveConflicts(@Body('conflicts') conflicts: any[]) {
    return this.syncService.resolveConflicts(conflicts);
  }
}
