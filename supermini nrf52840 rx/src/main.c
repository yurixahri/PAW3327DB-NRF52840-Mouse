#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <esb.h>

LOG_MODULE_REGISTER(mouse_rx, LOG_LEVEL_INF);

const struct device *hid_dev;
static enum usb_dc_status_code usb_status;
static K_SEM_DEFINE(ep_write_sem, 0, 1);
static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param){
	usb_status = status;
    switch (status){
        case USB_DC_CONFIGURED:
            k_sem_give(&ep_write_sem);
            break;
        default:
            break;
    }
}
static void int_in_ready_cb(const struct device *dev){
	ARG_UNUSED(dev);
	k_sem_give(&ep_write_sem);
}
static const uint8_t hid_report_desc[] = HID_MOUSE_REPORT_DESC(5);
static const struct hid_ops ops = {
	.int_in_ready = int_in_ready_cb,
};

// struct mouse_report {
//     uint8_t buttons;
//     int8_t x;
//     int8_t y;
//     int8_t wheel;
// };

struct mouse_packet {
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
    int8_t wheel;
} __packed;

K_MSGQ_DEFINE(mouse_report_queue, sizeof(struct mouse_packet), 1000, 4);

/* HID*/
void send_mouse_report(struct mouse_packet *report){
    // static struct mouse_report report = { 0, 0, 0, 0 };

    // report.buttons = packet->buttons;   // Button bitmask (0 = none pressed)
    // report.x = packet->dx;     // ΔX
    // report.y = packet->dy;     // ΔY
    // report.wheel = packet->wheel;
    // LOG_INF("HID report: BTN=0x%02X, DX=%d, DY=%d, WH=%d",
    //         report[0], (int8_t)report[1], (int8_t)report[2], (int8_t)report[3]);

    k_sem_take(&ep_write_sem, K_MSEC(1)); // Wait for previous transfer to complete
    hid_int_ep_write(hid_dev, (const uint8_t *)report, sizeof(*report), NULL);
    // if (report->wheel != 0) {
    //     report->wheel = 0;
    //     report->dx = 0;
    //     report->dy = 0;
    //     k_sem_take(&ep_write_sem, K_MSEC(1)); 
    //     hid_int_ep_write(hid_dev, (const uint8_t *)report, sizeof(*report), NULL);
    // }
}



void event_handler(struct esb_evt const *event){
    if (event->evt_id == ESB_EVENT_RX_RECEIVED) {
        struct esb_payload payload;
        while (esb_read_rx_payload(&payload) == 0) {
            struct mouse_packet *pkt = (struct mouse_packet *)payload.data;
            k_msgq_put(&mouse_report_queue, pkt, K_NO_WAIT);
            // send_mouse_report(pkt);
            // LOG_INF("dx=%d dy=%d btn=%02X", pkt->dx, pkt->dy, pkt->buttons);

            // TODO: send via HID here
            // send_mouse_report(hid_dev, pkt->dx, pkt->dy, pkt->wheel, 
            //                   pkt->buttons & 1, pkt->buttons & 2, pkt->buttons & 4, pkt->buttons & 8, pkt->buttons & 16);
        }
    }
}

void hid_send_thread(void *p1, void *p2, void *p3) {
    struct mouse_packet report;

    while (true) {
        // Block until a new report is available in the queue
        k_msgq_get(&mouse_report_queue, &report, K_FOREVER);

        // Now, send the HID report safely from the thread context
        if (hid_dev != NULL) {
            send_mouse_report(&report);
        }
    }
}
K_THREAD_DEFINE(hid_send_thread_id, 1024*4, hid_send_thread, NULL, NULL, NULL, 5, 0, 0);

int esb_init_rx(void) {
    /* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

    struct esb_config config = ESB_DEFAULT_CONFIG;
    config.mode = ESB_MODE_PRX; // Receiver mode
    config.event_handler = event_handler;
    config.bitrate = ESB_BITRATE_2MBPS;
    config.protocol = ESB_PROTOCOL_ESB_DPL;
    config.selective_auto_ack = true;

    if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

    int err = esb_init(&config);
    err = esb_set_base_address_0(base_addr_0);
    if (err) {
            return err;
    }
    err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}
    
    return 0;
}

void hid_init(){
    hid_dev = device_get_binding("HID_0");
    if (!hid_dev) {
        LOG_ERR("Cannot find HID device");
        return;
    }

    usb_hid_register_device(hid_dev,
				hid_report_desc, sizeof(hid_report_desc),
				&ops);

	usb_hid_init(hid_dev);

    // int ret = usb_enable(status_cb);
    // if (ret != 0) {
    //     LOG_ERR("Failed to enable USB: %d", ret);
    //     return;
    // }

    LOG_INF("USB HID ready");
}

int main(void){
    hid_init();
    int err;
    err = esb_init_rx();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

    err = esb_start_rx();
	if (err) {
		LOG_ERR("RX setup failed, err %d", err);
		return 0;
	}

    LOG_INF("Initialization complete");

    while (1) {
        k_sleep(K_MSEC(50));
    }
    return 0;
}
