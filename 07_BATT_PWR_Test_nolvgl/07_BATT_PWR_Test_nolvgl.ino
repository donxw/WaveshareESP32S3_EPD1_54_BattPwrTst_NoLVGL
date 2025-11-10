#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "user_config.h"
#include "user_app.h"

void setup()
{
  Serial.begin(115200);
  delay(200);

  user_app_init();   // power rails up + EPD init + buttons
  user_ui_init();    // draw initial "ON", start tasks
}

void loop()
{
  // FreeRTOS tasks do the work; nothing needed here.
  vTaskDelay(pdMS_TO_TICKS(500));

  //code to update display is in user_app.cpp - need to incorporate font.h
}
