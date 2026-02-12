/*
 * kolibri_train_kat.c
 *
 * Быстрое обучение KAT Transformer на текстовом корпусе
 *
 * Стратегия: прямое обучение через kat_train_step() на коротких
 * последовательностях (seq_len=32), минуя побайтовый kwm_observe().
 * Это ~100× быстрее чем auto_learn pipeline.
 *
 * Для корпуса 8 МБ одна эпоха занимает ~30 секунд.
 *
 * Использование:
 *   ./kolibri_train_kat --corpus data/corpus --epochs 3 --model trained.kwm
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/attention.h"
#include "kolibri/world_model.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ============================================================================
 * Конфигурация
 * ============================================================================ */

#define MAX_CORPUS_SIZE   (9 * 1024 * 1024)  /* 9 МБ максимум */
#define SEQ_LEN           32                  /* Длина последовательности */
#define STEP_SIZE         128                 /* Шаг окна по данным */
#define EVAL_SIZE         512                 /* Байт для eval */
#define PROGRESS_STEPS    5000                /* Прогресс каждые N шагов */

/* ============================================================================
 * Утилиты загрузки данных
 * ============================================================================ */

static int has_text_extension(const char *name) {
    size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".txt") == 0) return 1;
    if (len > 3 && strcmp(name + len - 3, ".md") == 0) return 1;
    return 0;
}

static size_t load_corpus_directory(const char *dir_path, uint8_t *buf,
                                     size_t buf_size, int *file_count) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    size_t total = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            size_t sub = load_corpus_directory(full_path, buf + total,
                                                buf_size - total, file_count);
            total += sub;
            if (total >= buf_size) break;
            continue;
        }

        if (!S_ISREG(st.st_mode)) continue;
        if (!has_text_extension(ent->d_name)) continue;
        if (st.st_size <= 0 || (size_t)st.st_size > buf_size - total) continue;

        FILE *f = fopen(full_path, "rb");
        if (!f) continue;

        size_t rd = fread(buf + total, 1, (size_t)st.st_size, f);
        fclose(f);
        total += rd;
        (*file_count)++;

        if (total < buf_size) buf[total++] = '\n';
        if (total >= buf_size - 1024) break;
    }

    closedir(d);
    return total;
}

/* ============================================================================
 * Быстрый eval: средний loss (bits/byte) на коротких окнах
 * ============================================================================ */

static double fast_eval(const KatModel *model, KatWorkspace *ws,
                         const uint8_t *data, size_t len) {
    if (len <= SEQ_LEN) return 99.0;

    double total_loss = 0.0;
    size_t count = 0;

    for (size_t pos = 0; pos + SEQ_LEN + 1 <= len; pos += SEQ_LEN) {
        kat_forward(model, ws, data + pos, SEQ_LEN);

        /* Cross-entropy целевого байта */
        uint8_t target = data[pos + SEQ_LEN];
        float p = ws->probs[target];
        if (p < 1e-10f) p = 1e-10f;
        total_loss += (double)(-log2f(p));
        count++;
    }

    return count > 0 ? total_loss / (double)count : 99.0;
}

/* Средняя уверенность top-1 + корректные предсказания */
static void fast_accuracy(const KatModel *model, KatWorkspace *ws,
                           const uint8_t *data, size_t len,
                           double *out_conf, double *out_acc) {
    if (len <= SEQ_LEN) { *out_conf = 0; *out_acc = 0; return; }

    double total_conf = 0.0;
    size_t correct = 0, count = 0;

    for (size_t pos = 0; pos + SEQ_LEN + 1 <= len; pos += SEQ_LEN) {
        kat_forward(model, ws, data + pos, SEQ_LEN);

        uint8_t target = data[pos + SEQ_LEN];

        /* Top-1 предсказание */
        uint8_t pred = 0;
        float best_p = ws->probs[0];
        for (int i = 1; i < 256; i++) {
            if (ws->probs[i] > best_p) {
                best_p = ws->probs[i];
                pred = (uint8_t)i;
            }
        }

        total_conf += (double)best_p;
        if (pred == target) correct++;
        count++;
    }

    *out_conf = count > 0 ? total_conf / (double)count : 0;
    *out_acc = count > 0 ? (double)correct / (double)count : 0;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    /* --- Параметры --- */
    const char *corpus_dir = "data/corpus";
    int epochs = 3;
    const char *model_path = NULL;
    float learning_rate = 0.005f;
    uint64_t seed = 42;
    int use_full_backprop = 0;  /* 0=fast (LM head only), 1=full backprop */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) {
            corpus_dir = argv[++i];
        } else if (strcmp(argv[i], "--epochs") == 0 && i + 1 < argc) {
            epochs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--lr") == 0 && i + 1 < argc) {
            learning_rate = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "full") == 0) use_full_backprop = 1;
            else use_full_backprop = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Использование: %s [параметры]\n"
                   "  --corpus DIR     Директория корпуса (default: data/corpus)\n"
                   "  --epochs N       Количество эпох (default: 3)\n"
                   "  --model PATH     Путь для сохранения модели\n"
                   "  --lr FLOAT       Скорость обучения (default: 0.005)\n"
                   "  --seed N         RNG seed (default: 42)\n"
                   "  --mode fast|full Режим: fast (LM head) или full (backprop)\n",
                   argv[0]);
            return 0;
        }
    }

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║       Kolibri KAT Transformer — Trainer              ║\n");
    printf("║       %s backprop на текстовом корпусе       ║\n",
           use_full_backprop ? "Full " : "Fast ");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    /* ========== Загрузка корпуса ========== */
    printf("📂 Загрузка корпуса из: %s\n", corpus_dir);

    uint8_t *corpus = malloc(MAX_CORPUS_SIZE);
    if (!corpus) { fprintf(stderr, "OOM\n"); return 1; }

    int file_count = 0;
    size_t corpus_len = load_corpus_directory(corpus_dir, corpus,
                                              MAX_CORPUS_SIZE, &file_count);
    if (corpus_len < SEQ_LEN + 1) {
        fprintf(stderr, "[ОШИБКА] Недостаточно данных\n");
        free(corpus);
        return 1;
    }

    /* Дополнительные источники */
    const char *extra_dirs[] = {"docs/wikipedia", "docs/ingested"};
    for (int w = 0; w < 2; w++) {
        int wfc = 0;
        size_t extra_len = load_corpus_directory(extra_dirs[w],
            corpus + corpus_len, MAX_CORPUS_SIZE - corpus_len, &wfc);
        if (extra_len > 0) {
            printf("   + %s: %d файлов, %zu байт\n", extra_dirs[w], wfc, extra_len);
            corpus_len += extra_len;
        }
    }

    printf("   Всего: %d файлов, %zu байт (%.2f МБ)\n",
           file_count, corpus_len, (double)corpus_len / (1024.0 * 1024.0));

    /* Train/eval split */
    size_t eval_len = EVAL_SIZE;
    if (eval_len > corpus_len / 10) eval_len = corpus_len / 10;
    size_t train_len = corpus_len - eval_len;
    uint8_t *train_data = corpus;
    uint8_t *eval_data  = corpus + train_len;

    size_t steps_per_epoch = (train_len - SEQ_LEN) / STEP_SIZE;

    printf("   Train: %zu байт (%zu шагов/эпоху) | Eval: %zu байт\n",
           train_len, steps_per_epoch, eval_len);

    /* ========== Создание модели ========== */
    printf("\n🧠 Создание KAT Transformer (small config)...\n");

    KatModel *model = kat_model_create(seed);
    if (!model) { fprintf(stderr, "Model create failed\n"); free(corpus); return 1; }

    KatWorkspace *ws = kat_workspace_create();
    if (!ws) { kat_model_destroy(model); free(corpus); return 1; }

    printf("   Параметров: %zu (%.1f KB)\n",
           kat_config_count_params(&model->cfg),
           (double)kat_config_count_params(&model->cfg) * 4.0 / 1024.0);
    printf("   Config: embed=%d heads=%d ff=%d layers=%d seq=%d\n",
           model->cfg.embed_dim, model->cfg.num_heads,
           model->cfg.ff_dim, model->cfg.num_layers, model->cfg.max_seq);
    printf("   LR=%.4f, SEQ_LEN=%d, STEP=%d, Epochs=%d\n\n",
           learning_rate, SEQ_LEN, STEP_SIZE, epochs);

    /* ========== Baseline ========== */
    double baseline_loss = fast_eval(model, ws, eval_data, eval_len);
    double baseline_conf, baseline_acc;
    fast_accuracy(model, ws, eval_data, eval_len, &baseline_conf, &baseline_acc);

    printf("📊 BASELINE (случайные веса):\n");
    printf("   Eval loss:      %.4f bits/byte\n", baseline_loss);
    printf("   Перплексия:     %.2f\n", pow(2.0, baseline_loss));
    printf("   Top-1 conf:     %.4f\n", baseline_conf);
    printf("   Accuracy:       %.2f%%\n\n", baseline_acc * 100.0);

    /* ========== Обучение ========== */
    printf("🚀 Начинаем обучение...\n\n");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float lr = learning_rate;

    for (int epoch = 0; epoch < epochs; epoch++) {
        double epoch_loss_sum = 0.0;
        size_t epoch_steps = 0;
        struct timespec ep0;
        clock_gettime(CLOCK_MONOTONIC, &ep0);

        for (size_t pos = 0; pos + SEQ_LEN + 1 <= train_len; pos += STEP_SIZE) {
            uint8_t target = train_data[pos + SEQ_LEN];

            float loss;
            if (use_full_backprop) {
                loss = kat_train_step_full(model, ws,
                                          train_data + pos, SEQ_LEN,
                                          target, lr);
            } else {
                loss = kat_train_step_fast(model, ws,
                                          train_data + pos, SEQ_LEN,
                                          target, lr);
            }

            epoch_loss_sum += (double)loss;
            epoch_steps++;

            /* Прогресс */
            if (epoch_steps % PROGRESS_STEPS == 0) {
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double elapsed = (double)(t1.tv_sec - ep0.tv_sec) +
                                 (double)(t1.tv_nsec - ep0.tv_nsec) / 1e9;

                printf("   [Epoch %d] %zuK/%zuK steps  loss=%.3f  "
                       "steps/s=%.0f  %.1fs\n",
                       epoch + 1, epoch_steps / 1000, steps_per_epoch / 1000,
                       epoch_loss_sum / (double)epoch_steps,
                       (double)epoch_steps / elapsed, elapsed);
            }
        }

        /* LR decay */
        lr *= 0.8f;

        /* Eval после эпохи */
        double eval_loss = fast_eval(model, ws, eval_data, eval_len);
        double eval_conf, eval_acc;
        fast_accuracy(model, ws, eval_data, eval_len, &eval_conf, &eval_acc);

        clock_gettime(CLOCK_MONOTONIC, &t1);
        double epoch_time = (double)(t1.tv_sec - ep0.tv_sec) +
                            (double)(t1.tv_nsec - ep0.tv_nsec) / 1e9;

        double improvement = (1.0 - eval_loss / baseline_loss) * 100.0;

        printf("\n   ═══ Epoch %d/%d завершена ═══\n", epoch + 1, epochs);
        printf("   Train loss:  %.4f bits/byte\n",
               epoch_loss_sum / (double)epoch_steps);
        printf("   Eval loss:   %.4f bits/byte (%.1f%% %s)\n",
               eval_loss, fabs(improvement),
               improvement > 0 ? "↓ лучше" : "↑ хуже");
        printf("   Перплексия:  %.2f (was %.2f)\n",
               pow(2.0, eval_loss), pow(2.0, baseline_loss));
        printf("   Top-1 conf:  %.4f (was %.4f)\n", eval_conf, baseline_conf);
        printf("   Accuracy:    %.2f%% (was %.2f%%)\n",
               eval_acc * 100.0, baseline_acc * 100.0);
        printf("   Время:       %.1f сек\n\n", epoch_time);
    }

    /* ========== Итоги ========== */
    double final_loss = fast_eval(model, ws, eval_data, eval_len);
    double final_conf, final_acc;
    fast_accuracy(model, ws, eval_data, eval_len, &final_conf, &final_acc);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double total_time = (double)(t1.tv_sec - t0.tv_sec) +
                        (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║                    ИТОГИ ОБУЧЕНИЯ                    ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Метрика          │  До       │  После    │ Δ       ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Loss (bits/byte) │ %8.4f  │ %8.4f  │ %+.4f  ║\n",
           baseline_loss, final_loss, final_loss - baseline_loss);
    printf("║  Перплексия       │ %8.2f  │ %8.2f  │ %+.2f  ║\n",
           pow(2.0, baseline_loss), pow(2.0, final_loss),
           pow(2.0, final_loss) - pow(2.0, baseline_loss));
    printf("║  Top-1 confidence │ %8.4f  │ %8.4f  │ %+.4f  ║\n",
           baseline_conf, final_conf, final_conf - baseline_conf);
    printf("║  Accuracy         │ %7.2f%%  │ %7.2f%%  │ %+.2f%%  ║\n",
           baseline_acc * 100.0, final_acc * 100.0,
           (final_acc - baseline_acc) * 100.0);
    printf("║  Время обучения   │          │ %7.1fs  │         ║\n", total_time);
    printf("╚══════════════════════════════════════════════════════╝\n");

    double improvement_pct = (1.0 - final_loss / baseline_loss) * 100.0;
    if (improvement_pct > 1.0) {
        printf("\n🎉 Модель стала умнее на %.1f%%!\n", improvement_pct);
    } else if (improvement_pct > 0.0) {
        printf("\n📈 Небольшое улучшение: %.2f%%\n", improvement_pct);
    } else {
        printf("\n⚠️  Loss не улучшился. Попробуйте другой LR или больше эпох.\n");
    }

    /* ========== Генерация текста ========== */
    printf("\n📝 Генерация текста:\n");

    /* Подаём контекст через мировую модель для генерации */
    KwmContext *wm = kwm_create(seed + 1);
    if (wm) {
        /* Копируем обученные веса LM head и эмбеддингов */
        KatModel *wm_backbone = (KatModel *)wm->backbone;
        memcpy(wm_backbone->lm_head, model->lm_head,
               (size_t)model->cfg.embed_dim * model->cfg.vocab_size * sizeof(float));
        memcpy(wm_backbone->embed.token_embed, model->embed.token_embed,
               (size_t)model->cfg.vocab_size * model->cfg.embed_dim * sizeof(float));

        const char *prompts[] = {
            "The quick ",
            "Machine le",
            "Data compr",
        };
        for (int pi = 0; pi < 3; pi++) {
            kwm_reset(wm);
            const char *prompt = prompts[pi];
            for (size_t c = 0; c < strlen(prompt); c++) {
                KwmPrediction pred;
                kwm_observe(wm, (uint8_t)prompt[c], &pred);
            }
            uint8_t gen[64];
            size_t gen_len = kwm_generate(wm, gen, 50, 0.7f);
            printf("   \"%s", prompt);
            for (size_t g = 0; g < gen_len; g++) {
                if (gen[g] >= 32 && gen[g] < 127) putchar(gen[g]);
                else if (gen[g] == '\n') printf("\\n");
                else printf(".");
            }
            printf("\"\n");
        }
        kwm_destroy(wm);
    }

    /* ========== Сохранение ========== */
    if (model_path) {
        printf("\n💾 Сохранение модели: %s\n", model_path);
        /* Первый вызов с NULL для определения размера */
        size_t ser_size = kat_serialize(model, NULL, 0);
        uint8_t *ser = malloc(ser_size);
        if (ser) {
            kat_serialize(model, ser, ser_size);
            FILE *f = fopen(model_path, "wb");
            if (f) {
                fwrite(ser, 1, ser_size, f);
                fclose(f);
                printf("   Сохранено: %zu байт (%.1f KB)\n", ser_size, (double)ser_size / 1024.0);
            }
            free(ser);
        }
    }

    /* Очистка */
    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    free(corpus);

    printf("\n✅ Готово!\n");
    return 0;
}
