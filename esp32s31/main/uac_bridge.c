#include "uac_bridge.h"

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_hf_ag_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "usb_device_uac.h"

#include "resampler.h"

static const char *TAG = "uac_bridge";

#define UAC_RATE            48000
#define STREAM_BYTES        (16 * 1024)
#define MAX_UAC_FRAMES      1024
#define MAX_HFP_SAMPLES     1024
#define MAX_UP_SAMPLES      (MAX_HFP_SAMPLES * 6)

static StreamBufferHandle_t s_spk_stream;   /* 48 kHz mono, USB -> HFP */
static StreamBufferHandle_t s_mic_stream;   /* 48 kHz mono, HFP -> USB */
static resampler_t s_down;
static resampler_t s_up;
static volatile int s_hfp_rate;
static volatile bool s_active;
static volatile uint32_t s_spk_drops;
static volatile uint32_t s_mic_drops;

static void uac_bridge_reset(void)
{
    s_active = false;
    s_hfp_rate = 0;
    resampler_reset(&s_down, UAC_RATE, UAC_RATE);
    resampler_reset(&s_up, UAC_RATE, UAC_RATE);
    if (s_spk_stream) {
        xStreamBufferReset(s_spk_stream);
    }
    if (s_mic_stream) {
        xStreamBufferReset(s_mic_stream);
    }
}

void uac_bridge_set_audio_state(int state)
{
    switch (state) {
        case ESP_HF_AUDIO_STATE_CONNECTED:
            s_hfp_rate = 8000;
            break;
        case ESP_HF_AUDIO_STATE_CONNECTED_MSBC:
            s_hfp_rate = 16000;
            break;
        case ESP_HF_AUDIO_STATE_CONNECTED_LC3:
            s_hfp_rate = 32000;
            break;
        default:
            ESP_LOGI(TAG, "HFP audio disconnected");
            uac_bridge_reset();
            return;
    }

    if (s_spk_stream) {
        xStreamBufferReset(s_spk_stream);
    }
    if (s_mic_stream) {
        xStreamBufferReset(s_mic_stream);
    }
    resampler_reset(&s_down, UAC_RATE, (uint32_t)s_hfp_rate);
    resampler_reset(&s_up, (uint32_t)s_hfp_rate, UAC_RATE);
    s_active = true;
    ESP_LOGI(TAG, "HFP audio connected: %d Hz", s_hfp_rate);
}

/* TinyUSB speaker callback: 48 kHz, 2 ch, S16. */
static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx)
{
    (void)cb_ctx;
    size_t frames = len / 4;
    int16_t *stereo = (int16_t *)buf;
    static int16_t mono[MAX_UAC_FRAMES];

    if (frames > MAX_UAC_FRAMES) {
        frames = MAX_UAC_FRAMES;
    }
    for (size_t i = 0; i < frames; i++) {
        mono[i] = (int16_t)(((int32_t)stereo[2 * i] + stereo[2 * i + 1]) / 2);
    }
    if (s_spk_stream) {
        size_t sent = xStreamBufferSend(s_spk_stream, mono, frames * sizeof(int16_t), 0);
        if (sent != frames * sizeof(int16_t)) {
            s_spk_drops++;
        }
    }
    return ESP_OK;
}

/* TinyUSB mic callback: 48 kHz, 1 ch, S16. */
static esp_err_t uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx)
{
    (void)cb_ctx;
    size_t got = 0;
    if (s_mic_stream) {
        got = xStreamBufferReceive(s_mic_stream, buf, len, 0);
    }
    if (got < len) {
        memset(buf + got, 0, len - got);
        s_mic_drops++;
    }
    *bytes_read = len;
    return ESP_OK;
}

static void uac_set_volume_cb(uint32_t volume, void *cb_ctx)
{
    (void)cb_ctx;
    ESP_LOGI(TAG, "USB host volume: %" PRIu32, volume);
}

static void uac_set_mute_cb(uint32_t mute, void *cb_ctx)
{
    (void)cb_ctx;
    ESP_LOGI(TAG, "USB host mute: %" PRIu32, mute);
}

/* HFP -> USB: up-sample 8/16 kHz mono to 48 kHz mono. */
void uac_bridge_hfp_incoming(const uint8_t *buf, uint32_t sz)
{
    static int16_t out[MAX_UP_SAMPLES];

    if (!s_active || s_hfp_rate == 0 || s_hfp_rate == 32000 || !s_mic_stream) {
        return;
    }
    size_t in_samples = sz / sizeof(int16_t);
    if (in_samples > MAX_HFP_SAMPLES) {
        in_samples = MAX_HFP_SAMPLES;
    }
    size_t n = resampler_upsample(&s_up, (const int16_t *)buf, in_samples, out,
                                  sizeof(out) / sizeof(out[0]));
    if (n) {
        xStreamBufferSend(s_mic_stream, out, n * sizeof(int16_t), 0);
    }
}

/* HFP <- USB: down-sample 48 kHz mono to 8/16 kHz mono. */
uint32_t uac_bridge_hfp_outgoing(uint8_t *buf, uint32_t sz)
{
    static int16_t in[MAX_HFP_SAMPLES * 6];

    if (!s_active || s_hfp_rate == 0 || s_hfp_rate == 32000 || !s_spk_stream) {
        memset(buf, 0, sz);
        return sz;
    }

    uint32_t ratio = UAC_RATE / (uint32_t)s_hfp_rate;   /* 6 (8k) or 3 (16k) */
    size_t out_samples = sz / sizeof(int16_t);
    size_t need = out_samples * ratio;
    if (need > sizeof(in) / sizeof(in[0])) {
        need = sizeof(in) / sizeof(in[0]);
    }

    size_t got = xStreamBufferReceive(s_spk_stream, in, need * sizeof(int16_t), 0) /
                 sizeof(int16_t);
    if (got < need) {
        memset(in + got, 0, (need - got) * sizeof(int16_t));
        s_spk_drops++;
    }

    size_t n = resampler_downsample(&s_down, in, need, (int16_t *)buf, out_samples);
    if (n < out_samples) {
        memset((int16_t *)buf + n, 0, (out_samples - n) * sizeof(int16_t));
    }
    return sz;
}

void uac_bridge_init(void)
{
    s_spk_stream = xStreamBufferCreate(STREAM_BYTES, 1);
    s_mic_stream = xStreamBufferCreate(STREAM_BYTES, 1);
    configASSERT(s_spk_stream && s_mic_stream);

    uac_bridge_reset();

    uac_device_config_t config = {
        .output_cb = uac_output_cb,
        .input_cb = uac_input_cb,
        .set_volume_cb = uac_set_volume_cb,
        .set_mute_cb = uac_set_mute_cb,
        .cb_ctx = NULL,
    };
    ESP_ERROR_CHECK(uac_device_init(&config));
    ESP_LOGI(TAG, "USB UAC device started (%d Hz, 2ch out / 1ch in)", UAC_RATE);
}
