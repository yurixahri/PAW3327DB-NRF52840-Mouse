#include <stdint.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/input/input.h>
#include <esb.h>

#include <zephyr/sys/poweroff.h>

#include <zephyr/settings/settings.h>

#include <hal/nrf_power.h>
#include <hal/nrf_clock.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <zephyr/drivers/adc.h>
#include <hal/nrf_saadc.h>

#define M_PI       3.14159265358979323846
#define DEG_TO_RAD(degrees) ((float)(degrees) * M_PI / 180.0)

//LOG_MODULE_REGISTER(paw3327, //LOG_LEVEL_INF);
#define ADC_NODE                DT_NODELABEL(adc)    
#define ADC_RESOLUTION          12                   
#define ADC_GAIN                ADC_GAIN_1_6         
#define ADC_REFERENCE           ADC_REF_INTERNAL     
#define ADC_ACQUISITION_TIME    ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID          0                    
#define ADC_CHANNEL_INPUT       NRF_SAADC_INPUT_VDD

#define SPI_DEV                 DT_NODELABEL(spi1)
#define GPIO_0                  DT_NODELABEL(gpio0)
#define GPIO_1                  DT_NODELABEL(gpio1)
#define SCK_PIN                 17
#define MOSI_PIN                22
#define MISO_PIN                20
#define CS_PIN                  24 
#define MOTION_PIN              4  
#define BUTTON1_PIN             31  
#define BUTTON2_PIN             29 
#define BUTTON3_PIN             13
#define BUTTON4_PIN             6 
#define BUTTON5_PIN             8
#define TOUCH1_PIN              2
#define TOUCH2_PIN              15
#define DPI_PIN                 6
#define VCC_CUTOFF_GPIO_PIN     13


#define PAW3327_MOTION_REG      0x02
#define PAW3327_DELTA_X_L_REG   0x03
#define PAW3327_DELTA_X_H_REG   0x04
#define PAW3327_DELTA_Y_L_REG   0x05
#define PAW3327_DELTA_Y_H_REG   0x06
#define PAW3327_SQUAL           0x07
#define PAW3327_MOTION_BIT      0x80
#define PAW3327_BURST_REG       0x16

#define DEBOUNCE_TIME           8 //ms
#define SCROLL_DELAY_HOLD_TIME  250 //ms
#define SCROLL_INTERVAL_TIME    25 //ms

#define POLL_INTERVAL_ACTIVE_MS 1  // active polling interval
#define POLL_INTERVAL_IDLE_MS   100   // idle polling interval
#define IDLE_THRESHOLD_MS       1000*10  // time before going idle
#define SLEEP_THRESHOLD_MS      1000*60*4  // time before going to sleep
#define IDLE_SLEEP_MS           100
#define BATT_CHECK_INTERVAL     1000*60*2
#define DPI_SAVE_INTERVAL_MS         1000*5 // only save DPI to flash every 5 seconds to reduce flash wear

const uint8_t mouse_dpi[] = {11, 21, 42, 84, 126};
const uint8_t mouse_dpi_size = sizeof(mouse_dpi) / sizeof(mouse_dpi[0]);
uint8_t current_dpi = 3;

/* ESB */
struct mouse_packet {
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
    int8_t wheel;
} __packed;


struct paw_burst_data {
    uint8_t motion;
    uint8_t delta_x_l;
    uint8_t delta_x_h;
    uint8_t delta_y_l;
    uint8_t delta_y_h;
    uint8_t squal;
};

/* --- SPI config --- */
static const struct spi_config spi_cfg = {
    .frequency = 4000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = NULL,

};

static const struct device *spi_dev;
static const struct device *gpio_0_dev;
static const struct device *gpio_1_dev;


// static struct gpio_callback motion_cb_data;
// Work queue for handling SPI reads
// static struct k_work motion_work;
// Semaphore to prevent interrupt overload
// static K_SEM_DEFINE(motion_sem, 1, 1);

// struct mouse_report {
//     uint8_t buttons;
//     int8_t x;
//     int8_t y;
//     int8_t wheel;
// };

struct key_debounce{
    bool is_pressed;
    bool on_double_click;
    int64_t pressed_time;
    uint16_t de_bounce;
    uint16_t end_time;
};

bool is_package_changed = false;
bool is_dpi_button_pressed = false;

bool button_pressed[5];
int64_t de_bounce[5];

static struct mouse_packet pkt;

bool scroll_up = false;
bool scroll_down = false;

int64_t scroll_up_holding_time = 0;
int64_t scroll_down_holding_time = 0;
int64_t scroll_up_interval_time = 0;
int64_t scroll_down_interval_time = 0;

struct key_debounce button5 = {.is_pressed = false, .on_double_click = false, .pressed_time = 0, .de_bounce = 10, .end_time = 500};

void button_check(uint8_t index, struct mouse_packet *pkt, bool is_pressed, int64_t now){
    if (is_pressed){
        if (!button_pressed[index] && now - de_bounce[index] >= DEBOUNCE_TIME){
            button_pressed[index] = true;
            pkt->buttons |= BIT(index);
            de_bounce[index] = now;
            is_package_changed = true;
        }
    }else{
        if (button_pressed[index] && now - de_bounce[index] >= DEBOUNCE_TIME){
            button_pressed[index] = false;
            pkt->buttons &= ~BIT(index);
            de_bounce[index] = now;
            is_package_changed = true;
        }
    }
}

void send_mouse_report_tx(struct mouse_packet *pkt){
    struct esb_payload payload = {
        .length = sizeof(struct mouse_packet),
        .pipe = 0,
        .noack = 1,
    };
    memcpy(payload.data, pkt, sizeof(struct mouse_packet));
    if (esb_write_payload(&payload) != 0) {
        esb_flush_tx();
        esb_write_payload(&payload);
    }
}

/* --- SPI helpers --- */
static inline void cs_select(void) {
    gpio_pin_set(gpio_0_dev, CS_PIN, 0);
    // k_busy_wait(50);
}
static inline void cs_deselect(void) {
    gpio_pin_set(gpio_0_dev, CS_PIN, 1);
    // k_busy_wait(50);
}

/* --- SPI read/write --- */
static uint8_t paw_read_reg(uint8_t reg, uint8_t *data)
{
    uint8_t tx[2] = { reg & 0x7F, 0x00 };
    uint8_t rx[2] = { 0 };

    struct spi_buf txb = { .buf = tx, .len = 2 };
    struct spi_buf rxb = { .buf = rx, .len = 2 };
    struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };

    cs_select();
    int ret = spi_transceive(spi_dev, &spi_cfg, &txs, &rxs);
    cs_deselect();
    if (ret) {
        //LOG_ERR("SPI read error %d", ret);
        return ret;
    }
    *data = rx[1];
    return 0;
}

static void paw_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg | 0x80, val };
    struct spi_buf b = { .buf = tx, .len = 2 };
    struct spi_buf_set txs = { .buffers = &b, .count = 1 };

    cs_select();
    spi_write(spi_dev, &spi_cfg, &txs);
    cs_deselect();
}

/* --- Burst motion read (0x16) --- */
static int paw_read_burst(struct paw_burst_data *burst_data) {
    uint8_t cmd = 0x16;

    struct spi_buf txb = { .buf = &cmd, .len = 1 };
    struct spi_buf_set tx = { .buffers = &txb, .count = 1 };

    cs_select();
    spi_write(spi_dev, &spi_cfg, &tx);

    k_usleep(35);

    uint8_t tx_dummy[7] = {0};
    uint8_t rx_buf[7] = {0};

    struct spi_buf txb2 = { .buf = tx_dummy, .len = 7 };
    struct spi_buf rxb2 = { .buf = rx_buf, .len = 7 };

    struct spi_buf_set tx2 = { .buffers = &txb2, .count = 1 };
    struct spi_buf_set rx2 = { .buffers = &rxb2, .count = 1 };

    spi_transceive(spi_dev, &spi_cfg, &tx2, &rx2);
    cs_deselect();

    burst_data->motion     = rx_buf[1];
    burst_data->delta_x_l  = rx_buf[2];
    burst_data->delta_x_h  = rx_buf[3];
    burst_data->delta_y_l  = rx_buf[4];
    burst_data->delta_y_h  = rx_buf[5];
    burst_data->squal      = rx_buf[6];
    k_usleep(250);
    
    return 0;
}

void paw3327_init(void){
    uint8_t id, inv, dummy;

    //LOG_INF("Initializing PAW3327...");

    /* Power-up reset */
    paw_write_reg(0x3A, 0x5A);      // POWER_UP_RESET
    k_msleep(50);

    /* Verify product ID */
    paw_read_reg(0x00, &id);        // PRODUCT_ID
    paw_read_reg(0x3F, &inv);       // INV_PRODUCT_ID
    //LOG_INF("Product ID=0x%02X, Inverse ID=0x%02X", id, inv);
    if (id != 0x4C || inv != 0xB3) {
        //LOG_WRN("Unexpected ID values! Check SPI polarity/mode.");
    }

    /* Exit shutdown */
    paw_write_reg(0x3B, 0x00);      // SHUTDOWN = 0 → Active mode
    k_msleep(50);              // Allow internal oscillator & LED to stabilize

    /* Optional internal setup (safe defaults) */
    paw_write_reg(0x1A, 0x03);      // RIPPLE_CONTROL (default)
    paw_write_reg(0x1B, mouse_dpi[current_dpi]);     // CPI_SETTING
    paw_write_reg(0x1E, 0x00);      // ANGLE_SNAP default
    paw_write_reg(0x20, 0x00);      // AXIS_CONTROL normal

    /* Dummy read to clear motion latch */
    paw_read_reg(0x01, &dummy);
    paw_read_reg(0x02, &dummy);
    paw_read_reg(0x03, &dummy);
    paw_read_reg(0x04, &dummy);
    paw_read_reg(0x05, &dummy);

    k_msleep(10);
    //LOG_INF("Resolution %d", dummy);
    //LOG_INF("PAW3327 initialization complete");
}

// void ensure_hfclk_running(void) {
//     // Check if high-frequency clock (HFXO) is running
//     if (!nrf_clock_hf_is_running(NRF_CLOCK, NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {

//         // Start the external high-frequency crystal oscillator
//         nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);

//         /* Wait until the HFCLK is ready */
//         while (!nrf_clock_hf_is_running(NRF_CLOCK, NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
//             k_busy_wait(10);
//         }
//     }
// }

void start_hfxo_properly(void) {
    const struct device *clk_dev = DEVICE_DT_GET(DT_NODELABEL(clock));
    
    // Request the HFXO (High Frequency Crystal Oscillator)
    // This is "clock-aware" and more efficient than a manual busy-wait
    clock_control_on(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_HF);
}

// static int dpi_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg){
//     const char *next;
//     int rc;

//     if (settings_name_steq(name, "index", &next) && !next) {
//         if (len != sizeof(current_dpi)) {
//             return -EINVAL;
//         }
//         rc = read_cb(cb_arg, &current_dpi, sizeof(current_dpi));
//         if (rc >= 0) {
//             return 0;
//         }
//         return rc;
//     }
//     return -ENOENT;
// }

// struct settings_handler dpi_settings_handler = {
//     .name = "dpi",
//     .h_set = dpi_settings_set,
// };

// static struct k_work dpi_save_work;

// static void dpi_save_work_handler(struct k_work *work)
// {
//     ARG_UNUSED(work);
//     //LOG_ERR(">> dpi_save_work_handler: saving dpi=%u\n", current_dpi);
//     int rc = settings_save_one("dpi/index", &current_dpi, sizeof(current_dpi));
//     //LOG_ERR(">> settings_save_one returned %d\n", rc);
//     if (rc) {
//         //LOG_ERR("Failed to save dpi: %d", rc);
//     } else {
//         //LOG_INF("Saved dpi index=%u", current_dpi);
//     }
// }
static int dpi_setting_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(name, "dpi") == 0) {
        read_cb(cb_arg, &current_dpi, sizeof(current_dpi));
        return 0;
    }
    return -ENOENT;
}

static struct settings_handler dpi_handler = {
    .name = "mouse",
    .h_set = dpi_setting_set
};

void save_dpi() {
    //LOG_INF("DPI saved to %d\n", current_dpi);
    settings_save_one("mouse/dpi", &current_dpi, sizeof(current_dpi));
}

void change_dpi(){
    current_dpi++;
    if (current_dpi >= mouse_dpi_size){
        current_dpi = 0;
    }
    //LOG_INF("DPI changed to %d\n", current_dpi);
    paw_write_reg(0x1B, mouse_dpi[current_dpi]);
}

void event_handler(struct esb_evt const *event){
    switch (event->evt_id) {
        case ESB_EVENT_TX_SUCCESS:
            break;
        case ESB_EVENT_TX_FAILED:
            esb_flush_tx();
            break;
        default:
            break;
    }
}

int esb_init_tx(void){
    /* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

    struct esb_config config = ESB_DEFAULT_CONFIG;
    config.mode = ESB_MODE_PTX; // Receiver mode
    config.event_handler = event_handler;
    config.bitrate = ESB_BITRATE_2MBPS;
    config.protocol = ESB_PROTOCOL_ESB_DPL;
    config.selective_auto_ack = true;
    config.tx_output_power = ESB_TX_POWER_4DBM;
    config.retransmit_count = 3;
    config.retransmit_delay = 250; // in microseconds
    

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
    
    /* --- Main --- */
int main(void){

    k_msleep(200); // Wait for system to stabilize

    int err;
    settings_subsys_init();
    settings_register(&dpi_handler);
    settings_load();
    //LOG_INF("Loaded DPI from flash: %d\n", current_dpi);
    // if (err) {
    //     //LOG_ERR("Error reading DPI from flash: %d\n", err);
    //     while(1) { k_msleep(1000); }
    // }


    nrf_power_dcdcen_set(NRF_POWER, true);
    start_hfxo_properly();
    // ensure_hfclk_running();
    err = esb_init_tx();
	if (err) {
		//LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

    // k_msleep(3000);

    //LOG_INF("PAW3327 start test...\n");
    spi_dev = DEVICE_DT_GET(SPI_DEV);
    gpio_0_dev = DEVICE_DT_GET(GPIO_0);
    gpio_1_dev = DEVICE_DT_GET(GPIO_1);

    if (!device_is_ready(spi_dev)) {
        //LOG_ERR("SPI not ready\n");
        return -ENODEV;
    }

    if (!device_is_ready(gpio_0_dev) || !device_is_ready(gpio_1_dev)) {
        //LOG_ERR("Devices not ready");
        return 1;
    }

    gpio_pin_configure(gpio_0_dev, CS_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio_1_dev, MOTION_PIN, GPIO_INPUT | GPIO_PULL_UP);

    gpio_pin_configure(gpio_0_dev, BUTTON1_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_0_dev, BUTTON2_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_1_dev, BUTTON3_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_0_dev, BUTTON4_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_0_dev, BUTTON5_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_0_dev, TOUCH1_PIN, GPIO_INPUT );
    gpio_pin_configure(gpio_1_dev, TOUCH2_PIN, GPIO_INPUT );
    gpio_pin_configure(gpio_1_dev, DPI_PIN, GPIO_INPUT | GPIO_PULL_UP);

    gpio_pin_configure(gpio_0_dev, VCC_CUTOFF_GPIO_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(gpio_0_dev, VCC_CUTOFF_GPIO_PIN, 1);

    // Initialize the work queue item
    // k_work_init(&motion_work, motion_handler);
    // err = gpio_pin_interrupt_configure(gpio_1_dev, MOTION_PIN, GPIO_INT_EDGE_FALLING);
    // if (err) {
    //     //LOG_ERR("Error configuring motion interrupt: %d", err);
    //     return err;
    // }
    // gpio_init_callback(&motion_cb_data, motion_callback, BIT(MOTION_PIN));
    // gpio_add_callback(gpio_1_dev, &motion_cb_data);
    
    paw3327_init();
    
    int64_t sleep_time = 0;
    int64_t last_dpi_save = 0;
    bool is_dpi_save_pending = false;
    int current_poll_interval = POLL_INTERVAL_ACTIVE_MS;

    struct k_timer polling_timer;
    k_timer_init(&polling_timer, NULL, NULL);
    k_timer_start(&polling_timer, K_MSEC(current_poll_interval), K_MSEC(current_poll_interval));
    // bool is_sensor_sleeping = false;
    // float scale = 1.49f;
    while (1) {
        int64_t now = k_uptime_get();
        k_timer_status_sync(&polling_timer);
        // bool wake_event = !gpio_pin_get(gpio_0_dev, BUTTON1_PIN) ||
        //               !gpio_pin_get(gpio_0_dev, BUTTON2_PIN);

        // if (is_sensor_sleeping && wake_event) {
        //     gpio_pin_set(gpio_0_dev, VCC_CUTOFF_GPIO_PIN, 1);
        //     esb_init_tx();
        //     k_msleep(50);
        //     paw_write_reg(0x1B, 0xC0);
        //     paw_write_reg(0x1A, 0x03);
        //     is_sensor_sleeping = false;
        //     sleep_time = now;
        // }

        struct paw_burst_data burst;
        pkt.dx = 0;
        pkt.dy = 0;
        int motion_pin_state = gpio_pin_get(gpio_1_dev, MOTION_PIN);
        if (motion_pin_state == 0) {
            if (paw_read_burst(&burst) == 0 ) { //
                if (burst.motion & PAW3327_MOTION_BIT && burst.squal > 10 ) { //
                    
                    int16_t dx = ((burst.delta_x_h << 8) | burst.delta_x_l);
                    int16_t dy = ((burst.delta_y_h << 8) | burst.delta_y_l);
                    
                    dx = CLAMP(dx, -127, 127) ;
                    dy = CLAMP(dy, -127, 127) ;
                    // // flip x
                    dx *= -1;

                    pkt.dx =  dx;
                    pkt.dy =  dy;
                    is_package_changed = true;
                }
            }
        }

        button_check(0, &pkt, !gpio_pin_get(gpio_0_dev, BUTTON1_PIN), now);
        button_check(1, &pkt, !gpio_pin_get(gpio_0_dev, BUTTON2_PIN), now);
        button_check(2, &pkt, !gpio_pin_get(gpio_1_dev, BUTTON3_PIN), now);
        button_check(4, &pkt, !gpio_pin_get(gpio_0_dev, BUTTON4_PIN), now);
        button_check(3, &pkt, !gpio_pin_get(gpio_0_dev, BUTTON5_PIN), now);

        // DPI button
        if (!gpio_pin_get(gpio_1_dev, DPI_PIN)){
            if (!is_dpi_button_pressed){
                change_dpi();
                last_dpi_save = now;
                is_dpi_save_pending = true;
                is_dpi_button_pressed = true;
            }
        }else{
            if (is_dpi_button_pressed)
                is_dpi_button_pressed = false;
        }

        if(gpio_pin_get(gpio_0_dev, TOUCH1_PIN)){
            //if (!scroll_up) scroll_up_holding_time = now;
            if (!scroll_up || (now - scroll_up_holding_time >= SCROLL_DELAY_HOLD_TIME && now - scroll_up_interval_time >= SCROLL_INTERVAL_TIME)){
                pkt.wheel = 1;
                is_package_changed = true;
                scroll_up = true;
                scroll_up_interval_time = now;
            }
        }else{
            scroll_up_holding_time = now;
            scroll_up = false;
        }
        if (gpio_pin_get(gpio_1_dev, TOUCH2_PIN)){
            //if (!scroll_down) scroll_down_holding_time = now;
            if (!scroll_down || (now - scroll_down_holding_time >= SCROLL_DELAY_HOLD_TIME && now - scroll_down_interval_time >= SCROLL_INTERVAL_TIME)){
                pkt.wheel = -1;
                is_package_changed = true;
                scroll_down = true;
                scroll_down_interval_time = now;
            }
        }else{
            scroll_down_holding_time = now;
            scroll_down = false;
        }

        if (gpio_pin_get(gpio_0_dev, TOUCH1_PIN) && gpio_pin_get(gpio_1_dev, TOUCH2_PIN)) {
            pkt.wheel = 0;
            is_package_changed = true;
        }else if(!gpio_pin_get(gpio_0_dev, TOUCH1_PIN) && !gpio_pin_get(gpio_1_dev, TOUCH2_PIN)){
            if  (pkt.wheel != 0){
                pkt.wheel = 0;
                is_package_changed = true;
            }           
        }

        if (is_package_changed){
            //LOG_INF("Sending packet: buttons=%02X, dx=%d, dy=%d, wheel=%d\n", pkt.buttons, pkt.dx, pkt.dy, pkt.wheel);
            send_mouse_report_tx(&pkt);
            is_package_changed = false;
            pkt.wheel = 0;
            sleep_time = now;
            if (current_poll_interval != POLL_INTERVAL_ACTIVE_MS){
                current_poll_interval = POLL_INTERVAL_ACTIVE_MS;
                esb_init_tx();
                k_timer_start(&polling_timer, K_MSEC(current_poll_interval), K_MSEC(current_poll_interval));
            }
        }

        if (now - sleep_time >= SLEEP_THRESHOLD_MS) {
            // if (!is_sensor_sleeping) {
                //     is_sensor_sleeping = true;
                //     esb_disable();
                // }
            gpio_pin_set(gpio_0_dev, VCC_CUTOFF_GPIO_PIN, 0);

            gpio_pin_configure(gpio_0_dev, CS_PIN, GPIO_DISCONNECTED);
            gpio_pin_configure(gpio_0_dev, SCK_PIN, GPIO_DISCONNECTED);
            gpio_pin_configure(gpio_0_dev, MOSI_PIN, GPIO_DISCONNECTED);
            gpio_pin_configure(gpio_0_dev, MISO_PIN, GPIO_DISCONNECTED);

            gpio_pin_configure(gpio_0_dev, TOUCH1_PIN, GPIO_DISCONNECTED);
            gpio_pin_configure(gpio_1_dev, TOUCH2_PIN, GPIO_DISCONNECTED);

            gpio_pin_interrupt_configure(gpio_0_dev, BUTTON1_PIN, GPIO_INT_LEVEL_LOW | GPIO_INT_WAKEUP);
            //gpio_pin_interrupt_configure(gpio_0_dev, BUTTON2_PIN, GPIO_INT_LEVEL_LOW | GPIO_INT_WAKEUP);

            k_msleep(200);
            sys_poweroff();
            //k_msleep(IDLE_SLEEP_MS);
        }else if (now - sleep_time >= IDLE_THRESHOLD_MS && current_poll_interval != POLL_INTERVAL_IDLE_MS) {
            current_poll_interval = POLL_INTERVAL_IDLE_MS;
            esb_disable();
            k_timer_start(&polling_timer, K_MSEC(current_poll_interval), K_MSEC(current_poll_interval));
        }

        if(!is_dpi_button_pressed && is_dpi_save_pending && now - last_dpi_save >= DPI_SAVE_INTERVAL_MS){
            save_dpi();
            is_dpi_save_pending = false;
        }
        // if (current_poll_interval != POLL_INTERVAL_CPU_CLOCK) {
        //     k_msleep(current_poll_interval);
        //     /* debug print ESB state while idle */
        //     //LOG_ERR("ESB state: OFF\n");
        // }else{
        //     while (k_cycle_get_32() - deadline < 0) {
                
        //     }
        // }
    }

    return 0;
}
