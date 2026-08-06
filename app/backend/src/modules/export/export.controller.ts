import { Controller, Get, Param, Res } from '@nestjs/common';
import { ApiTags, ApiOperation, ApiResponse } from '@nestjs/swagger';
import { Response } from 'express';
import { ExportService } from './export.service';

@ApiTags('export')
@Controller('api/export')
export class ExportController {
  constructor(private readonly exportService: ExportService) {}

  @Get(':id/pdf')
  @ApiOperation({ summary: 'Экспорт сметы в PDF' })
  @ApiResponse({ status: 200, description: 'PDF файл сгенерирован' })
  async exportPdf(@Param('id') id: string, @Res() res: Response) {
    const buffer = await this.exportService.exportToPdf(id);
    res.set({
      'Content-Type': 'application/pdf',
      'Content-Disposition': `attachment; filename=smeta-${id}.pdf`,
      'Content-Length': buffer.length,
    });
    res.send(buffer);
  }

  @Get(':id/excel')
  @ApiOperation({ summary: 'Экспорт сметы в Excel' })
  @ApiResponse({ status: 200, description: 'Excel файл сгенерирован' })
  async exportExcel(@Param('id') id: string, @Res() res: Response) {
    const buffer = await this.exportService.exportToExcel(id);
    res.set({
      'Content-Type': 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
      'Content-Disposition': `attachment; filename=smeta-${id}.xlsx`,
      'Content-Length': buffer.length,
    });
    res.send(buffer);
  }

  @Get(':id/word')
  @ApiOperation({ summary: 'Экспорт сметы в Word' })
  @ApiResponse({ status: 200, description: 'Word файл сгенерирован' })
  async exportWord(@Param('id') id: string, @Res() res: Response) {
    const buffer = await this.exportService.exportToWord(id);
    res.set({
      'Content-Type': 'application/vnd.openxmlformats-officedocument.wordprocessingml.document',
      'Content-Disposition': `attachment; filename=smeta-${id}.docx`,
      'Content-Length': buffer.length,
    });
    res.send(buffer);
  }

  @Get(':id/json')
  @ApiOperation({ summary: 'Экспорт сметы в JSON' })
  @ApiResponse({ status: 200, description: 'JSON данные' })
  async exportJson(@Param('id') id: string) {
    return this.exportService.exportToJson(id);
  }
}
