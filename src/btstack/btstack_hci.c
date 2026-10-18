//
// Created by Sean on 8/10/23.
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "btstack_hci.h"
#include "btstack_avdtp_source.h"
#include "../pico_w_led.h"
#include "sco_usb_bridge.h"


#define A2DP_SOURCE_DEMO_INQUIRY_DURATION_1280MS 12

static btstack_packet_callback_registration_t hci_event_callback_registration;

static char device_addr_string[] = "00:00:00:00:00:00";
static bd_addr_t device_addr;
static bd_addr_t device_addr_list[2];

static bool scan_active = true;


const char * get_device_addr_string(){
    return device_addr_string;
}

bd_addr_t * get_device_addr(){
    return &device_addr;
}

bd_addr_t * get_device_addr_from_list(uint8_t i){
    return &device_addr_list[i];
}

void gap_start_scanning(void){
    printf("Start scanning...\n");
    gap_inquiry_start(A2DP_SOURCE_DEMO_INQUIRY_DURATION_1280MS);
    scan_active = true;
    set_led_mode_off();
    set_led_mode_pairing();
}


static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;
    uint8_t status;
    UNUSED(status);

    bd_addr_t address;
    uint32_t cod;

    // Service Class: Rendering | Audio, Major Device Class: Audio
    const uint32_t bluetooth_speaker_cod = 0x200000 | 0x040000 | 0x000400;

    switch (hci_event_packet_get_type(packet)){
        case  BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            {
                bd_addr_t local_addr;
                gap_local_bd_addr(local_addr);
                printf("BT local addr %s\n", bd_addr_to_str(local_addr));
            }
            if ( strcmp(device_addr_string , "00:00:00:00:00:00") == 0 ){
                gap_start_scanning();
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            printf("Pin code request - using '0000'\n");
            hci_event_pin_code_request_get_bd_addr(packet, address);
            gap_pin_code_response(address, "0000");
            break;
        case HCI_EVENT_IO_CAPABILITY_REQUEST:
            printf("HCI: IO capability request\n");
            break;
        case HCI_EVENT_IO_CAPABILITY_RESPONSE:
            printf("HCI: IO capability response\n");
            break;
        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            printf("HCI: user confirmation request, accepting\n");
            hci_event_user_confirmation_request_get_bd_addr(packet, address);
            gap_ssp_confirmation_response(address);
            break;
        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
            printf("HCI: simple pairing complete status=0x%02x\n",
                   hci_event_simple_pairing_complete_get_status(packet));
            break;
        case HCI_EVENT_AUTHENTICATION_COMPLETE:
            printf("HCI: authentication complete status=0x%02x\n",
                   hci_event_authentication_complete_get_status(packet));
            break;
        case HCI_EVENT_COMMAND_STATUS:
            printf("HCI: cmd status opcode=0x%04x status=0x%02x\n",
                   hci_event_command_status_get_command_opcode(packet),
                   hci_event_command_status_get_status(packet));
            break;
        case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE:
            printf("HCI: sync complete status=0x%02x handle=0x%04x link_type=%u\n",
                   hci_event_synchronous_connection_complete_get_status(packet),
                   hci_event_synchronous_connection_complete_get_handle(packet),
                   hci_event_synchronous_connection_complete_get_link_type(packet));
            break;
        case HCI_EVENT_LINK_KEY_REQUEST:
            printf("HCI: link key request\n");
            break;
        case HCI_EVENT_LINK_KEY_NOTIFICATION:
            printf("HCI: link key notification\n");
            break;
        case GAP_EVENT_INQUIRY_RESULT:
            gap_event_inquiry_result_get_bd_addr(packet, address);
            // print info
            printf("Device found: %s ",  bd_addr_to_str(address));
            cod = gap_event_inquiry_result_get_class_of_device(packet);
            printf("with COD: %06" PRIx32, cod);
            if (gap_event_inquiry_result_get_rssi_available(packet)){
                printf(", rssi %d dBm", (int8_t) gap_event_inquiry_result_get_rssi(packet));
            }
            if (gap_event_inquiry_result_get_name_available(packet)){
                char name_buffer[240];
                int name_len = gap_event_inquiry_result_get_name_len(packet);
                memcpy(name_buffer, gap_event_inquiry_result_get_name(packet), name_len);
                name_buffer[name_len] = 0;
                printf(", name '%s'", name_buffer);
            }
            printf("\n");
            if ((cod & bluetooth_speaker_cod) == bluetooth_speaker_cod){
                memcpy(device_addr, address, 6);
                printf("Bluetooth speaker detected, trying to connect to %s...\n", bd_addr_to_str(device_addr));
                scan_active = false;
                gap_inquiry_stop();

                uint8_t currect_slot = read_uint8_last_flash();

                if (currect_slot == 0x1){
                    write_slot1_mac(device_addr);
                }
    
                if (currect_slot == 0x2){
                    write_slot2_mac(device_addr);
                }
                avdtp_source_establish_stream();
            }
            break;
        case GAP_EVENT_INQUIRY_COMPLETE:
            if (scan_active){
                printf("No Bluetooth speakers found, scanning again...\n");
                gap_inquiry_start(A2DP_SOURCE_DEMO_INQUIRY_DURATION_1280MS);
            }
            break;
        default:
            break;
    }
}


void get_link_keys(void){
    bd_addr_t  addr;
    link_key_t link_key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;
    const char * addr_str;

    int ok = gap_link_key_iterator_init(&it);
    if (!ok) {
        printf("Link key iterator not implemented\n");
        return;
    }
    printf("Stored First link key: \n");

    if (gap_link_key_iterator_get_next(&it, addr, link_key, &type)){
        addr_str = bd_addr_to_str(addr);
        printf("%s - type %u, key: ", addr_str, (int) type);
        printf_hexdump(link_key, 16);
        strncpy(device_addr_string, addr_str, sizeof(device_addr_string) - 1);
        sscanf_bd_addr(device_addr_string, device_addr_list[0]);
    }

    //sscanf_bd_addr(device_addr_string, device_addr);

    if (gap_link_key_iterator_get_next(&it, addr, link_key, &type)){
        addr_str = bd_addr_to_str(addr);
        printf("%s - type %u, key: ", addr_str, (int) type);
        printf_hexdump(link_key, 16);
        strncpy(device_addr_string, addr_str, sizeof(device_addr_string) - 1);
        sscanf_bd_addr(device_addr_string, device_addr_list[1]);
    }

    printf(".\n");
    gap_link_key_iterator_done(&it);

    uint8_t currect_slot = read_uint8_last_flash();

    printf("currect slot iss %d\n", currect_slot);

    if(currect_slot == 0x1){
        set_led_mode_double_blink();
        bd_addr_t  addr_flash_slot1;
        read_slot1_mac(addr_flash_slot1);
        printf("cur slot1 flash addr is %s\n ", bd_addr_to_str(addr_flash_slot1));
        memcpy(device_addr, addr_flash_slot1, sizeof(bd_addr_t)); 
    }

    if(currect_slot == 0x2){
        //printf("currect slot is 2\n");
        set_led_mode_triple_blink();
        bd_addr_t  addr_flash_slot2;
        read_slot2_mac(addr_flash_slot2);
        printf("cur slot2 flash addr is %s\n ", bd_addr_to_str(addr_flash_slot2));
        memcpy(device_addr, addr_flash_slot2, sizeof(bd_addr_t)); 
    }
    
}


// ---------------------------------------------------------------------------
// Minimal HFP AG bring-up for Bluetooth debugging.
// Exposes an HFP AG SDP record (CVSD + mSBC), logs SLC/audio events and makes
// the dongle discoverable/connectable so an HF (e.g. Bumble) can connect.
// Audio/SCO bridging is intentionally not wired here yet.
// ---------------------------------------------------------------------------
static uint8_t hfp_ag_sdp_buffer[256];
static hci_con_handle_t hfp_acl_handle = HCI_CON_HANDLE_INVALID;
static btstack_timer_source_t hfp_call_timer;
static void hfp_call_timer_handler(btstack_timer_source_t *ts);

static const hfp_ag_indicator_t hfp_ag_indicators[] = {
    // index, name, min, max, status, mandatory, enabled, status changed
    {1, "service",   0, 1, 1, 0, 0, 0},
    {2, "call",      0, 1, 0, 1, 1, 0},
    {3, "callsetup", 0, 3, 0, 1, 1, 0},
    {4, "battchg",   0, 5, 3, 0, 0, 0},
    {5, "signal",    0, 5, 5, 0, 1, 0},
    {6, "roam",      0, 1, 0, 0, 1, 0},
    {7, "callheld",  0, 2, 0, 1, 1, 0},
};

static void hfp_ag_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_HFP_META) return;

    uint8_t subevent = packet[2];
    switch (subevent){
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED: {
            hfp_acl_handle = hfp_subevent_service_level_connection_established_get_acl_handle(packet);
            printf("HFP: SLC established, status=0x%02x acl=0x%04x\n",
                   hfp_subevent_service_level_connection_established_get_status(packet),
                   hfp_acl_handle);
            // Debug: simulate an incoming call so that the HF can answer and
            // the AG can then establish the audio (e)SCO connection.
            printf("HFP: simulating incoming call\n");
            hfp_ag_incoming_call();
            btstack_run_loop_set_timer_handler(&hfp_call_timer, hfp_call_timer_handler);
            btstack_run_loop_set_timer(&hfp_call_timer, 1000);
            btstack_run_loop_add_timer(&hfp_call_timer);
            break;
        }
        case HFP_SUBEVENT_CALL_ANSWERED:
            printf("HFP: call answered, establishing audio\n");
            if (hfp_acl_handle != HCI_CON_HANDLE_INVALID) {
                uint8_t audio_status = hfp_ag_establish_audio_connection(hfp_acl_handle);
                printf("HFP: establish audio status=0x%02x\n", audio_status);
            }
            break;
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
            printf("HFP: SLC released\n");
            break;
        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
            printf("HFP: audio established status=0x%02x sco=0x%04x codec=0x%02x\n",
                   hfp_subevent_audio_connection_established_get_status(packet),
                   hfp_subevent_audio_connection_established_get_sco_handle(packet),
                   hfp_subevent_audio_connection_established_get_negotiated_codec(packet));
            break;
        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
            printf("HFP: audio released\n");
            break;
        default:
            printf("HFP: event 0x%02x\n", subevent);
            break;
    }
}

static void hfp_call_timer_handler(btstack_timer_source_t *ts){
    (void) ts;
    printf("HFP: answering simulated call and establishing audio\n");
    hfp_ag_answer_incoming_call();
    if (hfp_acl_handle != HCI_CON_HANDLE_INVALID) {
        uint8_t audio_status = hfp_ag_establish_audio_connection(hfp_acl_handle);
        printf("HFP: establish audio status=0x%02x\n", audio_status);
    }
}

void hfp_ag_debug_init(void){
    // Debug only: allow an unauthenticated HF (Bumble) to open RFCOMM.
    // A real headset should pair first; set this back to LEVEL_2 once pairing
    // and bonding are wired up.
    gap_set_security_level(LEVEL_0);
    // Allow the test HF to pair/bond (Just Works) so the ACL can be
    // authenticated: the HF controller rejects eSCO with 0x12 otherwise.
    gap_set_bondable_mode(1);
    gap_ssp_set_enable(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_secure_connections_enable(false);
    printf("HFP: extended SCO supported=%d\n", hci_extended_sco_link_supported());
    rfcomm_init();

    const uint32_t features =
        (1u << HFP_AGSF_THREE_WAY_CALLING) |
        (1u << HFP_AGSF_IN_BAND_RING_TONE) |
        (1u << HFP_AGSF_ABILITY_TO_REJECT_A_CALL) |
        (1u << HFP_AGSF_ENHANCED_CALL_STATUS) |
        (1u << HFP_AGSF_ENHANCED_CALL_CONTROL) |
        (1u << HFP_AGSF_CODEC_NEGOTIATION) |
        (1u << HFP_AGSF_HF_INDICATORS) |
        (1u << HFP_AGSF_ESCO_S4);

    static const uint8_t codecs[] = { HFP_CODEC_CVSD, HFP_CODEC_MSBC };

    hfp_ag_init(1);
    hfp_ag_init_supported_features(features);
    hfp_ag_init_codecs(sizeof(codecs), codecs);
    hfp_ag_init_ag_indicators(sizeof(hfp_ag_indicators) / sizeof(hfp_ag_indicators[0]), hfp_ag_indicators);
    hfp_ag_register_packet_handler(&hfp_ag_packet_handler);

    memset(hfp_ag_sdp_buffer, 0, sizeof(hfp_ag_sdp_buffer));
    hfp_ag_create_sdp_record_with_codecs(hfp_ag_sdp_buffer, sdp_create_service_record_handle(),
                                         1, "Pico USB Audio", 1, (uint16_t)features,
                                         sizeof(codecs), codecs);
    sdp_register_service(hfp_ag_sdp_buffer);

    gap_discoverable_control(1);
    gap_connectable_control(1);
    printf("HFP AG ready: rfcomm ch 1, CVSD+mSBC, discoverable/connectable\n");
}


void bt_hci_init(void){

    // Request role change on reconnecting headset to always use them in slave mode
    hci_set_master_slave_policy(0);
    // enabled EIR
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);

    // Set local name with a template Bluetooth address, that will be automatically
    // replaced with a actual address once it is available, i.e. when BTstack boots
    // up and starts talking to a Bluetooth module.
    gap_set_local_name("Pico USB Audio");
    gap_discoverable_control(0);
    gap_set_class_of_device(0x200408);

        // Initialize HFP Audio Gateway (AG) to allow Hands-Free devices to connect
        // RFCOMM channel 1 is used for HFP by default here
        // hfp_ag_init(1);
        // hfp_ag_init_supported_features(HFP_DEFAULT_AG_SUPPORTED_FEATURES);

        // initialize SCO <-> USB bridge
        // sco_usb_bridge_init();
        // sco_usb_bridge_register_hfp();

    /* Register for HCI events */
    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    get_link_keys();

}