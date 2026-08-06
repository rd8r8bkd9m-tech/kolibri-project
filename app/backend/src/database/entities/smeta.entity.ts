import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn, UpdateDateColumn, OneToMany } from 'typeorm';
import { SmetaPositionEntity } from './smeta-position.entity';

@Entity('smetas')
export class SmetaEntity {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ type: 'varchar', length: 255 })
  name: string;

  @Column({ type: 'text', nullable: true })
  description: string;

  @Column({ type: 'varchar', length: 100, nullable: true })
  objectAddress: string;

  @Column({ type: 'varchar', length: 100, nullable: true })
  clientName: string;

  @Column({ type: 'varchar', length: 50, nullable: true })
  region: string;

  @Column({ type: 'decimal', precision: 15, scale: 2, default: 0 })
  totalCost: number;

  @Column({ type: 'jsonb', nullable: true })
  metadata: any; // Доп. метаданные

  @OneToMany(() => SmetaPositionEntity, position => position.smeta, { cascade: true })
  positions: SmetaPositionEntity[];

  @Column({ type: 'varchar', length: 50, default: 'draft' })
  status: string; // draft, approved, archived

  @Column({ type: 'uuid', nullable: true })
  userId: string; // ID пользователя

  @Column({ type: 'boolean', default: false })
  isTemplate: boolean;

  @CreateDateColumn()
  createdAt: Date;

  @UpdateDateColumn()
  updatedAt: Date;
}
