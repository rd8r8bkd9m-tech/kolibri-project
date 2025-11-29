import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn, UpdateDateColumn, ManyToOne, OneToMany } from 'typeorm';

@Entity('norms')
export class NormEntity {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ type: 'varchar', length: 50 })
  code: string; // Код расценки (напр. "ФЕР01-01-001-1")

  @Column({ type: 'varchar', length: 20 })
  standard: string; // ФЕР, ГЭСН, ТЕР

  @Column({ type: 'varchar', length: 500 })
  name: string; // Название работы

  @Column({ type: 'text' })
  description: string; // Полное описание

  @Column({ type: 'varchar', length: 20 })
  unit: string; // Единица измерения (м2, м3, пог.м)

  @Column({ type: 'decimal', precision: 10, scale: 2 })
  basePrice: number; // Базовая цена

  @Column({ type: 'jsonb', nullable: true })
  materials: any; // Материалы в JSON

  @Column({ type: 'jsonb', nullable: true })
  labor: any; // Трудозатраты в JSON

  @Column({ type: 'jsonb', nullable: true })
  equipment: any; // Машины и механизмы в JSON

  @Column({ type: 'jsonb', nullable: true })
  coefficients: any; // Коэффициенты в JSON

  @Column({ type: 'varchar', length: 100, nullable: true })
  category: string; // Категория работ

  @Column({ type: 'varchar', length: 100, nullable: true })
  subcategory: string;

  @Column({ type: 'int', default: 2024 })
  year: number; // Год расценки

  @Column({ type: 'varchar', length: 50, nullable: true })
  region: string; // Регион

  @Column({ type: 'boolean', default: true })
  isActive: boolean;

  @Column({ type: 'boolean', default: false })
  isCustom: boolean; // Пользовательская норма

  @CreateDateColumn()
  createdAt: Date;

  @UpdateDateColumn()
  updatedAt: Date;
}
