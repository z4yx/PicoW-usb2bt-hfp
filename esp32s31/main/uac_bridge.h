/*
 * USB UAC <-> HFP SCO audio bridge for ESP32-S31.
 *
 * USB side: 48 kHz, 16-bit.  Speaker: stereo -> mono.  Mic: mono.
 * HFP side: SCO over HCI, PCM blocks of 7.5 ms:
 *   CVSD  : 8 kHz  mono (120 bytes / block)
 *   mSBC  : 16 kHz mono (240 bytes / block)
 *
 * The HFP stack calls the data callbacks from the Bluetooth task; the UAC
 * component calls its callbacks from the TinyUSB speaker/mic tasks.  FreeRTOS
 * stream buffers decouple the two clock domains.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void uac_bridge_init(bool enable_uac);

/* Called by the HFP AG stack.  `sz` is the PCM block size in bytes. */
uint32_t uac_bridge_hfp_outgoing(uint8_t *buf, uint32_t sz);
void uac_bridge_hfp_incoming(const uint8_t *buf, uint32_t sz);

/* Feed ESP_HF_AUDIO_STATE_* from the HFP AG callback. */
void uac_bridge_set_audio_state(int state);

#ifdef __cplusplus
}
#endif
