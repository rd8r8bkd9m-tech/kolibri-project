import { Injectable } from '@nestjs/common';
import { SmetaService } from '../smeta/smeta.service';
import { PDFDocument, rgb, StandardFonts } from 'pdf-lib';
import * as ExcelJS from 'exceljs';
import Docxtemplater from 'docxtemplater';
import PizZip from 'pizzip';
import * as fs from 'fs';
import * as path from 'path';

@Injectable()
export class ExportService {
  constructor(private readonly smetaService: SmetaService) {}

  async exportToPdf(smetaId: string): Promise<Buffer> {
    const smeta = await this.smetaService.findOne(smetaId);
    
    const pdfDoc = await PDFDocument.create();
    const page = pdfDoc.addPage([595, 842]); // A4
    const font = await pdfDoc.embedFont(StandardFonts.Helvetica);
    const boldFont = await pdfDoc.embedFont(StandardFonts.HelveticaBold);
    
    let yPos = 800;
    
    // Заголовок
    page.drawText('ЛОКАЛЬНАЯ СМЕТА', {
      x: 50,
      y: yPos,
      size: 16,
      font: boldFont,
      color: rgb(0, 0, 0),
    });
    
    yPos -= 30;
    page.drawText(`Наименование: ${smeta.name}`, {
      x: 50,
      y: yPos,
      size: 12,
      font: font,
    });
    
    yPos -= 20;
    page.drawText(`Объект: ${smeta.objectAddress || 'Не указан'}`, {
      x: 50,
      y: yPos,
      size: 10,
      font: font,
    });
    
    yPos -= 40;
    
    // Таблица заголовков
    page.drawText('№', { x: 50, y: yPos, size: 10, font: boldFont });
    page.drawText('Наименование работ', { x: 80, y: yPos, size: 10, font: boldFont });
    page.drawText('Ед.изм.', { x: 300, y: yPos, size: 10, font: boldFont });
    page.drawText('Кол-во', { x: 360, y: yPos, size: 10, font: boldFont });
    page.drawText('Цена', { x: 420, y: yPos, size: 10, font: boldFont });
    page.drawText('Стоимость', { x: 480, y: yPos, size: 10, font: boldFont });
    
    yPos -= 20;
    
    // Позиции
    for (const position of smeta.positions) {
      if (yPos < 50) {
        // Новая страница если место закончилось
        const newPage = pdfDoc.addPage([595, 842]);
        yPos = 800;
      }
      
      page.drawText(position.orderNumber.toString(), { x: 50, y: yPos, size: 9, font: font });
      page.drawText(position.workDescription.substring(0, 30), { x: 80, y: yPos, size: 9, font: font });
      page.drawText(position.unit, { x: 300, y: yPos, size: 9, font: font });
      page.drawText(position.quantity.toString(), { x: 360, y: yPos, size: 9, font: font });
      page.drawText(position.unitPrice.toString(), { x: 420, y: yPos, size: 9, font: font });
      page.drawText(position.totalPrice.toString(), { x: 480, y: yPos, size: 9, font: font });
      
      yPos -= 15;
    }
    
    yPos -= 20;
    
    // Итого
    page.drawText(`ИТОГО: ${smeta.totalCost} руб.`, {
      x: 400,
      y: yPos,
      size: 12,
      font: boldFont,
    });
    
    return Buffer.from(await pdfDoc.save());
  }

  async exportToExcel(smetaId: string): Promise<Buffer> {
    const smeta = await this.smetaService.findOne(smetaId);
    
    const workbook = new ExcelJS.Workbook();
    const worksheet = workbook.addWorksheet('Смета');
    
    // Заголовок
    worksheet.mergeCells('A1:F1');
    worksheet.getCell('A1').value = 'ЛОКАЛЬНАЯ СМЕТА';
    worksheet.getCell('A1').font = { bold: true, size: 14 };
    worksheet.getCell('A1').alignment = { horizontal: 'center' };
    
    worksheet.getCell('A2').value = `Наименование: ${smeta.name}`;
    worksheet.getCell('A3').value = `Объект: ${smeta.objectAddress || 'Не указан'}`;
    worksheet.getCell('A4').value = `Дата: ${new Date().toLocaleDateString('ru-RU')}`;
    
    // Шапка таблицы
    worksheet.addRow([]);
    const headerRow = worksheet.addRow(['№', 'Наименование работ', 'Ед.изм.', 'Кол-во', 'Цена', 'Стоимость']);
    headerRow.font = { bold: true };
    headerRow.alignment = { horizontal: 'center' };
    
    // Позиции
    for (const position of smeta.positions) {
      worksheet.addRow([
        position.orderNumber,
        position.workDescription,
        position.unit,
        position.quantity,
        position.unitPrice,
        position.totalPrice,
      ]);
    }
    
    // Итого
    worksheet.addRow([]);
    const totalRow = worksheet.addRow(['', '', '', '', 'ИТОГО:', smeta.totalCost]);
    totalRow.font = { bold: true };
    
    // Форматирование колонок
    worksheet.columns = [
      { width: 5 },
      { width: 50 },
      { width: 10 },
      { width: 10 },
      { width: 12 },
      { width: 15 },
    ];
    
    return Buffer.from(await workbook.xlsx.writeBuffer());
  }

  async exportToWord(smetaId: string): Promise<Buffer> {
    const smeta = await this.smetaService.findOne(smetaId);
    
    // Шаблон документа Word
    const template = `
      <w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
        <w:body>
          <w:p><w:r><w:t>ЛОКАЛЬНАЯ СМЕТА</w:t></w:r></w:p>
          <w:p><w:r><w:t>Наименование: ${smeta.name}</w:t></w:r></w:p>
          <w:p><w:r><w:t>Объект: ${smeta.objectAddress || 'Не указан'}</w:t></w:r></w:p>
        </w:body>
      </w:document>
    `;
    
    // Упрощенная версия - в реальности нужен полноценный DOCX шаблон
    return Buffer.from(template);
  }

  async exportToJson(smetaId: string): Promise<any> {
    const smeta = await this.smetaService.findOne(smetaId);
    return smeta;
  }
}
