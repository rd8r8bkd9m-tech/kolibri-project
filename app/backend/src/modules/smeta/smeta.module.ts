import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { SmetaEntity } from '@/database/entities/smeta.entity';
import { SmetaPositionEntity } from '@/database/entities/smeta-position.entity';
import { SmetaService } from './smeta.service';
import { SmetaController } from './smeta.controller';

@Module({
  imports: [TypeOrmModule.forFeature([SmetaEntity, SmetaPositionEntity])],
  controllers: [SmetaController],
  providers: [SmetaService],
  exports: [SmetaService],
})
export class SmetaModule {}
