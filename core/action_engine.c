/*
 * action_engine.c
 *
 * Реализация Reasoning-guided action loops.
 */

#include "kolibri/action_engine.h"
#include "kolibri/tool_registry.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int kolibri_ae_init_loop(KolibriActionLoop *loop, const char *goal, KolibriGenome *genome) {
    if (!loop || !goal) return -1;
    memset(loop, 0, sizeof(KolibriActionLoop));
    strncpy(loop->goal, goal, sizeof(loop->goal) - 1);
    loop->overall_status = KAE_STATUS_PENDING;
    loop->genome = genome;
    kolibri_tr_init(); /* Initialize tools */
    return 0;
}

int kolibri_ae_plan_step(KolibriActionLoop *loop, const KolibriReasoningResult *reasoning) {
    if (!loop || !reasoning || loop->num_actions >= 16) return -1;

    KolibriAction *act = &loop->actions[loop->num_actions];

    /* Если задан внешний обработчик выбора инструментов, используем его */
    if (loop->tool_selector) {
        if (loop->tool_selector(reasoning, act) == 0) {
            act->status = KAE_STATUS_PENDING;
            act->confidence = reasoning->confidence;
            loop->num_actions++;
            return 0;
        }
    }

    /* Логика по умолчанию (fallback) */
    strncpy(act->name, "Internal Analysis", 63);
    act->type = KAE_ACTION_REASONING;
    strncpy(act->reasoning_justification, reasoning->answer, 511);
    act->status = KAE_STATUS_PENDING;
    act->confidence = reasoning->confidence;

    loop->num_actions++;
    return 0;
}

int kolibri_ae_execute_current(KolibriActionLoop *loop) {
    if (!loop || loop->current_action_idx >= loop->num_actions) return -1;

    KolibriAction *act = &loop->actions[loop->current_action_idx];

    /* Safety Check: если инструмент небезопасен, блокируем выполнение до подтверждения */
    if (act->type == KAE_ACTION_TOOL_USE) {
        const KolibriTool *tool = kolibri_tr_get(act->tool_id);
        if (tool && !tool->is_safe && act->status != KAE_STATUS_EXECUTING) {
            act->status = KAE_STATUS_BLOCKED;
            printf("ACTION BLOCKED: Tool '%s' requires manual confirmation.\n", act->tool_id);
            return 1;
        }
    }

    act->status = KAE_STATUS_EXECUTING;

    if (act->type == KAE_ACTION_TOOL_USE) {
        if (kolibri_tr_execute(act->tool_id, act->parameters, act->result, sizeof(act->result)) == 0) {
            act->status = KAE_STATUS_SUCCESS;
            /* Log success to Genome */
            if (loop->genome) {
                char log_payload[KOLIBRI_PAYLOAD_SIZE];
                snprintf(log_payload, sizeof(log_payload), "Action: %s, Tool: %s, Result: %s",
                         act->name, act->tool_id, act->result);
                kg_append(loop->genome, "ACTION_SUCCESS", log_payload, NULL);
            }
        } else {
            act->status = KAE_STATUS_FAILURE;
            snprintf(act->result, sizeof(act->result), "Tool execution failed: %s", act->tool_id);
        }
    } else {
        snprintf(act->result, 2047, "Reasoning step completed.");
        act->status = KAE_STATUS_SUCCESS;
        if (loop->genome) {
            kg_append(loop->genome, "REASON_STEP", act->reasoning_justification, NULL);
        }
    }

    return 0;
}

int kolibri_ae_verify_last(KolibriActionLoop *loop) {
    if (!loop || loop->current_action_idx >= loop->num_actions) return -1;

    KolibriAction *act = &loop->actions[loop->current_action_idx];
    /* Верификация: проверяем содержит ли результат успех */
    if (strstr(act->result, "Success") || strstr(act->result, "completed")) {
        act->status = KAE_STATUS_SUCCESS;
        loop->current_action_idx++;
        loop->progress = (double)loop->current_action_idx / loop->num_actions;
        return 0;
    }

    act->status = KAE_STATUS_FAILURE;
    return 1;
}

int kolibri_ae_run_to_goal(KolibriActionLoop *loop, int max_steps) {
    printf("Starting Action Loop for goal: %s\n", loop->goal);

    for (int i = 0; i < max_steps; i++) {
        if (loop->current_action_idx >= loop->num_actions && loop->num_actions > 0) {
            loop->overall_status = KAE_STATUS_SUCCESS;
            break;
        }

        /* В реальной системе здесь был бы вызов планировщика на каждом шаге */
        if (kolibri_ae_execute_current(loop) == 0) {
            kolibri_ae_verify_last(loop);
        } else {
            break;
        }
    }

    return (loop->overall_status == KAE_STATUS_SUCCESS) ? 0 : -1;
}
