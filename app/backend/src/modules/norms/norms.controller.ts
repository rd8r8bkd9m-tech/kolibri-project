import { Controller, Get, Post, Body, Query, Param } from '@nestjs/common';
import { ApiTags, ApiOperation, ApiResponse } from '@nestjs/swagger';
import { NormsService } from './norms.service';

@ApiTags('norms')
@Controller('api/norms')
export class NormsController {
  constructor(private readonly normsService: NormsService) {}

  @Get()
  @ApiOperation({ summary: 'Получить список норм' })
  @ApiResponse({ status: 200, description: 'Список норм' })
  findAll(@Query() filters: any) {
    return this.normsService.findAll(filters);
  }

  @Get('search')
  @ApiOperation({ summary: 'Поиск норм' })
  @ApiResponse({ status: 200, description: 'Результаты поиска' })
  search(@Query('q') query: string) {
    return this.normsService.search(query);
  }

  @Get('code/:code')
  @ApiOperation({ summary: 'Получить норму по коду' })
  @ApiResponse({ status: 200, description: 'Норма найдена' })
  findByCode(@Param('code') code: string) {
    return this.normsService.findByCode(code);
  }

  @Post()
  @ApiOperation({ summary: 'Создать пользовательскую норму' })
  @ApiResponse({ status: 201, description: 'Норма создана' })
  create(@Body() createNormDto: any) {
    return this.normsService.create(createNormDto);
  }

  @Post('import')
  @ApiOperation({ summary: 'Импорт норм из CSV' })
  @ApiResponse({ status: 200, description: 'Нормы импортированы' })
  import(@Body('csvData') csvData: string) {
    return this.normsService.importFromCsv(csvData);
  }

  @Post('ai-match')
  @ApiOperation({ summary: 'AI подбор норм по описанию работ' })
  @ApiResponse({ status: 200, description: 'Подходящие нормы найдены' })
  aiMatch(@Body('description') description: string) {
    return this.normsService.aiMatchNorm(description);
  }

  @Post(':id/apply-coefficients')
  @ApiOperation({ summary: 'Применить коэффициенты к норме' })
  @ApiResponse({ status: 200, description: 'Коэффициенты применены' })
  applyCoefficients(@Param('id') id: string, @Body() coefficients: any) {
    return this.normsService.applyCoefficients(id, coefficients);
  }
}
