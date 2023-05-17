#include <Arduino.h>

int app_cpu = 1;
int outPin1 = 2;
int outPin2 = 2;
int outPin3 = 2;

void taskPin1(void* par) {
  while (1) {
    digitalWrite(outPin1, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    digitalWrite(outPin1, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}


void setup() {

  pinMode(outPin1, OUTPUT);
  pinMode(outPin2, OUTPUT);
  pinMode(outPin3, OUTPUT);

  xTaskCreatePinnedToCore(taskPin1, "out1", 1024, NULL, 1, NULL, app_cpu);

}

void loop() {
}
