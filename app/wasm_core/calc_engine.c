#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

// Структура для расчета позиции сметы
typedef struct {
    double quantity;      // Объем работ
    double unit_price;    // Цена за единицу
    double total_price;   // Общая стоимость
    double coefficient;   // Коэффициент
} SmetaPosition;

// Структура для материала
typedef struct {
    char code[32];
    char name[128];
    char unit[16];
    double quantity;
    double price;
    double total;
} Material;

// Расчет позиции сметы
EMSCRIPTEN_KEEPALIVE
double calc_position(double quantity, double unit_price, double coefficient) {
    return quantity * unit_price * coefficient;
}

// Расчет объема помещения
EMSCRIPTEN_KEEPALIVE
double calc_room_volume(double length, double width, double height) {
    return length * width * height;
}

// Расчет площади пола
EMSCRIPTEN_KEEPALIVE
double calc_floor_area(double length, double width) {
    return length * width;
}

// Расчет площади стен
EMSCRIPTEN_KEEPALIVE
double calc_wall_area(double length, double width, double height) {
    double perimeter = 2.0 * (length + width);
    return perimeter * height;
}

// Расчет площади потолка (равна площади пола)
EMSCRIPTEN_KEEPALIVE
double calc_ceiling_area(double length, double width) {
    return calc_floor_area(length, width);
}

// Расчет периметра
EMSCRIPTEN_KEEPALIVE
double calc_perimeter(double length, double width) {
    return 2.0 * (length + width);
}

// Применение регионального коэффициента
EMSCRIPTEN_KEEPALIVE
double apply_regional_coefficient(double base_price, double coefficient) {
    return base_price * coefficient;
}

// Применение сезонного коэффициента
EMSCRIPTEN_KEEPALIVE
double apply_seasonal_coefficient(double base_price, double coefficient) {
    return base_price * coefficient;
}

// Расчет трудозатрат (чел-час)
EMSCRIPTEN_KEEPALIVE
double calc_labor_hours(double quantity, double norm_per_unit) {
    return quantity * norm_per_unit;
}

// Расчет стоимости материалов
EMSCRIPTEN_KEEPALIVE
double calc_materials_cost(double quantity, double material_rate, double material_price) {
    double material_quantity = quantity * material_rate;
    return material_quantity * material_price;
}

// Расчет машино-часов
EMSCRIPTEN_KEEPALIVE
double calc_machine_hours(double quantity, double norm_per_unit) {
    return quantity * norm_per_unit;
}

// Суммирование позиций сметы
EMSCRIPTEN_KEEPALIVE
double sum_positions(double* positions, int count) {
    double total = 0.0;
    for (int i = 0; i < count; i++) {
        total += positions[i];
    }
    return total;
}

// Расчет НДС
EMSCRIPTEN_KEEPALIVE
double calc_vat(double base_price, double vat_rate) {
    return base_price * vat_rate;
}

// Расчет итоговой стоимости с НДС
EMSCRIPTEN_KEEPALIVE
double calc_total_with_vat(double base_price, double vat_rate) {
    return base_price * (1.0 + vat_rate);
}

// Расчет накладных расходов
EMSCRIPTEN_KEEPALIVE
double calc_overhead(double direct_costs, double overhead_rate) {
    return direct_costs * overhead_rate;
}

// Расчет сметной прибыли
EMSCRIPTEN_KEEPALIVE
double calc_profit(double direct_costs, double profit_rate) {
    return direct_costs * profit_rate;
}

// Комплексный расчет сметы с учетом всех коэффициентов
EMSCRIPTEN_KEEPALIVE
double calc_full_smeta(
    double base_cost,
    double regional_coef,
    double seasonal_coef,
    double overhead_rate,
    double profit_rate,
    double vat_rate
) {
    // Применяем коэффициенты к базовой стоимости
    double adjusted_cost = base_cost * regional_coef * seasonal_coef;
    
    // Добавляем накладные расходы
    double with_overhead = adjusted_cost + (adjusted_cost * overhead_rate);
    
    // Добавляем сметную прибыль
    double with_profit = with_overhead + (with_overhead * profit_rate);
    
    // Добавляем НДС
    double total = with_profit * (1.0 + vat_rate);
    
    return total;
}

// Главная функция для тестирования
#ifndef __EMSCRIPTEN__
int main() {
    printf("Kolibri Smeta WASM Core\n");
    printf("======================\n\n");
    
    // Пример расчета
    double length = 5.0;
    double width = 4.0;
    double height = 2.7;
    
    printf("Помещение: %.1f x %.1f x %.1f м\n", length, width, height);
    printf("Площадь пола: %.2f м²\n", calc_floor_area(length, width));
    printf("Площадь стен: %.2f м²\n", calc_wall_area(length, width, height));
    printf("Объем: %.2f м³\n", calc_room_volume(length, width, height));
    printf("Периметр: %.2f м\n\n", calc_perimeter(length, width));
    
    // Пример расчета позиции
    double base_price = 1000.0;
    double quantity = 20.0;
    double unit_price = 50.0;
    
    printf("Расчет позиции:\n");
    printf("Объем: %.1f м²\n", quantity);
    printf("Цена за ед.: %.2f руб.\n", unit_price);
    printf("Базовая стоимость: %.2f руб.\n", base_price);
    
    double total = calc_full_smeta(base_price, 1.15, 1.0, 0.12, 0.08, 0.20);
    printf("Итоговая стоимость: %.2f руб.\n", total);
    
    return 0;
}
#endif
