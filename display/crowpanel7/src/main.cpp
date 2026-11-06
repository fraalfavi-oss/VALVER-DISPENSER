/*---------------------------------------------------------------
 * PlatformIO LVGL dashboard
 * CrowPanel V3.0 RGB and GT911 base for VALVER HMI touch-only preview.
 *--------------------------------------------------------------*/

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <cstring>
#include "soc/soc.h"
#include "soc/io_mux_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "hal/usb_serial_jtag_ll.h"


#include "valver_ui.h"

/*---------------------------------------------------------------
 * RGB display configuration
 * Bind the ESP32-S3 RGB signals and panel timing to LovyanGFX.
 *--------------------------------------------------------------*/

class LGFX : public lgfx::LGFX_Device
{
public:
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;

    /**
     * @brief Configure the RGB bus and the 800 by 480 panel.
     * @param None.
     * @return A configured LGFX device through the global lcd object.
     * @note Runs automatically while lcd is constructed before setup().
     */
    LGFX()
    {
        auto bus_config = bus.config();
        bus_config.panel = &panel;
        bus_config.pin_d0 = GPIO_NUM_15;
        bus_config.pin_d1 = GPIO_NUM_7;
        bus_config.pin_d2 = GPIO_NUM_6;
        bus_config.pin_d3 = GPIO_NUM_5;
        bus_config.pin_d4 = GPIO_NUM_4;
        bus_config.pin_d5 = GPIO_NUM_9;
        bus_config.pin_d6 = GPIO_NUM_46;
        bus_config.pin_d7 = GPIO_NUM_3;
        bus_config.pin_d8 = GPIO_NUM_8;
        bus_config.pin_d9 = GPIO_NUM_16;
        bus_config.pin_d10 = GPIO_NUM_1;
        bus_config.pin_d11 = GPIO_NUM_14;
        bus_config.pin_d12 = GPIO_NUM_21;
        bus_config.pin_d13 = GPIO_NUM_47;
        bus_config.pin_d14 = GPIO_NUM_48;
        bus_config.pin_d15 = GPIO_NUM_45;
        bus_config.pin_henable = GPIO_NUM_41;
        bus_config.pin_vsync = GPIO_NUM_40;
        bus_config.pin_hsync = GPIO_NUM_39;
        bus_config.pin_pclk = GPIO_NUM_0;
        // 20 MHz gives about 41 RGB refreshes/s with these panel porches.
        // The previous 15 MHz mode only refreshed at about 31 Hz.
        // Keep enough PSRAM/EDMA headroom rather than returning to 24 MHz.
        bus_config.freq_write = 20000000;
        bus_config.hsync_polarity = 0;
        bus_config.hsync_front_porch = 40;
        bus_config.hsync_pulse_width = 48;
        bus_config.hsync_back_porch = 40;
        bus_config.vsync_polarity = 0;
        bus_config.vsync_front_porch = 1;
        bus_config.vsync_pulse_width = 31;
        bus_config.vsync_back_porch = 13;
        bus_config.pclk_active_neg = 1;
        bus_config.de_idle_high = 0;
        bus_config.pclk_idle_high = 0;
        bus.config(bus_config);

        auto panel_config = panel.config();
        panel_config.memory_width = 800;
        panel_config.memory_height = 480;
        panel_config.panel_width = 800;
        panel_config.panel_height = 480;
        panel_config.offset_x = 0;
        panel_config.offset_y = 0;
        panel.config(panel_config);

        panel.setBus(&bus);
        setPanel(&panel);
    }

};

// Owns the panel and the two full-screen frame buffers used by LVGL.
LGFX lcd;

#include "touch.h"

namespace
{
// Board pins and logical dimensions shared by the display pipeline.
constexpr uint8_t   BACKLIGHT_PIN = 2;
constexpr uint32_t  SCREEN_WIDTH = 800;
constexpr uint32_t  SCREEN_HEIGHT = 480;
constexpr size_t FRAME_BYTES = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
constexpr uint32_t LVGL_DRAW_LINES = 32;
constexpr size_t LVGL_DRAW_BYTES = (size_t)SCREEN_WIDTH * LVGL_DRAW_LINES * sizeof(uint16_t);

// Keep the RGB DMA front framebuffer completely separate from LVGL rendering.
// The driver owns two full-screen PSRAM buffers. LVGL paints only a 32-line
// scratch buffer in internal RAM. Only finished images reach RGB on VSYNC.
uint8_t *rgb_front = nullptr;
uint8_t *rgb_back = nullptr;
uint8_t *lvgl_scratch = nullptr;
bool rgb_frame_open = false;
uint32_t rgb_present_ok = 0;
uint32_t rgb_present_errors = 0;
uint32_t rgb_last_error_log = 0;


/**
 * @brief Return milliseconds for LVGL's internal time base.
 * @param None.
 * @return Current Arduino millisecond counter.
 * @note Registered with LVGL during setup().
 */
uint32_t tick_get()
{
    return millis();
}

/**
 * @brief Switch the RGB panel to the buffer rendered by LVGL.
 * @param display LVGL display requesting the transfer.
 * @param area Unused because full-frame rendering is configured.
 * @param pixel_map Frame buffer selected by LVGL.
 * @return Nothing.
 * @note Called by LVGL after each rendered frame.
 */
void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    if(!rgb_front || !rgb_back || !pixel_map){
        lv_display_flush_ready(display);
        return;
    }
    // The screen keeps scanning rgb_front unchanged while LVGL draws the
    // updated rectangles into rgb_back. Copy the previous completed frame
    // first so unchanged pixels never blink or revert to an old image.
    if(!rgb_frame_open){
        memcpy(rgb_back, rgb_front, FRAME_BYTES);
        rgb_frame_open = true;
    }

    const int x1=area->x1, y1=area->y1, x2=area->x2, y2=area->y2;
    if(x1>=0 && y1>=0 && x2<(int)SCREEN_WIDTH &&
       y2<(int)SCREEN_HEIGHT && x1<=x2 && y1<=y2){
        const size_t row_bytes=(size_t)(x2-x1+1)*sizeof(uint16_t);
        uint8_t *dst=rgb_back+((size_t)y1*SCREEN_WIDTH+x1)*sizeof(uint16_t);
        for(int row=0;row<=y2-y1;row++){
            memcpy(dst+(size_t)row*SCREEN_WIDTH*sizeof(uint16_t),
                   pixel_map+(size_t)row*row_bytes,row_bytes);
        }
    }

    // Commit exactly ONCE after the last LVGL rectangle, when the entire
    // back framebuffer is ready. The RGB driver flips on VSYNC.
    if(lv_display_flush_is_last(display)){
        const bool presented=lcd.bus.presentFrameBuffer(rgb_back, 150);
        if(presented){
            uint8_t *old_front=rgb_front;
            rgb_front=rgb_back;
            rgb_back=old_front;
            rgb_present_ok++;
        }else{
            rgb_present_errors++;
            const uint32_t now=millis();
            if(now-rgb_last_error_log>2500){
                rgb_last_error_log=now;
                Serial.printf("[RGB] VSYNC timeout; OK=%lu ERR=%lu\n",
                  (unsigned long)rgb_present_ok,
                  (unsigned long)rgb_present_errors);
            }
        }
        rgb_frame_open=false;
    }
    lv_display_flush_ready(display);
}

/**
 * @brief Supply the current touch state and coordinates to LVGL.
 * @param indev LVGL input device requesting a sample.
 * @param data Destination for pointer state and coordinates.
 * @return Nothing.
 * @note Called periodically after the input device is registered.
 */
void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    static bool was_pressed=false;
    const bool pressed=touch_touched();
    if(pressed){
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_last_x;
        data->point.y = touch_last_y;
        if(!was_pressed){
            Serial.printf("[TOUCH] X=%d Y=%d\n",touch_last_x,touch_last_y);
            valver_ui_note_touch(touch_last_x,touch_last_y);
        }
    }
    was_pressed=pressed;
}
}

// Desired output state selected by the generated UI button callbacks.


/*---------------------------------------------------------------
 * Hardware and UI initialization
 * Start buses, peripherals, LVGL, and the generated dashboard.
 *--------------------------------------------------------------*/

/**
 * @brief Initialize the display, touch controller, sensor, and UI.
 * @param None.
 * @return Nothing.
 * @note Called once by the Arduino framework after reset.
 */
void setup()
{
    Serial.begin(115200);

    // GPIO19/20 share the ESP32-S3 native USB-JTAG PHY. This board's
    // external CH340 on COM15 remains available for logging/programming.
    // Return the shared pads to ordinary GPIO before using GT911 I2C.
    usb_serial_jtag_ll_phy_enable_pad(false);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
    PIN_FUNC_SELECT(IO_MUX_GPIO19_REG, PIN_FUNC_GPIO);
    PIN_FUNC_SELECT(IO_MUX_GPIO20_REG, PIN_FUNC_GPIO);

    // Elecrow V3.0 starts I2C BEFORE the RGB controller. Reinitialise it
    // after lcd.begin(); this ordering is important for the PCA9557/GT911.
    const bool initial_i2c=Wire1.begin(19,20);
    Wire1.setClock(100000);
    Wire1.setTimeOut(50);
    Serial.printf("[I2C] Primer inicio: %s\n",initial_i2c?"OK":"ERROR");
    if(!lcd.begin()) {
        Serial.println("[DISPLAY] lcd.begin() failed");
        return;
    }
    delay(200);

    Wire1.end();
    delay(10);
    const bool ready_i2c=Wire1.begin(19,20);
    Wire1.setClock(100000);
    Wire1.setTimeOut(50);
    Serial.printf("[I2C] Reinicio tras LCD: %s\n",ready_i2c?"OK":"ERROR");
    lv_init();
    lv_tick_set_cb(tick_get);
    const bool touch_ready=ready_i2c?touch_init():false;
    if(!ready_i2c)Serial.println("[TOUCH] GT911 omitido: I2C no inicializado");

    rgb_front=static_cast<uint8_t *>(lcd.bus.getFrameBuffer(0));
    rgb_back=static_cast<uint8_t *>(lcd.bus.getFrameBuffer(1));
    Serial.printf("[RGB] PSRAM framebuffer A=%p B=%p\n",rgb_front,rgb_back);
    if(!rgb_front || !rgb_back) {
        Serial.println("[RGB] ERROR: double framebuffer allocation failed");
        return;
    }
    // The temporary LVGL buffer is in *internal* memory. It is not a panel
    // framebuffer: LVGL can never draw over a frame currently being scanned.
    lvgl_scratch=static_cast<uint8_t *>(
        heap_caps_malloc(LVGL_DRAW_BYTES, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!lvgl_scratch){
        Serial.printf("[RGB] ERROR: internal LVGL scratch %u bytes failed\n",
                      (unsigned)LVGL_DRAW_BYTES);
        return;
    }
    Serial.printf("[RGB] RGB565 PARTIAL 32 lines; LVGL scratch=%p (%u bytes)\n",
                  lvgl_scratch,(unsigned)LVGL_DRAW_BYTES);

    lv_display_t *display = lv_display_create(lcd.width(), lcd.height());
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, lvgl_scratch, nullptr,
                           LVGL_DRAW_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *touchpad = lv_indev_create();
    lv_indev_set_type(touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touchpad, touchpad_read);

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);

    valver_ui_init();
    valver_ui_set_touch_status(touch_ready ? touch_address : 0);
    Serial.printf("[DISPLAY] VALVER 800x480, RGB 20MHz, partial+VSYNC, GT911 %s\n",
                  touch_ready ? "OK" : "NO DETECTADO");
}

/*---------------------------------------------------------------
 * Runtime sensor and UI processing
 * Service touch UI and LVGL; no motor pins or motion commands.
 *--------------------------------------------------------------*/

/**
 * @brief Refresh sensor labels, output state, and LVGL timers.
 * @param None.
 * @return Nothing.
 * @note Called repeatedly by the Arduino framework after setup().
 */
void loop()
{
    lv_timer_handler();
    valver_ui_tick();
    delay(10);
}
