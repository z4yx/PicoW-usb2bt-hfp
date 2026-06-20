#include "sco_usb_bridge.h"
#include <string.h>
#include <stdio.h>

#include "classic/hfp.h"
#include "classic/hfp_msbc.h"
#include "btstack_run_loop.h"
#include "btstack_sco_transport.h"

// optional transport instance provided by the platform port (may be NULL)
__attribute__((weak)) const btstack_sco_transport_t * btstack_sco_transport = NULL;

#define MIC_RING_SAMPLES 2048
#define SPK_RING_SAMPLES 4096

static int16_t mic_ring[MIC_RING_SAMPLES];
static volatile uint32_t mic_head = 0;
static volatile uint32_t mic_tail = 0;

static int16_t spk_ring[SPK_RING_SAMPLES];
static volatile uint32_t spk_head = 0;
static volatile uint32_t spk_tail = 0;

void sco_usb_bridge_init(void){
    mic_head = mic_tail = 0;
    spk_head = spk_tail = 0;
}

static inline uint32_t ring_count(uint32_t head, uint32_t tail, uint32_t size){
    if (head >= tail) return head - tail;
    return size - (tail - head);
}

void sco_usb_bridge_push_spk_samples(const int16_t *stereo_src, uint16_t stereo_pair_count){
    // convert stereo to mono and push into spk_ring
    for (uint16_t i=0;i<stereo_pair_count;i++){
        int16_t l = stereo_src[2*i];
        int16_t r = stereo_src[2*i+1];
        int16_t mono = (int32_t)l/2 + (int32_t)r/2;
        uint32_t next = (spk_head + 1) & (SPK_RING_SAMPLES - 1);
        if (next == spk_tail) break; // full
        spk_ring[spk_head] = mono;
        spk_head = next;
    }
}

uint16_t sco_usb_bridge_pop_mic_samples(int16_t *dst, uint16_t max_samples){
    uint16_t popped = 0;
    while (popped < max_samples && mic_tail != mic_head){
        dst[popped++] = mic_ring[mic_tail];
        mic_tail = (mic_tail + 1) & (MIC_RING_SAMPLES - 1);
    }
    return popped;
}

void sco_usb_bridge_push_mic_samples_from_sco(const int16_t *src_mono, uint16_t samples){
    for (uint16_t i=0;i<samples;i++){
        uint32_t next = (mic_head + 1) & (MIC_RING_SAMPLES - 1);
        if (next == mic_tail) break; // full
        mic_ring[mic_head] = src_mono[i];
        mic_head = next;
    }
}

// --- forward declarations for local helpers ---
static void sco_usb_bridge_hfp_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void sco_transport_packet_handler(uint8_t packet_type, uint8_t *packet, uint16_t size);

// minimal HFP registration placeholder
void sco_usb_bridge_register_hfp(void){
    // Register AG packet handler to monitor audio connection events
    hfp_set_ag_callback(sco_usb_bridge_hfp_event);
}

// timer for periodic SCO TX (encode + send)
static btstack_timer_source_t sco_tx_timer;

static void sco_tx_timer_handler(btstack_timer_source_t * ts){
    (void)ts;
    // number of PCM samples per mSBC frame
    int samples_per_frame = hfp_msbc_num_audio_samples_per_frame();
    if (samples_per_frame <= 0) return;

    // temporary PCM buffer (mono)
    static int16_t pcm_buf[256];
    // ensure not to overflow
    if (samples_per_frame > (int)sizeof(pcm_buf)/sizeof(pcm_buf[0])) return;

    // pop samples from speaker ring (mono)
    uint16_t got = 0;
    for (int i=0;i<samples_per_frame;i++){
        if (spk_tail == spk_head) break;
        pcm_buf[got++] = spk_ring[spk_tail];
        spk_tail = (spk_tail + 1) & (SPK_RING_SAMPLES - 1);
    }

    if (got == 0) {
        // schedule next
        btstack_run_loop_set_timer(&sco_tx_timer, 8);
        btstack_run_loop_add_timer(&sco_tx_timer);
        return;
    }

    // pad with zeros if needed
    for (int i=got;i<samples_per_frame;i++) pcm_buf[i] = 0;

    // encode PCM -> mSBC stream
    hfp_msbc_encode_audio_frame(pcm_buf);

    // send available bytes via transport
    if (btstack_sco_transport && btstack_sco_transport->send_packet){
        uint8_t send_buf[256];
        while (hfp_msbc_num_bytes_in_stream() > 0){
            int to_read = hfp_msbc_num_bytes_in_stream();
            if (to_read > (int)sizeof(send_buf)) to_read = sizeof(send_buf);
            hfp_msbc_read_from_stream(send_buf, to_read);
            btstack_sco_transport->send_packet(send_buf, to_read);
        }
    }

    // schedule next
    btstack_run_loop_set_timer(&sco_tx_timer, 8);
    btstack_run_loop_add_timer(&sco_tx_timer);
}

static void sco_usb_bridge_hfp_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    (void)channel; (void)size;
    // HFP meta events are delivered as HCI_EVENT_PACKET with HCI_EVENT_HFP_META in packet[0]
    if (packet_type != HCI_EVENT_PACKET) return;
    if (packet[0] != HCI_EVENT_HFP_META) return;
    uint8_t subtype = packet[2];
    if (subtype == HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED){
        // start SCO transport handler and TX timer
        if (btstack_sco_transport && btstack_sco_transport->register_packet_handler){
            btstack_sco_transport->register_packet_handler(sco_transport_packet_handler);
        }
        // init mSBC encoder
        hfp_msbc_init();
        // schedule first TX
        btstack_run_loop_set_timer_handler(&sco_tx_timer, sco_tx_timer_handler);
        btstack_run_loop_set_timer_context(&sco_tx_timer, NULL);
        btstack_run_loop_set_timer(&sco_tx_timer, 8);
        btstack_run_loop_add_timer(&sco_tx_timer);
    } else if (subtype == HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED){
        // stop timer and unregister transport handler
        btstack_run_loop_remove_timer(&sco_tx_timer);
        if (btstack_sco_transport && btstack_sco_transport->register_packet_handler){
            btstack_sco_transport->register_packet_handler(NULL);
        }
        hfp_msbc_deinit();
    }
}

static hfp_h2_sync_t h2sync;

static bool h2_frame_callback(bool bad_frame, const uint8_t * frame_data, uint16_t frame_len){
    (void)bad_frame;
    // frame_data contains decoded PCM samples (if using platform decoder) or mSBC payloads
    // For now: if frame_len is even, interpret as 16-bit mono PCM and push to mic ring
    if (frame_len % 2 != 0) return false;
    uint16_t samples = frame_len / 2;
    const int16_t *pcm = (const int16_t *)frame_data;
    sco_usb_bridge_push_mic_samples_from_sco(pcm, samples);
    return true;
}

static void sco_transport_packet_handler(uint8_t packet_type, uint8_t *packet, uint16_t size){
    (void)packet_type;
    if (!packet || size == 0) return;
    // pass incoming SCO data into H2 sync processor; hfp_h2_sync will invoke our callback
    hfp_h2_sync_init(&h2sync, h2_frame_callback);
    hfp_h2_sync_process(&h2sync, false, packet, size);
}
