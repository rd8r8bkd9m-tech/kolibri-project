import { Injectable } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, Like } from 'typeorm';
import { NormEntity } from '@/database/entities/norm.entity';

@Injectable()
export class NormsService {
  constructor(
    @InjectRepository(NormEntity)
    private normRepository: Repository<NormEntity>,
  ) {}

  async findAll(filters?: any): Promise<NormEntity[]> {
    const where: any = { isActive: true };
    
    if (filters?.standard) {
      where.standard = filters.standard;
    }
    if (filters?.category) {
      where.category = filters.category;
    }
    if (filters?.region) {
      where.region = filters.region;
    }
    
    return this.normRepository.find({ where });
  }

  async search(query: string): Promise<NormEntity[]> {
    return this.normRepository.find({
      where: [
        { code: Like(`%${query}%`) },
        { name: Like(`%${query}%`) },
        { description: Like(`%${query}%`) },
      ],
      take: 50,
    });
  }

  async findByCode(code: string): Promise<NormEntity> {
    return this.normRepository.findOne({ where: { code } });
  }

  async create(createNormDto: any): Promise<NormEntity> {
    const norm = this.normRepository.create({ ...createNormDto, isCustom: true });
    return this.normRepository.save(norm);
  }

  async importFromCsv(csvData: string): Promise<number> {
    // Парсинг CSV и импорт норм
    const lines = csvData.split('\n');
    const norms = [];
    
    // Пропускаем заголовок
    for (let i = 1; i < lines.length; i++) {
      const line = lines[i].trim();
      if (!line) continue;
      
      const fields = line.split(',');
      if (fields.length < 7) continue;
      
      norms.push({
        code: fields[0],
        standard: fields[1],
        name: fields[2],
        description: fields[3],
        unit: fields[4],
        basePrice: parseFloat(fields[5]),
        category: fields[6],
        isCustom: false,
      });
    }
    
    await this.normRepository.save(norms);
    return norms.length;
  }

  async aiMatchNorm(workDescription: string): Promise<NormEntity[]> {
    // Простой алгоритм подбора норм по описанию работ
    // В реальности здесь будет ML модель
    const keywords = workDescription.toLowerCase().split(' ');
    
    const results = await this.normRepository
      .createQueryBuilder('norm')
      .where('LOWER(norm.name) LIKE ANY(ARRAY[:...keywords])', {
        keywords: keywords.map(k => `%${k}%`),
      })
      .orWhere('LOWER(norm.description) LIKE ANY(ARRAY[:...keywords])', {
        keywords: keywords.map(k => `%${k}%`),
      })
      .take(10)
      .getMany();
    
    return results;
  }

  async applyCoefficients(normId: string, coefficients: any): Promise<any> {
    const norm = await this.normRepository.findOne({ where: { id: normId } });
    
    if (!norm) {
      return null;
    }
    
    let adjustedPrice = Number(norm.basePrice);
    
    // Применение коэффициентов
    if (coefficients.regional) {
      adjustedPrice *= coefficients.regional;
    }
    if (coefficients.seasonal) {
      adjustedPrice *= coefficients.seasonal;
    }
    if (coefficients.complexity) {
      adjustedPrice *= coefficients.complexity;
    }
    
    return {
      normId: norm.id,
      code: norm.code,
      basePrice: norm.basePrice,
      adjustedPrice: Math.round(adjustedPrice * 100) / 100,
      coefficients,
    };
  }
}
