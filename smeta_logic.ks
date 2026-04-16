// KolibriScript: Smeta Logic Pack v1.0 (RU)
// Специализация: Сметное дело России (Методика 421/пр)

define logic SMETA_VAT_RATE = 0.20;
define logic SMETA_RECONSTRUCTION_COEF = 1.15;

formula CALCULATE_TOTAL_WITH_VAT(base_value) {
    return base_value * (1 + SMETA_VAT_RATE);
}

formula APPLY_RECONSTRUCTION(base_fot) {
    return base_fot * SMETA_RECONSTRUCTION_COEF;
}

rule "НДС 20%" {
    if (context.has("смета") || context.has("стоимость")) {
        apply CALCULATE_TOTAL_WITH_VAT;
        output "Применен НДС 20% согласно НК РФ.";
    }
}

rule "Методика 421/пр" {
    if (query.mentions("коэффициент на демонтаж")) {
        set coef_fot = 0.8;
        set coef_mat = 0.3;
        output "Для демонтажа применены коэффициенты 0.8 к ФОТ и 0.3 к материалам (п. 58 Методики 421/пр).";
    }
}

// Связь с фрактальной памятью
associate "смета" -> "421/пр" strength 1.0;
associate "НДС" -> "20%" strength 0.95;
associate "демонтаж" -> "0.8" strength 0.9;
