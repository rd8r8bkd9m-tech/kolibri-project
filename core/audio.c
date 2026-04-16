/*
 * Kolibri Audio Module Implementation — Фаза 3.2
 *
 * Audio → decimal genome через MFCC
 */

#include "kolibri/audio.h"
#include "kolibri/decimal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * MFCC Feature Extraction (упрощённая реализация)
 * ============================================================================ */

/* Pre-emphasis filter */
static void pre_emphasis(const int16_t *input, double *output, size_t len, double coeff) {
    output[0] = (double)input[0];
    for (size_t i = 1; i < len; i++) {
        output[i] = (double)input[i] - coeff * (double)input[i - 1];
    }
}

/* Hamming window */
static void hamming_window(double *frame, size_t len) {
    for (size_t i = 0; i < len; i++) {
        frame[i] *= 0.54 - 0.46 * cos(2.0 * M_PI * i / (len - 1));
    }
}

/* Magnitude spectrum (упрощённый DFT) */
static void magnitude_spectrum(const double *frame, size_t len, double *magnitude) {
    size_t half = len / 2;
    for (size_t k = 0; k < half; k++) {
        double real = 0.0, imag = 0.0;
        for (size_t n = 0; n < len; n++) {
            double angle = 2.0 * M_PI * k * n / len;
            real += frame[n] * cos(angle);
            imag -= frame[n] * sin(angle);
        }
        magnitude[k] = sqrt(real * real + imag * imag);
    }
}

/* Mel filterbank */
static double mel_scale(double freq) {
    return 2595.0 * log10(1.0 + freq / 700.0);
}

static double inv_mel_scale(double mel) {
    return 700.0 * (pow(10.0, mel / 2595.0) - 1.0);
}

static void mel_filterbank(const double *magnitude, size_t len,
                            uint16_t sample_rate,
                            double *mel_energies, size_t num_filters) {
    double min_mel = mel_scale(300.0);
    double max_mel = mel_scale((double)sample_rate / 2.0);

    /* Mel-spaced center frequencies */
    double *mel_points = (double *)calloc(num_filters + 2, sizeof(double));
    if (!mel_points) return;

    for (size_t i = 0; i < num_filters + 2; i++) {
        double mel = min_mel + (max_mel - min_mel) * i / (num_filters + 1);
        mel_points[i] = inv_mel_scale(mel);
    }

    /* Convert to FFT bins */
    double *bin_points = (double *)calloc(num_filters + 2, sizeof(double));
    if (!bin_points) { free(mel_points); return; }

    for (size_t i = 0; i < num_filters + 2; i++) {
        bin_points[i] = floor((len + 1) * mel_points[i] / (double)sample_rate);
    }

    /* Apply triangular filters */
    memset(mel_energies, 0, num_filters * sizeof(double));
    for (size_t m = 1; m <= num_filters; m++) {
        for (size_t k = (size_t)bin_points[m - 1]; k <= (size_t)bin_points[m + 1] && k < len; k++) {
            double weight;
            if (k <= (size_t)bin_points[m]) {
                weight = (k - bin_points[m - 1]) / (bin_points[m] - bin_points[m - 1] + 1e-10);
            } else {
                weight = (bin_points[m + 1] - k) / (bin_points[m + 1] - bin_points[m] + 1e-10);
            }
            mel_energies[m - 1] += magnitude[k] * weight;
        }
    }

    free(mel_points);
    free(bin_points);
}

/* DCT for cepstral coefficients */
static void dct_type_ii(const double *input, double *output, size_t len) {
    for (size_t k = 0; k < len; k++) {
        double sum = 0.0;
        for (size_t n = 0; n < len; n++) {
            sum += input[n] * cos(M_PI * k * (2.0 * n + 1.0) / (2.0 * len));
        }
        output[k] = sum;
    }
}

int kolibri_audio_mfcc_features(const int16_t *frame,
                                 size_t frame_size,
                                 uint16_t sample_rate,
                                 double *out_mfcc,
                                 size_t num_mfcc) {
    if (!frame || !out_mfcc || frame_size == 0) return -1;

    double *pre = (double *)calloc(frame_size, sizeof(double));
    double *magnitude = (double *)calloc(frame_size / 2, sizeof(double));
    double *mel_energies = (double *)calloc(KOLIBRI_AUDIO_NUM_MEL_FILTERS, sizeof(double));
    double *log_mel = (double *)calloc(KOLIBRI_AUDIO_NUM_MEL_FILTERS, sizeof(double));
    double *cepstral = (double *)calloc(KOLIBRI_AUDIO_NUM_MEL_FILTERS, sizeof(double));

    if (!pre || !magnitude || !mel_energies || !log_mel || !cepstral) {
        free(pre); free(magnitude); free(mel_energies); free(log_mel); free(cepstral);
        return -1;
    }

    /* 1. Pre-emphasis */
    pre_emphasis(frame, pre, frame_size, 0.97);

    /* 2. Windowing */
    hamming_window(pre, frame_size);

    /* 3. Magnitude spectrum */
    magnitude_spectrum(pre, frame_size, magnitude);

    /* 4. Mel filterbank */
    mel_filterbank(magnitude, frame_size / 2, sample_rate,
                   mel_energies, KOLIBRI_AUDIO_NUM_MEL_FILTERS);

    /* 5. Log energy */
    for (size_t i = 0; i < KOLIBRI_AUDIO_NUM_MEL_FILTERS; i++) {
        log_mel[i] = log(mel_energies[i] + 1e-10);
    }

    /* 6. DCT */
    dct_type_ii(log_mel, cepstral, KOLIBRI_AUDIO_NUM_MEL_FILTERS);

    /* Take first num_mfcc coefficients */
    for (size_t i = 0; i < num_mfcc; i++) {
        out_mfcc[i] = cepstral[i];
    }

    free(pre);
    free(magnitude);
    free(mel_energies);
    free(log_mel);
    free(cepstral);

    return 0;
}

/* ============================================================================
 * Perceptual Audio Hash
 * ============================================================================ */

uint32_t kolibri_audio_perceptual_hash(const int16_t *samples,
                                        size_t num_samples,
                                        uint16_t sample_rate) {
    if (!samples || num_samples == 0) return 0;

    /* Упрощённый hash: energy + spectral centroid + zero-crossing rate */
    double energy = 0.0;
    double zcr = 0.0;
    double spectral_centroid_num = 0.0, spectral_centroid_den = 0.0;

    for (size_t i = 0; i < num_samples; i++) {
        double s = (double)samples[i] / 32768.0;
        energy += s * s;

        if (i > 0) {
            if ((samples[i] >= 0) != (samples[i - 1] >= 0)) {
                zcr++;
            }
        }
    }

    energy /= (double)num_samples;
    zcr /= (double)num_samples;

    /* Simple spectral centroid */
    size_t frame_size = 512;
    if (num_samples >= frame_size) {
        double *frame = (double *)calloc(frame_size, sizeof(double));
        double *mag = (double *)calloc(frame_size / 2, sizeof(double));
        if (frame && mag) {
            for (size_t i = 0; i < frame_size; i++) {
                frame[i] = (double)samples[i] / 32768.0;
            }
            hamming_window(frame, frame_size);
            magnitude_spectrum(frame, frame_size, mag);

            for (size_t k = 0; k < frame_size / 2; k++) {
                spectral_centroid_num += (double)k * mag[k];
                spectral_centroid_den += mag[k];
            }

            free(frame);
            free(mag);
        }
    }

    double spectral_centroid = spectral_centroid_den > 0 ?
                               spectral_centroid_num / spectral_centroid_den : 0.0;

    /* Combine into hash */
    uint32_t hash = 0;
    hash ^= (uint32_t)(energy * 1000000.0);
    hash = (hash << 7) | (hash >> 25);
    hash ^= (uint32_t)(zcr * 1000000.0);
    hash = (hash << 11) | (hash >> 21);
    hash ^= (uint32_t)(spectral_centroid * 100.0);

    return hash;
}

/* ============================================================================
 * Audio Processing
 * ============================================================================ */

int kolibri_audio_process_samples(KolibriAudioMemory *amem,
                                    const int16_t *samples,
                                    size_t num_samples,
                                    uint16_t sample_rate,
                                    KolibriAudioGenome *out_genome) {
    if (!samples || !out_genome || num_samples == 0) return -1;

    memset(out_genome, 0, sizeof(*out_genome));
    out_genome->duration_sec = (double)num_samples / (double)sample_rate;
    out_genome->sample_rate = sample_rate;
    out_genome->audio_hash = kolibri_audio_perceptual_hash(samples, num_samples, sample_rate);

    /* MFCC features по фреймам */
    size_t genome_pos = 0;
    size_t hop = KOLIBRI_AUDIO_HOP_SIZE;
    size_t frame_size = KOLIBRI_AUDIO_FRAME_SIZE;
    double mfcc[KOLIBRI_AUDIO_NUM_CEPSTRAL];

    for (size_t start = 0; start + frame_size <= num_samples &&
         genome_pos + KOLIBRI_AUDIO_NUM_CEPSTRAL <= KOLIBRI_AUDIO_GENOME_SIZE;
         start += hop) {

        if (kolibri_audio_mfcc_features(&samples[start], frame_size, sample_rate,
                                         mfcc, KOLIBRI_AUDIO_NUM_CEPSTRAL) == 0) {
            for (size_t i = 0; i < KOLIBRI_AUDIO_NUM_CEPSTRAL; i++) {
                /* Конвертируем MFCC в decimal digits [0-9] */
                int digit = (int)(fabs(mfcc[i]) * 10.0) % 10;
                if (digit < 0) digit = 0;
                if (digit > 9) digit = 9;
                out_genome->digits[genome_pos++] = (uint8_t)digit;
            }
        }
    }

    out_genome->length = genome_pos;
    return 0;
}

/* ============================================================================
 * Audio Memory
 * ============================================================================ */

int kolibri_audio_memory_init(KolibriAudioMemory *amem, uint64_t seed) {
    if (!amem) return -1;

    memset(amem, 0, sizeof(*amem));
    amem->capacity = 512;
    amem->formulas = (KolibriAudioFormula *)calloc(amem->capacity,
                                                    sizeof(KolibriAudioFormula));
    if (!amem->formulas) return -1;

    k_rng_seed(&amem->rng, seed);
    return 0;
}

void kolibri_audio_memory_destroy(KolibriAudioMemory *amem) {
    if (!amem) return;
    if (amem->formulas) {
        free(amem->formulas);
        amem->formulas = NULL;
    }
    amem->count = 0;
    amem->capacity = 0;
}

/* ============================================================================
 * Speech-to-Text через формулы
 * ============================================================================ */

int kolibri_audio_speech_to_text(KolibriAudioMemory *amem,
                                  const KolibriAudioGenome *genome,
                                  char *out_transcription,
                                  size_t transcription_size,
                                  double *out_confidence) {
    if (!amem || !genome || !out_transcription) return -1;

    /* Ищем похожее аудио */
    double best_sim = 0.0;
    const KolibriAudioFormula *best = NULL;

    for (size_t i = 0; i < amem->count; i++) {
        /* Genome similarity */
        size_t min_len = genome->length < amem->formulas[i].genome.length ?
                         genome->length : amem->formulas[i].genome.length;
        if (min_len == 0) continue;

        double sim = 0.0;
        for (size_t j = 0; j < min_len; j++) {
            int diff = abs((int)genome->digits[j] - (int)amem->formulas[i].genome.digits[j]);
            sim += (9.0 - (double)diff) / 9.0;
        }
        sim /= (double)min_len;

        /* Hash bonus */
        if (genome->audio_hash == amem->formulas[i].genome.audio_hash) {
            sim += 0.2;
        }

        if (sim > best_sim) {
            best_sim = sim;
            best = &amem->formulas[i];
        }
    }

    if (best && best_sim > 0.5) {
        strncpy(out_transcription, best->transcription, transcription_size - 1);
        out_transcription[transcription_size - 1] = '\0';
        if (out_confidence) *out_confidence = best_sim;
        return 0;
    }

    strncpy(out_transcription, "[не распознано]", transcription_size - 1);
    out_transcription[transcription_size - 1] = '\0';
    if (out_confidence) *out_confidence = best_sim;
    return -1;
}

/* ============================================================================
 * Audio-Language Fusion
 * ============================================================================ */

int kolibri_audio_language_fusion(KolibriAudioMemory *amem,
                                   KolibriFormulaPool *formula_pool,
                                   const KolibriAudioGenome *audio_genome,
                                   const char *text_context,
                                   char *out_response, size_t response_size) {
    if (!amem || !formula_pool || !audio_genome || !out_response) return -1;

    /* Шаг 1: Speech-to-text */
    char transcription[512] = {0};
    double stt_confidence = 0.0;
    kolibri_audio_speech_to_text(amem, audio_genome, transcription,
                                  sizeof(transcription), &stt_confidence);

    /* Шаг 2: Ищем формулы по транскрипции */
    const KolibriFormula *best_formula = NULL;
    double best_fitness = -1.0;

    if (transcription[0] != '\0') {
        for (size_t i = 0; i < formula_pool->count; i++) {
            for (size_t j = 0; j < formula_pool->association_count; j++) {
                if (strstr(formula_pool->associations[j].question, transcription) != NULL) {
                    if (formula_pool->formulas[i].fitness > best_fitness) {
                        best_fitness = formula_pool->formulas[i].fitness;
                        best_formula = &formula_pool->formulas[i];
                    }
                    break;
                }
            }
        }
    }

    /* Шаг 3: Формируем ответ */
    if (best_formula && stt_confidence > 0.5) {
        snprintf(out_response, response_size,
                 "Распознано: \"%s\" (уверенность: %.0f%%). "
                 "Ответ: %s",
                 transcription, stt_confidence * 100.0,
                 best_formula->associations[0].answer);
    } else if (stt_confidence > 0.3) {
        snprintf(out_response, response_size,
                 "Распознано: \"%s\" (уверенность: %.0f%%). "
                 "Ответ не найден.",
                 transcription, stt_confidence * 100.0);
    } else {
        snprintf(out_response, response_size,
                 "Не удалось распознать речь.");
    }

    return 0;
}
