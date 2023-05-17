#include <Arduino.h>

int app_cpu = 1;
int led_pin = 2;

void taskBlinkLed(void* par) {
  while (1) {
    digitalWrite(led_pin, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    digitalWrite(led_pin, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void taskBlinkLed2(void* par) {
  while (1) {
    digitalWrite(led_pin, 1);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    digitalWrite(led_pin, 0);
    vTaskDelay(400 / portTICK_PERIOD_MS);
  }
}

void setup() {

  pinMode(led_pin, OUTPUT);
  xTaskCreatePinnedToCore(taskBlinkLed, // wywoływany task
    "BLink", // nazwa tekstowa taska 
    1024, // rozmiar stosu dla taska (dla ESP32 w bajtach)                          
    NULL, // parametr przekazywany do taska
    1, // priorytet, w ESP32 od 0 do configMAX_PRIORITIES
    NULL, // task handle
    app_cpu); // numer rdzenia na którym będzie wykonywany task

  xTaskCreatePinnedToCore(taskBlinkLed2, "BLink", 1024, NULL, 1, NULL, app_cpu); //drugi task na tym samum proirytecie 

  // nie ma potrzeby uruchamiania za pomocą vTaskStartScheduler() bo w Arduino ESP32 jest uruchamiany automatycznie po setup()
}

void loop() {
}
