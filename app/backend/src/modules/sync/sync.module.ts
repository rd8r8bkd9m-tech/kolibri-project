import { Module } from '@nestjs/common';
import { SyncService } from './sync.service';
import { SyncController } from './sync.controller';
import { SmetaModule } from '../smeta/smeta.module';
import { NormsModule } from '../norms/norms.module';

@Module({
  imports: [SmetaModule, NormsModule],
  controllers: [SyncController],
  providers: [SyncService],
  exports: [SyncService],
})
export class SyncModule {}
