#if CONFIG_FREERTOS_UNICORE
  static const BaseType_t app_cpu = 0;
#else
  static const BaseType_t app_cpu = 1;
#endif

#define TIMER_INTERVAL_US 10000
// 10,000 microseconds = 10 milli seconds

static const int adc_pin = 34;

hw_timer_t* timer = NULL;
volatile bool timerFlag = false;

#define BUFFER_SIZE 32
volatile int buffer[BUFFER_SIZE];
volatile int head = 0;
volatile int tail = 0;

// volatile SemaphoreHandle_t BufferMutex = NULL;
// Spinlock for ISR <-> Task synchronization
portMUX_TYPE bufferMux =portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t TaskA_Handle =NULL;

volatile float avg = 0;
volatile SemaphoreHandle_t AvgMutex = NULL;

void IRAM_ATTR onTimer() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  //instead of seamaphores to control access to the buffer
  //we use spinlocks
  portENTER_CRITICAL_ISR(&bufferMux);

  if (head == (tail + 1) % BUFFER_SIZE) {
    // the buffer is full activate task A
    vTaskNotifyGiveFromISR(TaskA_Handle, &xHigherPriorityTaskWoken);
    
    // Force a context switch if Task A has a higher priority than the currently running task
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    int val = analogRead(adc_pin);
    buffer[head] = val;
    head = (head + 1) % BUFFER_SIZE;
  }

   portEXIT_CRITICAL_ISR(&bufferMux);
 
}
  void TaskA(void* parameters) {
    while (1) {
      // Wait here infinitely until the ISR sends a notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Enter critical section just long enough to copy data and reset the buffer
    portENTER_CRITICAL(&bufferMux);
      int total = 0;
      // if (head == (tail + 1) % BUFFER_SIZE) {
        //no need for if statement whenever TaskA gets notified 
        //it will be for actual usage 
        for (int i = 0; i < 32; i++) {
          total = total + buffer[i];
        }
        head = 0;
        tail = 0;
        portEXIT_CRITICAL(&bufferMux);

        xSemaphoreTake(AvgMutex,portMAX_DELAY);
        //divide by 32.0 not 32 so that float gets stored
        avg = total / 32.0;
        xSemaphoreGive(AvgMutex);
    // }

    }
   
  }
void TaskB(void* parameters){
  while(1){
    if(Serial.available()>0){
      String incomingString = Serial.readStringUntil('\n'); // Read until newline
      incomingString.trim(); // Remove whitespace/newlines

      if(incomingString =="avg"){
        xSemaphoreTake(AvgMutex,portMAX_DELAY);
        // Serial.println(avg);instead of printing direlctly
        //we copy and then print allowing us to give Semaphores much faster
        float currentAvg= avg;
        xSemaphoreGive(AvgMutex);
        Serial.println(currentAvg);
        
      }
      else{
        Serial.println(incomingString);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void setup() {
  Serial.begin(115200);

  AvgMutex=xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(TaskA, "TaskA", 2048, NULL, 1, &TaskA_Handle,app_cpu);
  xTaskCreatePinnedToCore(TaskB, "TaskB", 2048, NULL,1, NULL,app_cpu);
  timer = timerBegin(TIMER_INTERVAL_US);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, TIMER_INTERVAL_US, true, 0);
}
void loop(){

}