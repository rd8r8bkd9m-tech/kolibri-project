import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { NormEntity } from '@/database/entities/norm.entity';
import { NormsService } from './norms.service';
import { NormsController } from './norms.controller';

@Module({
  imports: [TypeOrmModule.forFeature([NormEntity])],
  controllers: [NormsController],
  providers: [NormsService],
  exports: [NormsService],
})
export class NormsModule {}
