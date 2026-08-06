import { Controller, Get, Post, Body, Patch, Param, Delete, Query } from '@nestjs/common';
import { ApiTags, ApiOperation, ApiResponse } from '@nestjs/swagger';
import { SmetaService } from './smeta.service';

@ApiTags('smeta')
@Controller('api/smeta')
export class SmetaController {
  constructor(private readonly smetaService: SmetaService) {}

  @Post()
  @ApiOperation({ summary: 'Создать новую смету' })
  @ApiResponse({ status: 201, description: 'Смета создана' })
  create(@Body() createSmetaDto: any) {
    return this.smetaService.create(createSmetaDto);
  }

  @Get()
  @ApiOperation({ summary: 'Получить список всех смет' })
  @ApiResponse({ status: 200, description: 'Список смет' })
  findAll(@Query('userId') userId?: string) {
    return this.smetaService.findAll(userId);
  }

  @Get(':id')
  @ApiOperation({ summary: 'Получить смету по ID' })
  @ApiResponse({ status: 200, description: 'Смета найдена' })
  @ApiResponse({ status: 404, description: 'Смета не найдена' })
  findOne(@Param('id') id: string) {
    return this.smetaService.findOne(id);
  }

  @Patch(':id')
  @ApiOperation({ summary: 'Обновить смету' })
  @ApiResponse({ status: 200, description: 'Смета обновлена' })
  update(@Param('id') id: string, @Body() updateSmetaDto: any) {
    return this.smetaService.update(id, updateSmetaDto);
  }

  @Delete(':id')
  @ApiOperation({ summary: 'Удалить смету' })
  @ApiResponse({ status: 200, description: 'Смета удалена' })
  remove(@Param('id') id: string) {
    return this.smetaService.remove(id);
  }

  @Post(':id/positions')
  @ApiOperation({ summary: 'Добавить позицию в смету' })
  @ApiResponse({ status: 201, description: 'Позиция добавлена' })
  addPosition(@Param('id') id: string, @Body() positionDto: any) {
    return this.smetaService.addPosition(id, positionDto);
  }

  @Delete('positions/:positionId')
  @ApiOperation({ summary: 'Удалить позицию из сметы' })
  @ApiResponse({ status: 200, description: 'Позиция удалена' })
  removePosition(@Param('positionId') positionId: string) {
    return this.smetaService.removePosition(positionId);
  }

  @Post('calculate-volumes')
  @ApiOperation({ summary: 'Рассчитать объемы по описанию' })
  @ApiResponse({ status: 200, description: 'Объемы рассчитаны' })
  calculateVolumes(@Body('description') description: string) {
    return this.smetaService.calculateVolumes(description);
  }
}
