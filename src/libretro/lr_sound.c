/*
 * 86Box libretro core: the sound backend.
 *
 * In place of OpenAL: every source 86Box feeds (sound card, music, wavetable,
 * CD audio, floppy and hard disk sounds, MIDI) hands its blocks to
 * givealbuffer_common() at its own rate, in float or int16 stereo. Each goes
 * into a ring of its own; retro_run takes exactly one frame's worth out of each,
 * resampled to the output rate, and mixes them. A source that is late gives
 * silence for the part it is missing, and one that runs ahead loses its oldest
 * samples once its ring holds more than a quarter of a second.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/midi.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include "lr_core.h"

#define RING_FRAMES 16384 /* per source, a power of two; > 0.25 s at 49716 Hz */
#define MAX_BACKLOG(rate) ((rate) / 4)

typedef struct {
    float    l[RING_FRAMES];
    float    r[RING_FRAMES];
    uint64_t write; /* frames written so far */
    uint64_t read;  /* whole frames consumed */
    double   frac;  /* position between read and read + 1 */
    int      active;
} source_ring_t;

static source_ring_t   rings[I_MAX];
static pthread_mutex_t ring_lock   = PTHREAD_MUTEX_INITIALIZER;
static int             initialized = 0;

static int
source_rate(int src)
{
    switch (src) {
        case I_NORMAL:
        case I_FDD:
        case I_HDD:
            return sound_sample_rate;
        case I_MIDI:
            return midi_freq;
        default:
            return (int) src_freqs[src];
    }
}

void
al_set_midi(const int freq, const int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

const char *
sound_get_output_devices(void)
{
    return NULL;
}

const char *
sound_get_input_devices(void)
{
    return NULL;
}

int
sound_get_device_sample_rate(UNUSED(const char *device_name))
{
    return FREQ_48000;
}

int
sound_get_device_supported_rates(UNUSED(const char *device_name), int *rates_out, int max_rates)
{
    static const int rates[] = { FREQ_44100, FREQ_48000 };
    int              n       = 0;

    for (int i = 0; (i < 2) && (n < max_rates); i++)
        rates_out[n++] = rates[i];
    return n;
}

/* No microphone in the core */
int
al_capture_get_rate(void)
{
    return 0;
}

int
al_capture_available(void)
{
    return 0;
}

void
al_capture_start(void)
{
}

void
al_capture_stop(void)
{
}

void
al_capture_get_data(UNUSED(int16_t *buf), size_t *len)
{
    if (len)
        *len = 0;
}

void
lr_sound_reset(void)
{
    pthread_mutex_lock(&ring_lock);
    memset(rings, 0, sizeof(rings));
    pthread_mutex_unlock(&ring_lock);
}

void
lr_sound_init(void)
{
    lr_sound_reset();
}

void
inital(void)
{
    lr_sound_reset();
    initialized = 1;
}

void
closeal(void)
{
    initialized = 0;
    lr_sound_reset();
}

void
givealbuffer_common(const void *buf, const uint8_t src, const int size)
{
    if (!initialized || (src >= I_MAX) || (size <= 0) || !buf)
        return;

    const int frames = size / 2;
    const int rate   = source_rate(src);

    pthread_mutex_lock(&ring_lock);
    source_ring_t *ring = &rings[src];
    ring->active        = 1;
    for (int i = 0; i < frames; i++) {
        const uint64_t pos = ring->write & (RING_FRAMES - 1);
        if (sound_is_float) {
            ring->l[pos] = ((const float *) buf)[i * 2];
            ring->r[pos] = ((const float *) buf)[i * 2 + 1];
        } else {
            ring->l[pos] = ((const int16_t *) buf)[i * 2] / 32768.0f;
            ring->r[pos] = ((const int16_t *) buf)[i * 2 + 1] / 32768.0f;
        }
        ring->write++;
    }
    /* Ahead by more than a quarter of a second: the oldest go */
    if ((rate > 0) && ((ring->write - ring->read) > (uint64_t) MAX_BACKLOG(rate))) {
        ring->read = ring->write - MAX_BACKLOG(rate);
        ring->frac = 0.0;
    }
    pthread_mutex_unlock(&ring_lock);
}

unsigned
lr_sound_mix(int16_t *out, unsigned frames, unsigned out_rate)
{
    static float mix[2 * 4096];

    if (frames > 4096)
        frames = 4096;
    memset(mix, 0, frames * 2 * sizeof(float));

    pthread_mutex_lock(&ring_lock);
    for (int src = 0; src < I_MAX; src++) {
        source_ring_t *ring = &rings[src];
        const int      rate = source_rate(src);
        if (!ring->active || (rate <= 0))
            continue;
        const double step = (double) rate / (double) out_rate;
        for (unsigned i = 0; i < frames; i++) {
            /* Linear between read and read + 1; silence past what was written */
            if ((ring->read + 1) < ring->write) {
                const uint64_t a = ring->read & (RING_FRAMES - 1);
                const uint64_t b = (ring->read + 1) & (RING_FRAMES - 1);
                const float    t = (float) ring->frac;
                mix[i * 2] += ring->l[a] + (ring->l[b] - ring->l[a]) * t;
                mix[i * 2 + 1] += ring->r[a] + (ring->r[b] - ring->r[a]) * t;
            }
            ring->frac += step;
            const uint64_t whole = (uint64_t) ring->frac;
            ring->frac -= (double) whole;
            ring->read += whole;
            if (ring->read > ring->write)
                ring->read = ring->write;
        }
    }
    pthread_mutex_unlock(&ring_lock);

    const float gain = sound_muted ? 0.0f : (float) pow(10.0, (double) sound_gain / 20.0);
    for (unsigned i = 0; i < frames * 2; i++) {
        float v = mix[i] * gain * 32767.0f;
        if (v > 32767.0f)
            v = 32767.0f;
        else if (v < -32768.0f)
            v = -32768.0f;
        out[i] = (int16_t) v;
    }
    return frames;
}
