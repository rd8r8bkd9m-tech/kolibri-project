#include <metal_stdlib>
using namespace metal;

/*
 * Ядро для генеративного кодирования (Уровень 1 -> Уровень 2)
 *
 * Каждый поток GPU выполняет эту функцию для одного байта.
 * Преобразует один байт в его 3-значное десятичное представление.
 * Например: байт 255 -> символы '2', '5', '5'.
 */
kernel void decimal_encode_kernel(
    const device uchar *input_data [[buffer(0)]], // Входной буфер с исходными данными
    device uchar *output_data [[buffer(1)]],      // Выходной буфер для L2 (decimal) данных
    uint thread_id [[thread_position_in_grid]])  // ID текущего потока
{
    // 1. Получаем байт, за который отвечает этот поток
    uchar current_byte = input_data[thread_id];
    
    // 2. Вычисляем три цифры (эквивалент sprintf("%03d", current_byte) на GPU)
    uchar digit1 = (current_byte / 100) % 10;
    uchar digit2 = (current_byte / 10) % 10;
    uchar digit3 = current_byte % 10;
    
    // 3. Записываем ASCII-коды цифр в выходной буфер
    // Каждый входной байт занимает 3 выходных байта
    output_data[thread_id * 3 + 0] = digit1 + '0';
    output_data[thread_id * 3 + 1] = digit2 + '0';
    output_data[thread_id * 3 + 2] = digit3 + '0';
}
