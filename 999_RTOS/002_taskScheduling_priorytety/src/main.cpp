#include <Arduino.h>

int app_cpu = 1;
int outPin1 = 18;
int outPin2 = 19;
//int outPin3 = 2;


TaskHandle_t hTaskPin1 = NULL;
void taskPin1(void* par) {
  while (1) {
    digitalWrite(outPin1, 1);
    vTaskDelay(5 / portTICK_PERIOD_MS);
    digitalWrite(outPin1, 0);
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

TaskHandle_t hTaskPin2 = NULL;
void taskPin2(void* par) {
  while (1) {
    for (int i = 0; i < 100000; i++) { // ta pętla trwa kilkadziesiąt milisekund i przerwie taska o mniejszym priorytecie
      digitalWrite(outPin2, 1);
      digitalWrite(outPin2, 0);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void setup() {

  pinMode(outPin1, OUTPUT);
  pinMode(outPin2, OUTPUT);
  //pinMode(outPin3, OUTPUT);

  xTaskCreatePinnedToCore(taskPin1, "out1", 1024, NULL, 1, &hTaskPin1, app_cpu);
  xTaskCreatePinnedToCore(taskPin2, "out2", 1024, NULL, 2, &hTaskPin2, app_cpu);

}

void loop() {
  if (millis() > 5000) {
    if (hTaskPin2 != NULL) {
      vTaskDelete(hTaskPin2);
      hTaskPin2 = NULL;
    }
  }
  if (millis() > 7000) {
    if (hTaskPin1 != NULL) {
      vTaskDelete(hTaskPin1);
      hTaskPin1 = NULL;
    }
  }
}
