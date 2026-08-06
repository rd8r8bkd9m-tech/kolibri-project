import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { SmetaEntity } from '@/database/entities/smeta.entity';
import { SmetaPositionEntity } from '@/database/entities/smeta-position.entity';

@Injectable()
export class SmetaService {
  constructor(
    @InjectRepository(SmetaEntity)
    private smetaRepository: Repository<SmetaEntity>,
    @InjectRepository(SmetaPositionEntity)
    private positionRepository: Repository<SmetaPositionEntity>,
  ) {}

  async create(createSmetaDto: any): Promise<SmetaEntity> {
    const smeta = this.smetaRepository.create(createSmetaDto);
    return this.smetaRepository.save(smeta);
  }

  async findAll(userId?: string): Promise<SmetaEntity[]> {
    const query = this.smetaRepository.createQueryBuilder('smeta')
      .leftJoinAndSelect('smeta.positions', 'positions')
      .orderBy('smeta.createdAt', 'DESC');
    
    if (userId) {
      query.where('smeta.userId = :userId', { userId });
    }
    
    return query.getMany();
  }

  async findOne(id: string): Promise<SmetaEntity> {
    const smeta = await this.smetaRepository.findOne({
      where: { id },
      relations: ['positions', 'positions.norm'],
    });
    
    if (!smeta) {
      throw new NotFoundException(`Смета с ID ${id} не найдена`);
    }
    
    return smeta;
  }

  async update(id: string, updateSmetaDto: any): Promise<SmetaEntity> {
    const smeta = await this.findOne(id);
    Object.assign(smeta, updateSmetaDto);
    return this.smetaRepository.save(smeta);
  }

  async remove(id: string): Promise<void> {
    const result = await this.smetaRepository.delete(id);
    if (result.affected === 0) {
      throw new NotFoundException(`Смета с ID ${id} не найдена`);
    }
  }

  async addPosition(smetaId: string, positionDto: any): Promise<SmetaPositionEntity> {
    const smeta = await this.findOne(smetaId);
    
    const position = this.positionRepository.create({
      ...positionDto,
      smetaId: smeta.id,
      totalPrice: positionDto.quantity * positionDto.unitPrice,
    });
    
    const savedPosition = await this.positionRepository.save(position);
    
    // Пересчитать общую стоимость сметы
    await this.recalculateTotalCost(smetaId);
    
    return savedPosition;
  }

  async removePosition(positionId: string): Promise<void> {
    const position = await this.positionRepository.findOne({
      where: { id: positionId },
    });
    
    if (!position) {
      throw new NotFoundException(`Позиция с ID ${positionId} не найдена`);
    }
    
    const smetaId = position.smetaId;
    await this.positionRepository.delete(positionId);
    await this.recalculateTotalCost(smetaId);
  }

  private async recalculateTotalCost(smetaId: string): Promise<void> {
    const positions = await this.positionRepository.find({
      where: { smetaId },
    });
    
    const totalCost = positions.reduce((sum, pos) => sum + Number(pos.totalPrice), 0);
    
    await this.smetaRepository.update(smetaId, { totalCost });
  }

  async calculateVolumes(description: string): Promise<any> {
    // Парсинг описания и расчет объемов
    // Пример: "Комната 3×5, высота 2.7. Штукатурка стен"
    const roomMatch = description.match(/(\d+(?:\.\d+)?)\s*[×x]\s*(\d+(?:\.\d+)?)/);
    const heightMatch = description.match(/высота\s+(\d+(?:\.\d+)?)/i);
    
    if (roomMatch) {
      const length = parseFloat(roomMatch[1]);
      const width = parseFloat(roomMatch[2]);
      const height = heightMatch ? parseFloat(heightMatch[1]) : 2.7;
      
      const floorArea = length * width;
      const wallArea = 2 * (length + width) * height;
      const volume = floorArea * height;
      
      return {
        length,
        width,
        height,
        floorArea: Math.round(floorArea * 100) / 100,
        wallArea: Math.round(wallArea * 100) / 100,
        volume: Math.round(volume * 100) / 100,
      };
    }
    
    return null;
  }
}
