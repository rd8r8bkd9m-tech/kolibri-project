import { Entity, PrimaryGeneratedColumn, Column, ManyToOne, JoinColumn } from 'typeorm';
import { SmetaEntity } from './smeta.entity';
import { NormEntity } from './norm.entity';

@Entity('smeta_positions')
export class SmetaPositionEntity {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @ManyToOne(() => SmetaEntity, smeta => smeta.positions, { onDelete: 'CASCADE' })
  @JoinColumn({ name: 'smetaId' })
  smeta: SmetaEntity;

  @Column({ type: 'uuid' })
  smetaId: string;

  @ManyToOne(() => NormEntity, { nullable: true })
  @JoinColumn({ name: 'normId' })
  norm: NormEntity;

  @Column({ type: 'uuid', nullable: true })
  normId: string;

  @Column({ type: 'int' })
  orderNumber: number; // Порядковый номер в смете

  @Column({ type: 'varchar', length: 500 })
  workDescription: string; // Описание работы

  @Column({ type: 'varchar', length: 20 })
  unit: string; // Единица измерения

  @Column({ type: 'decimal', precision: 10, scale: 3 })
  quantity: number; // Объем работ

  @Column({ type: 'decimal', precision: 10, scale: 2 })
  unitPrice: number; // Цена за единицу

  @Column({ type: 'decimal', precision: 15, scale: 2 })
  totalPrice: number; // Общая стоимость

  @Column({ type: 'jsonb', nullable: true })
  calculations: any; // Расчеты объемов

  @Column({ type: 'jsonb', nullable: true })
  coefficients: any; // Примененные коэффициенты

  @Column({ type: 'text', nullable: true })
  notes: string;
}
