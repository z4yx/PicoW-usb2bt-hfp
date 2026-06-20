#ifndef SCO_USB_BRIDGE_H
#define SCO_USB_BRIDGE_H

#include <stdint.h>

// Initialize the bridge
void sco_usb_bridge_init(void);

// push stereo samples from USB host (interleaved int16_t pairs)
void sco_usb_bridge_push_spk_samples(const int16_t *stereo_src, uint16_t stereo_pair_count);

// pop mono samples for microphone IN (returns number of samples popped)
uint16_t sco_usb_bridge_pop_mic_samples(int16_t *dst, uint16_t max_samples);

// push mic samples (from SCO RX) to be sent to host
void sco_usb_bridge_push_mic_samples_from_sco(const int16_t *src_mono, uint16_t samples);

// HFP event handler registration (optional)
void sco_usb_bridge_register_hfp(void);

#endif
