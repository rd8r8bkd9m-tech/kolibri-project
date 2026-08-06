import { Controller, Post, Body, UploadedFile, UseInterceptors } from '@nestjs/common';
import { FileInterceptor } from '@nestjs/platform-express';
import { ApiTags, ApiOperation, ApiResponse, ApiConsumes } from '@nestjs/swagger';
import { AiService } from './ai.service';

@ApiTags('ai')
@Controller('api/ai')
export class AiController {
  constructor(private readonly aiService: AiService) {}

  @Post('analyze-text')
  @ApiOperation({ summary: 'Анализ текстового описания работ' })
  @ApiResponse({ status: 200, description: 'Анализ выполнен' })
  analyzeText(@Body('description') description: string) {
    return this.aiService.analyzeWorkDescription(description);
  }

  @Post('analyze-image')
  @UseInterceptors(FileInterceptor('image'))
  @ApiConsumes('multipart/form-data')
  @ApiOperation({ summary: 'Анализ изображения объекта' })
  @ApiResponse({ status: 200, description: 'Изображение проанализировано' })
  analyzeImage(@UploadedFile() file: Express.Multer.File) {
    return this.aiService.analyzeImage(file.buffer);
  }

  @Post('generate-smeta')
  @ApiOperation({ summary: 'Генерация сметы по описанию' })
  @ApiResponse({ status: 200, description: 'Смета сгенерирована' })
  generateSmeta(@Body('prompt') prompt: string) {
    return this.aiService.generateSmeta(prompt);
  }

  @Post('parse-bim')
  @ApiOperation({ summary: 'Парсинг BIM/IFC файла' })
  @ApiResponse({ status: 200, description: 'BIM файл обработан' })
  parseBim(@Body() bimData: any) {
    return this.aiService.parseBimFile(bimData);
  }
}
