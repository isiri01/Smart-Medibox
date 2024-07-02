//libraries
#include <Wire.h> //I2C lib
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>


WiFiClient espClient;
PubSubClient mqttClient(espClient);

//definitions to display the time
#define NTP_SERVER "pool.ntp.org"
int UTC_OFFSET = 0;
#define UTC_OFFSET_DST 0

//variables to initiate the display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

//variables defining buttons,sensors and buzzer
#define BUZZER 5
#define LED_1 2
#define LED_2 15
#define PB_CANCEL 34
#define PB_DOWN 35
#define PB_UP 32
#define PB_OK 33
#define DHT 12
#define LDR_right 36 //right lDR
#define LDR_left 39  //left ldr


#define servoPin 18
float motorAngle = 0;
float motorAngleOffset;
float controlFactor = 0.75;
String message ;
float minimumAngle =30;
float D_servo ;

//Parameters related to light intensity
float GAMMA = 0.75;
const float RL10 = 50;
float MIN_ANGLE = 30;
float lux_right = 0;
float lux_left =0;

//Parameters related to publishing data 
char ldr_ar[6];
char temp_ar[6];
char humid_ar[6];
char high_ldr[38];             


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;
Servo servo;

//variables to define time and date
int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;
int month,year;
String time_string;
String date_string;

//variables to define temperature and humidity
String temp_value;
String humid_value;
bool temp_ok = true;
bool humid_ok = true;



//variables to initiate the alarms
unsigned long timeLast = 0;
unsigned long timeNow = 0;
int offset_hours = 0;
int offset_minutes = 0;
bool alarm_enabled = false;
int n_alarms = 3;
int alarm_num = 0;
int alarm_hours[] = {0, 0, 0};
int alarm_minutes[] = {0, 0, 0};
bool alarm_triggered[] = {false, false, false};

//variables for buzzer sound
int n_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 349;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

//variables for the menu
int current_mode = 0;
int max_modes = 6;
String modes[] = {"1-Set Time Zone", "2-Set Alarm 1", 
"3-Set Alarm 2", "4-Set Alarm 3", "5-Disable Alarm"};


void setup() {
  Serial.begin(115200);

  //declaring outputs and inputs
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_DOWN, INPUT);
  pinMode(PB_UP, INPUT);

  servo.attach(servoPin, 500, 2400); //servo mototor initializing

  dhtSensor.setup(DHT, DHTesp::DHT22);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  // Show the display buffer on the screen
  display.display();
  delay(500);

  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED){
    delay(250);
    display.clearDisplay();
    print_line("Connecting to Wi-Fi", 0, 0, 2);
  }

  

  display.clearDisplay();
  print_line("Connected to Wifi", 0, 0, 2);
  delay(250);

  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);

  // Clear the buffer
  display.clearDisplay();
  print_line("Welcome to MediBox !", 0, 15, 2);
  openingSound();
  delay(500);
  display.clearDisplay();

  setupMqtt(); //initializing Mqtt settings
}

void loop() {
  slidingWindow();
  
  update_time_with_check_alarm();
  if (digitalRead(PB_OK) == LOW) {
    delay(200);
    go_to_menu();
  }

  check_temp();
  
  if(!mqttClient.connected()){
    connectToBroker();
  }

  
  LightIntensity();
  tempandhumid();
  mqttClient.loop(); //publishing data
  mqttClient.publish("LDR-intensity",ldr_ar);
  mqttClient.publish("LDR-identifier",high_ldr);
  mqttClient.publish("Temperature",temp_ar);
  mqttClient.publish("Humidity",humid_ar);
  
}

//establishing mqtt connection
void setupMqtt(){
  mqttClient.setServer("test.mosquitto.org",1883);
  mqttClient.setCallback(receiveCallback);

}
// Attempts to connect to MQTT broker and subscribes to two topics.
void connectToBroker() {                                                   
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect("ESP-8266-000789458")) {
      Serial.print("connected");
      mqttClient.subscribe("ControlFactor");
      mqttClient.subscribe("Minimumangle");
    } else {
      Serial.print("failed ");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}

//calculating intensity from voltage values
void LightIntensity(){
  //calculate light intensity in 0-1 range
  float voltage1 = analogRead(LDR_right) / 1024. * 5;
  float resistance1 = 2000 * voltage1 / (1 - voltage1 / 5);
  float maxlux1 = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
  lux_right = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance1, (1 / GAMMA))/maxlux1;


  float voltage2 = analogRead(LDR_left) / 1024. * 5;
  float resistance2 = 2000 * voltage2 / (1 - voltage2 / 5);
  float maxlux2 = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
  lux_left = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance2, (1 / GAMMA))/maxlux2;

  String temp;

   //checking if intensity values are nan and setting them to zero
  if(isnan(lux_right)){
    lux_right = 0;
  }
  if(isnan(lux_left)){
    lux_left =0;
  }

//comparing intensity values
  if(lux_right>lux_left){
    String(lux_right).toCharArray(ldr_ar,6);
    //right ldr
    temp = "Right LDR gives the highest intensity"; //
    temp.toCharArray(high_ldr,38);
  }else if(lux_left>lux_right){
    String(lux_left).toCharArray(ldr_ar,6);
    temp = "Left LDR gives the highest intensity"; //left ldr
    temp.toCharArray(high_ldr,38);
  }else{
    temp = "LDRs give the same intensity";
    temp.toCharArray(high_ldr,38);
  }

}

void slidingWindow(){
  //calculate light intensity in 0-1 range
  float voltage1 = analogRead(LDR_right) / 1024. * 5;
  float resistance1 = 2000 * voltage1 / (1 - voltage1 / 5);
  float maxlux1 = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
  lux_right = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance1, (1 / GAMMA))/maxlux1;


  float voltage2 = analogRead(LDR_left) / 1024. * 5;
  float resistance2 = 2000 * voltage2 / (1 - voltage2 / 5);
  float maxlux2 = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
  lux_left = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance2, (1 / GAMMA))/maxlux2;

  String temp;

   //checking if intensity values are nan and setting them to zero
  if(isnan(lux_right)){
    lux_right = 0;
  }
  if(isnan(lux_left)){
    lux_left =0;
  }

  if(lux_left < lux_right){
    D_servo = 0.5;
  }else{
    D_servo= 1.5;
  }

  float max_intensity = max(lux_right,lux_left);
  if(minimumAngle!=0 && controlFactor != 0 ){
    motorAngleOffset = minimumAngle;
    motorAngle = min((motorAngleOffset*D_servo) + ((180 - motorAngleOffset)*max_intensity*controlFactor),(float)180) ;
    Serial.print("motorAngle: ");
    Serial.println(motorAngle);
    servo.write(motorAngle);
    delay(15);
  }

}

void tempandhumid(void){
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  delay(100);
  temp_value = String(data.temperature, 2);
  humid_value = String(data.humidity, 1);
  temp_value.toCharArray(temp_ar,6);
  humid_value.toCharArray(humid_ar,6);
}

//Receiving data from the server
void receiveCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char payloadCharAr[length];
  for (int i = 0; i < length; i++) {
    payloadCharAr[i] = (char)payload[i]; 
  }
  
  String stMessage;

  if (strcmp(topic,"Minimumangle")==0) {
    for (int i = 0; i < length; i++) {
      stMessage += (char)payload[i];
      minimumAngle = stMessage.toFloat();
      delay(100); // Add delay for stabilization
    }
    if(minimumAngle == 0){
      Serial.println("Enter a value");

    }else{

    Serial.print("Minang: ");
    Serial.println(minimumAngle);
    }
    
  }

  
    
  if (strcmp(topic,"ControlFactor")==0) {
    for (int i = 0; i < length; i++) {
      stMessage += (char)payload[i];
      controlFactor = stMessage.toFloat();
      delay(100); // Add delay for stabilization
    }
    if(controlFactor == 0){
      Serial.println("Enter a value");

    }else{

    Serial.print("CF: ");
    Serial.println(controlFactor);
    }
    
  }

}

//End of the tasks of Assignment 02
//-------------------------------------------------------------------------

//DISPLAY FUNCTIONS
//display texts on the OLED display
void print_line(String text, int column, int row, int textsize) {
  display.setTextSize(textsize);            
  display.setTextColor(SSD1306_WHITE);       
  display.setCursor(column, row);            
  display.print(text);

  display.display();
}

void print_time_now(void) {
  display.clearDisplay();
  date_string=String(days)+"."+String(month)+"."+String(year);
  time_string=String(hours)+":"+String(minutes)+":"+String(seconds);
  print_line(date_string, 0, 15, 2);
  print_line(time_string, 0, 0, 2);
}

//TIME RELATED FUNCTIONS
//update the time using wifi
void update_time() {

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char day_str[8];
  char hour_str[8];
  char min_str[8];
  char sec_str[8];
  char month_str[8];
  char year_str[8];

  strftime(day_str, 8, "%d", &timeinfo);
  strftime(sec_str, 8, "%S", &timeinfo);
  strftime(hour_str, 8, "%H", &timeinfo);
  strftime(min_str, 8, "%M", &timeinfo);

  strftime(month_str, 8, "%m", &timeinfo);
  strftime(year_str, 8, "%Y", &timeinfo);
  

  days = atoi(day_str);
  hours = atoi(hour_str);
  minutes = atoi(min_str);
  seconds = atoi(sec_str);
  month=atoi(month_str);
  year=atoi(year_str); 
}

//ringing the alarm
void ring_alarm(void) {
  display.clearDisplay();
  print_line("Medicine Time!!", 0, 0, 2);

  //light an LED
  digitalWrite(LED_1, HIGH);
  bool break_happened = false;
  //ring the BUZZER
  while (digitalRead(PB_CANCEL) == HIGH && break_happened == false) {
    for (int i = 0; i < n_notes ; i++) {
      if (digitalRead(PB_CANCEL) == LOW) {
        delay(200);
        break_happened = true;
        break;
      }
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }
  }
  delay(200);
  digitalWrite(LED_1, LOW);
}

void update_time_with_check_alarm(void) {
  display.clearDisplay();
  update_time();
  print_time_now();

  //check for alarms
  if (alarm_enabled) {
    for (int i = 0; i < n_alarms; i++) {
      if (alarm_triggered[i] == false && alarm_hours[i] == hours && alarm_minutes[i] == minutes) {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}


//BUTTON HANDLING FUNCTIONS
//pressing the buttons
int wait_for_button_press() {

  while (true) {
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }
    else if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }
    else if (digitalRead(PB_OK) == LOW) {
      delay(200);
      
      return PB_OK;
    }
    else if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      
      return PB_CANCEL;
    }
    update_time();
  }
}

//defining the menu
void go_to_menu() {
  while (true) {
    display.clearDisplay();
    print_line(modes[current_mode], 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      current_mode += 1;
      current_mode = current_mode % max_modes;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
     
      run_mode(current_mode);
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      
      current_mode = 0;
      break;
    }
  }
}


//MENU RELATED FUNCTIONS
//setting time zone - for Sri Lanka - 5h 30 min
void set_time_zone() {
  int temp_hour = offset_hours;
  while (true) {
    display.clearDisplay();
    print_line("Enter offset hours : " + String(temp_hour), 0, 0, 2);    //set hour
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      temp_hour += 1;
      temp_hour = temp_hour % 12;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      temp_hour -= 1;
      if (temp_hour < -12) {
        temp_hour = 12;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      UTC_OFFSET += temp_hour * 3600;
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      display.clearDisplay();
      print_line("Cancelled.", 0, 0, 2);
      delay(500);
      break;
    }
  }
  //set min
  int temp_minute = offset_minutes;
  while (true) {
    display.clearDisplay();
    print_line("Enter offset minutes : " + String(temp_minute), 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP||pressed == PB_DOWN) {
      delay(200);
      temp_minute += 1;
      temp_minute = temp_minute % 60;
    }
  
    else if (pressed == PB_OK) {
      delay(200);
      UTC_OFFSET += temp_minute * 60;
      
      display.clearDisplay();
      print_line("Time Zone is Set", 0, 0, 2);
      successSound();
      delay(1000);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      display.clearDisplay();
      print_line("Cancelled operation", 0, 0, 2);
      delay(500);
      break;
    }
  }
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
}

//setting the alarms
void set_alarm(int alarm) {
  //set hour
  int temp_hour = alarm_hours[alarm];
  while (true) {
    display.clearDisplay();
    print_line("Enter hour : " + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      delay(200);
      temp_hour += 1;
      temp_hour = temp_hour % 24;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      temp_hour -= 1;
      if (temp_hour < 0) {
        temp_hour = 23;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      alarm_hours[alarm] = temp_hour;
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      display.clearDisplay();
      print_line("Cancelled.", 0, 0, 2);
      delay(500);
      break;
    }
  }

  //set min
  int temp_minute = alarm_minutes[alarm];
  while (true) {
    display.clearDisplay();
    print_line("Enter minute : " + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      delay(200);
      temp_minute += 1;
      temp_minute = temp_minute % 60;

    }
    else if (pressed == PB_DOWN) {
      delay(200);
      temp_minute -= 1;
      if (temp_minute < 0) {
        temp_minute = 59;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      alarm_minutes[alarm] = temp_minute;
      display.clearDisplay();
      print_line("Alarm Set.", 0, 0, 2);
      alarm_enabled = true;
      successSound();
      delay(1000);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      display.clearDisplay();
      print_line("Cancelled.", 0, 0, 2);
      delay(500);
      break;
    }
  }
}

void disable_alarm() {
  display.clearDisplay();
  
  print_line("Enter alarm to disable:" + String(alarm_num), 0, 0, 2);
  int pressed = wait_for_button_press();
  while (true) {
    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      delay(200);
      alarm_num = (alarm_num + 1) % n_alarms;
      display.clearDisplay();
      print_line("Enter alarm to disable:" + String(alarm_num + 1), 0, 0, 2);
    } else if (pressed == PB_DOWN) {
      delay(200);
      alarm_num = (alarm_num - 1 + n_alarms) % n_alarms;
      display.clearDisplay();
      print_line("Enter alarm to disable:" + String(alarm_num + 1), 0, 0, 2);
    } else if (pressed == PB_OK) {
      delay(200);
      alarm_enabled = false;
      alarm_triggered[alarm_num] = false;
      display.clearDisplay();
      print_line("Alarm " + String(alarm_num + 1) + " is Disabled", 0, 0, 2);
      successSound();
      delay(1000);
      break;
    } else if (pressed == PB_CANCEL) {
      delay(200);
      display.clearDisplay();
      print_line("Cancelled.", 0, 0, 2);
      delay(500);
      break;
    }
  }
}

//defining the modes
void run_mode(int mode) {
  int temp_hour = 0;
  int temp_minute = 0;

  //set time zone
  if (mode == 0) {
    set_time_zone();
  }

  //set alarm
  else if (mode == 1 || mode == 2 || mode == 3) {
    set_alarm(mode - 1);
  }

  //Disable Alarms
  else if (mode == 4) {
    disable_alarm();
  }
}

//getting the humidity and temperature values
void check_temp(void) {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  delay(100);
  temp_value = String(data.temperature, 2);
  humid_value = String(data.humidity, 1);
  temp_value.toCharArray(temp_ar,6);
  humid_value.toCharArray(humid_ar,6);
  
  

  if (data.temperature > 32) {
    temp_ok = false;
    temp_value = "HIGH";
    warningSound();
  }
  else if (data.temperature < 26) {
    temp_ok = false;
    temp_value = "LOW  ";
    warningSound();
  }else{
    temp_ok = true;
  }

  if (data.humidity > 80) {
    humid_ok = false;
    humid_value = "HIGH";
    warningSound();

  }
  else if (data.humidity < 60) {
    humid_ok = false;
    humid_value = "LOW";
    warningSound();
  }
  else{
    humid_ok = true;
  }

  print_line("Temperature : " + temp_value + "  Humidity : " + humid_value, 0, 40, 1);
  delay(500);
}


//SOUND RELATED FUNCTIONS
//defining the tones
void successSound(){
  digitalWrite(LED_1, HIGH);  // Turn on LED
  tone(BUZZER, D);  // Turn on tone
  delay(200);
  digitalWrite(LED_1, LOW); // Turn off LED
  noTone(BUZZER); // Turn off the tone
  delay(200);
}

void openingSound(){
  tone(BUZZER, C);  // Turn on tone
  delay(200);
  noTone(BUZZER);  // Turn off tone
  delay(200);
  tone(BUZZER, E);  // Turn on tone
  delay(200);
  noTone(BUZZER); // Turn off the tone
}

void warningSound(){
  digitalWrite(LED_2, HIGH);  // Turn on LED
  tone(BUZZER, G);  // Turn on tone
  delay(200);
  digitalWrite(LED_2, LOW);  // Turn off LED
  noTone(BUZZER);  // Turn off tone
  delay(200);
  digitalWrite(LED_2, HIGH);  // Turn on LED
  tone(BUZZER, G);  // Turn on tone
  delay(200);
  digitalWrite(LED_2, LOW); // Turn off LED
  noTone(BUZZER); // Turn off the tone
  delay(200);
  digitalWrite(LED_2, HIGH);  // Turn on LED
  tone(BUZZER, G);  // Turn on tone
  delay(200);
  digitalWrite(LED_2, LOW); // Turn off LED
  noTone(BUZZER); // Turn off the tone
}