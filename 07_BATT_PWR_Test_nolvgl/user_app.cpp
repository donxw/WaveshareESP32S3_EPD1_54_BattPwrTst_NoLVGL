#include <stdio.h>
#include <SPI.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "user_app.h"
#include "driver/gpio.h"
#include "user_config.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/rtc_io.h"
#include "esp_sleep.h"

#include "src/button_bsp/button_bsp.h"
#include "src/power/board_power_bsp.h"

// ---- GxEPD2 includes ----
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// ----------- EPD (1.54" D67) -----------
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));

static const char *TAG = "GxEPD2_1_54";

// Power rails helper (same as your original)
board_power_bsp_t board_div(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);

// Tracks whether VBAT domain is “armed” (your is_vabtflag logic)
static bool s_vbat_flag = false;

// ---------------------------------------------------------
// Helper: Initialize EPD with full screen clear
// ---------------------------------------------------------
static void epd_begin_full()
{
  // Initialize SPI with your defined pins
  SPI.end(); // ensure fresh SPI config
  SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);

  // Initialize GxEPD2 display
  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();

  // Clear once (full refresh)
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
}

// ---------------------------------------------------------
// Helper: Draw centered text
// ---------------------------------------------------------
static void drawCenteredText(const char* text, const GFXfont* font, int16_t y_center)
{
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y_center, &x1, &y1, &w, &h);
  int16_t x = (EPD_WIDTH - (int16_t)w) / 2;
  int16_t y = y_center + (h / 2);
  display.setCursor(x, y);
  display.print(text);
}

// ---------------------------------------------------------
// Helper: Full-screen message (ON / OFF screens)
// ---------------------------------------------------------
static void epd_showMessage_full(const char* msgLarge, const char* msgSmall)
{
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawCenteredText(msgLarge, &FreeMonoBold24pt7b, 90);
    if (msgSmall && msgSmall[0]) {
      drawCenteredText(msgSmall, &FreeMonoBold12pt7b, 150);
    }
  } while (display.nextPage());
}

// ---------------------------------------------------------
// Helper: Partial window update for small text region
// ---------------------------------------------------------
static void epd_showStatus_partial(const char* status)
{
  const int16_t x = 20, y = 70, w = 160, h = 70;
  display.setPartialWindow(x, y, w, h);
  display.firstPage();
  do {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    drawCenteredText(status, &FreeMonoBold24pt7b, y + h / 2);
  } while (display.nextPage());
}

// ---------------------------------------------------------
// LED blink test task
// ---------------------------------------------------------
static void led_test_task(void *arg)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = 0x1ULL << 3; // GPIO3
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

  for (;;)
  {
    gpio_set_level((gpio_num_t)3, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)3, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ---------------------------------------------------------
// VBAT status scan (from your original code)
// ---------------------------------------------------------
static void vbat_scanStatus(void)
{
  if (gpio_get_level(PWR_BUTTON_PIN) && (!s_vbat_flag))
  {
    s_vbat_flag = true;
  }
}

// ---------------------------------------------------------
// Simple wrappers for ON/OFF updates
// ---------------------------------------------------------
// static void epd_show_on()  { epd_showStatus_partial("ON"); }
// static void epd_show_off() { epd_showStatus_partial("OFF"); }  

static void epd_show_on()
{
  // Full-window refresh to erase any partial ghosting
  epd_showMessage_full("ON", "long press to  turn off");
  display.hibernate();  // put the panel into deep sleep (low current, stable image)
}

static void epd_show_off()
{
  // Full-window refresh to erase any partial ghosting
  epd_showMessage_full("OFF", "long press to  turn on");
  display.hibernate();  // put the panel into deep sleep (low current, stable image)
}

// ---------------------------------------------------------
// Button power management task
// ---------------------------------------------------------
static void button_power_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    EventBits_t even = xEventGroupWaitBits(
        pwr_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));

    // Long-press (bit 2) → show OFF, power down VBAT
    if (get_bit_button(even, 2))
    {
      if (s_vbat_flag)
      {
        s_vbat_flag = false;
        epd_show_off();
        vTaskDelay(pdMS_TO_TICKS(600));
        board_div.VBAT_POWER_OFF();
        // The system may lose power after this line depending on hardware
      }
    }
    // Release (bit 3) → re-arm VBAT flag
    else if (get_bit_button(even, 3))
    {
      if (!s_vbat_flag)
      {
        s_vbat_flag = true;
      }
    }
  }
}

// ---------------------------------------------------------
// Initialize peripherals and display
// ---------------------------------------------------------
void user_app_init(void)
{
  board_div.VBAT_POWER_ON();
  board_div.POWEER_EPD_ON();
  board_div.POWEER_Audio_ON();

  epd_begin_full();
  user_button_init();
}

// ---------------------------------------------------------
// Initialize simple UI and tasks - displays EPD, starts led flash task, monitors for power off
// ---------------------------------------------------------
void user_ui_init(void)
{
  vbat_scanStatus();
  // epd_showMessage_full("ON", "Power: VBAT active");
  epd_show_on();

  xTaskCreatePinnedToCore(led_test_task, "led_test_task", 4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(button_power_task, "button_power_task", 4096, NULL, 4, NULL, 1);
}
