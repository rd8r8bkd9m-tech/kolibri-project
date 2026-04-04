/*
 * Kolibri Audio Module — Фаза 3.2
 *
 * Audio → decimal genome через MFCC (Mel-Frequency Cepstral Coefficients)
 * MFCC → числовые паттерны → формулы для распознавания речи
 */

#ifndef KOLIBRI_AUDIO_H
#define KOLIBRI_AUDIO_H

#include "kolibri/formula.h"
#include "kolibri/decimal.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Параметры обработки */
#define KOLIBRI_AUDIO_SAMPLE_RATE 16000
#define KOLIBRI_AUDIO_FRAME_SIZE 512
#define KOLIBRI_AUDIO_HOP_SIZE 256
#define KOLIBRI_AUDIO_NUM_MEL_FILTERS 26
#define KOLIBRI_AUDIO_NUM_CEPSTRAL 13

/* Размер аудио генома */
#define KOLIBRI_AUDIO_GENOME_SIZE 2048

/* Аудио геном — decimal представление звука */
typedef struct {
    uint8_t digits[KOLIBRI_AUDIO_GENOME_SIZE];
    size_t length;
    uint32_t audio_hash;
    double duration_sec;
    uint16_t sample_rate;
} KolibriAudioGenome;

/* Фонемный паттерн */
typedef struct {
    char phoneme[16];
    double confidence;
    double start_time;
    double end_time;
    uint8_t mfcc[KOLIBRI_AUDIO_NUM_CEPSTRAL];
} KolibriPhonemePattern;

/* Аудио формула */
typedef struct {
    KolibriAudioGenome genome;
    KolibriFormula *formula;
    char transcription[512];
    double confidence;
} KolibriAudioFormula;

/* Аудио память */
typedef struct {
    KolibriAudioFormula *formulas;
    size_t count;
    size_t capacity;
    KolibriRng rng;
} KolibriAudioMemory;

/* ============================================================================
 * API
 * ============================================================================ */

int kolibri_audio_memory_init(KolibriAudioMemory *amem, uint64_t seed);
void kolibri_audio_memory_destroy(KolibriAudioMemory *amem);

/* Обработка аудио samples → genome */
int kolibri_audio_process_samples(KolibriAudioMemory *amem,
                                    const int16_t *samples,
                                    size_t num_samples,
                                    uint16_t sample_rate,
                                    KolibriAudioGenome *out_genome);

/* MFCC feature extraction */
int kolibri_audio_mfcc_features(const int16_t *frame,
                                 size_t frame_size,
                                 uint16_t sample_rate,
                                 double *out_mfcc,
                                 size_t num_mfcc);

/* Phoneme recognition */
int kolibri_audio_extract_phonemes(const int16_t *samples,
                                    size_t num_samples,
                                    uint16_t sample_rate,
                                    KolibriPhonemePattern *out_phonemes,
                                    size_t max_phonemes,
                                    size_t *out_count);

/* Audio hash */
uint32_t kolibri_audio_perceptual_hash(const int16_t *samples,
                                        size_t num_samples,
                                        uint16_t sample_rate);

/* Speech-to-text через формулы */
int kolibri_audio_speech_to_text(KolibriAudioMemory *amem,
                                  const KolibriAudioGenome *genome,
                                  char *out_transcription,
                                  size_t transcription_size,
                                  double *out_confidence);

/* Audio-language fusion */
int kolibri_audio_language_fusion(KolibriAudioMemory *amem,
                                   KolibriFormulaPool *formula_pool,
                                   const KolibriAudioGenome *audio_genome,
                                   const char *text_context,
                                   char *out_response, size_t response_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AUDIO_H */
