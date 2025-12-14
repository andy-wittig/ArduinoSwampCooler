#include <dht11.h>
#include <LiquidCrystal.h>
#include <Stepper.h>
#include <Wire.h>
#include <RTClib.h>

//------------------------
//--Swamp Cooler Project--
//Written by Andrew Wittig
//For Final CPE301 Project
//Dated - 12/12/2025
//------------------------

//---Time---
RTC_DS3231 rtc;

String GetTime()
{
  char time[32];
  DateTime curTime = rtc.now();
  sprintf(time, "%02d:%02d:%02d %02d/%02d/%04d",
              curTime.hour(), curTime.minute(), curTime.second(),
              curTime.day(), curTime.month(), curTime.year());
  return String(time);
}

//---States---
enum MACHINE_STATE {
  DISABLED,
  IDLE,
  RUNNING,
  ERROR
};
MACHINE_STATE currentState = IDLE;

enum LED_NAME
{
  RED,
  YELLOW,
  GREEN,
  BLUE
};

//---LCD---
const int rs = 22, en = 24, d4 = 29, d5 = 27, d6 = 25, d7 = 23;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

//---Stepper Motor---
#define STEPPER_PIN_1 9
#define STEPPER_PIN_2 10
#define STEPPER_PIN_3 11
#define STEPPER_PIN_4 12

#define STEPS 64
Stepper stepper(STEPS, STEPPER_PIN_1, STEPPER_PIN_3, STEPPER_PIN_2, STEPPER_PIN_4);

//---3-6v Motor---
#define ENABLE 5
#define DIRA 3
#define DIRB 4
const int FAN_SPEED = 100;

//---Potentiometer---
int potPin = A2;
int potVal = 0;

//---Water Sensor---
int waterPin = A0;
int waterLevel = 0;
const int waterThreshhold = 240;

//---Temp & Humidity---
#define DHT11PIN 7
dht11 DHT11;

const float tempThreshhold = 25; //In degrees celcius
float tempInCelcius = 0;
float humidity = 0;

//---LEDs---
#define RED_LED_PIN 30
#define YELLOW_LED_PIN 31
#define GREEN_LED_PIN 32
#define BLUE_LED_PIN 33

//---Buttons---
#define START_BUTTON_PIN 18
volatile bool buttonState = false;
int lastButtonState = 0;

//---UART---
#define RDA 0x80
#define TBE 0x20
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;

//---ADC---
volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

//---Function Definitions---
void SwitchState(MACHINE_STATE newState);
void ActivateLed(LED_NAME name);
void HandleStepper();
void TempAndHumToLCD();
void StopFan();
void StartFan();
void StartButtonPressed();
void HandleMachineDisabled();
void HandleMachineIdle();
void HandleMachineRunning();
void HandleMachineError();

void setup()
{
  //---Time---
  Wire.begin();
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));

  //---Serial & Analog---
  U0init(9600);
  adc_init();

  //---LCD---
  lcd.begin(16, 2); //Columns and rows

  //Arduino Mega Pinout

  //---Stepper Motor---
  //pinMode(STEPPER_PIN_1, OUTPUT);
  DDRH |= (1 << PH6);
  //pinMode(STEPPER_PIN_2, OUTPUT);
  DDRB |= (1 << PB4);
  //pinMode(STEPPER_PIN_3, OUTPUT);
  DDRB |= (1 << PB5);
  //pinMode(STEPPER_PIN_4, OUTPUT);
  DDRB |= (1 << PB6);

  //---3-6v Motor---
  //pinMode(ENABLE, OUTPUT);
  DDRE |= (1 << PE3);
  //pinMode(DIRA, OUTPUT);
  DDRE |= (1 << PE5);
  //pinMode(DIRB, OUTPUT);
  DDRG |= (1 << PG5);

  //---LEDs---
  //pinMode(RED_LED_PIN, OUTPUT);
  DDRC |= (1 << PC7);
  //pinMode(YELLOW_LED_PIN, OUTPUT);
  DDRC |= (1 << PC6);
  //pinMode(GREEN_LED_PIN, OUTPUT);
  DDRC |= (1 << PC5);
  //pinMode(BLUE_LED_PIN, OUTPUT);
  DDRC |= (1 << PC4);

  //---Buttons---
  //pinMode(START_BUTTON_PIN, INPUT);
  DDRD &= ~(1 << PD3);
  PORTD |= (1 << PD3); //pull-up
  attachInterrupt(digitalPinToInterrupt(START_BUTTON_PIN), StartButtonPressed, FALLING);
}

//---UART Functions---
void U0init(unsigned long U0baud)
{
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0  = tbaud;
}
unsigned char U0kbhit()
{
  bool RDAStatus = ((*myUCSR0A & RDA) != 0);
  return RDAStatus;
}
unsigned char U0getchar()
{
  return *myUDR0;
}
void U0putchar(unsigned char U0pdata)
{
  while ((*myUCSR0A & TBE) == 0);
  *myUDR0 = U0pdata;
}
void PrintMessage(const char message[])
{
   int messageLength = strlen(message);
   for (int i = 0; i < messageLength; i++)
   {
      U0putchar(message[i]);
   }
   U0putchar('\n');
}
//---------------------

void SwitchState(MACHINE_STATE  newState)
{
  currentState = newState;
  String msg = String("State has been switched! ") + GetTime();
  PrintMessage(msg.c_str());
}

void ActivateLed(LED_NAME name)
{
  //digitalWrite(RED_LED_PIN, name == RED ? HIGH : LOW);
  //digitalWrite(YELLOW_LED_PIN, name == YELLOW ? HIGH : LOW);
  //digitalWrite(GREEN_LED_PIN, name == GREEN ? HIGH : LOW);
  //digitalWrite(BLUE_LED_PIN, name == BLUE ? HIGH : LOW);

  if (name == RED) { PORTC |= (1 << PC7); }
  else { PORTC &= ~(1 << PC7); }
  if (name == YELLOW) { PORTC |= (1 << PC6); }
  else { PORTC &= ~(1 << PC6); }
  if (name == GREEN) { PORTC |= (1 << PC5); }
  else { PORTC &= ~(1 << PC5); }
  if (name == BLUE) { PORTC |= (1 << PC4); }
  else { PORTC &= ~(1 << PC4); }
}

void HandleMachineDisabled()
{ 
  //Turn on LED
  ActivateLed(YELLOW);
  //Turn off fan
  StopFan();
  //LCD Message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Disabled :(");
}
void HandleMachineIdle()
{ 
  //Change state condition
  if (tempInCelcius > tempThreshhold)
  {
    SwitchState(RUNNING);
    return;
  }
  if (waterLevel <= waterThreshhold)
  {
    SwitchState(ERROR);
    return;
  }

  //Display Temp and Humidity
  TempAndHumToLCD();
  //Turn on LED
  ActivateLed(GREEN);
  //Turn off fan
  StopFan();
}
void HandleMachineRunning() 
{ 
  //Change state condition
  if (tempInCelcius <= tempThreshhold)
  {
    StopFan();
    SwitchState(IDLE);
    return;
  }
  if (waterLevel < waterThreshhold)
  {
    StopFan();
    SwitchState(ERROR);
    return;
  }

  //Display Temp and Humidity
  TempAndHumToLCD();
  //Turn on LED
  ActivateLed(BLUE);
  //On entry start fan motor
  StartFan();
}
void HandleMachineError() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Water level is");
  lcd.setCursor(0, 1);
  lcd.print("too low! :O");
  //Turn on LED
  ActivateLed(RED);
}

void TempAndHumToLCD()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Humidity %");
  lcd.print(humidity, 2);
  lcd.setCursor(0, 1);
  lcd.print("Temp (C): ");
  lcd.print(tempInCelcius, 2);
}

void StopFan()
{
  //digitalWrite(DIRA, LOW);
  PORTE &= ~(1 << PE5);
  //digitalWrite(DIRB, LOW);
  PORTG &= ~(1 << PG5);
  analogWrite(ENABLE, LOW); //<-- Okay to use here.

  String msg = String("Fan Stopped: ") + GetTime();
  PrintMessage(msg.c_str());
}
void StartFan()
{
  //digitalWrite(DIRA, HIGH);
  PORTE |= (1 << PE5);
  //digitalWrite(DIRB, LOW);
  PORTG &= ~(1 << PG5);
  analogWrite(ENABLE, FAN_SPEED); //<-- Okay to use here.

  String msg = String("Fan Started: ") + GetTime();
  PrintMessage(msg.c_str());
}

volatile unsigned long lastInterruptTime = 0;

void StartButtonPressed()
{
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > 200) //Debounce
  {
    buttonState = true;
  }
  lastInterruptTime = interruptTime;
}

//---ADC Functions---
void adc_init()
{
  *my_ADMUX = (1 << 6); 
  *my_ADCSRA = (1 << 7) | (1 << 2) | (1 << 1) | (1 << 0); 
  *my_ADCSRB = 0x00;
}

//Reads analog data from a specific ADC channel
unsigned int adc_read(int channelNum)
{
  channelNum &= 0x07;
  *my_ADMUX &= 0xE0;
  *my_ADCSRB &= 0xF7;
  *my_ADMUX |= channelNum;
  *my_ADCSRA |= (1 << 6);

  while ((*my_ADCSRA & (1 << 6)) != 0);

  unsigned int val = *my_ADC_DATA;
  return val;
}
//---End ADC---

bool allowMonitoring = true;
bool allowVentControl = true;
unsigned long lastUpdateTime = 0;
int updateDelay = 10; //One minute delay for main loop processing

void loop() 
{
  //while (U0kbhit() == 0) { };

  unsigned long now = millis();

  if (now - lastUpdateTime >= updateDelay)
  {
    lastUpdateTime = now;
    //---Sensor Readings---
    if (allowMonitoring)
    {
      //potVal = analogRead(potPin);
      potVal = adc_read(2);
      //waterLevel = analogRead(waterPin);
      waterLevel = adc_read(0);
      
      char msg[32];
      snprintf(msg, sizeof(msg), "Water level: %d", waterLevel);
      PrintMessage(msg);

      int readDHT11 = DHT11.read(DHT11PIN);
      tempInCelcius = (float)DHT11.temperature;
      humidity = (float)DHT11.humidity;
    }

    if (buttonState)
    {
      buttonState = false;

      PrintMessage("Button Pressed!");
      switch (currentState)
      {
        case (DISABLED):
          SwitchState(IDLE);
          break;
        case (IDLE):
          SwitchState(DISABLED);
          break;
        case (RUNNING):
          StopFan();
          SwitchState(DISABLED);
          break;
        case (ERROR):
          SwitchState(DISABLED);
          break;
      }
    }

    //State Machine
    switch (currentState)
    {
      case (DISABLED):
        HandleMachineDisabled();
        allowMonitoring = false;
        allowVentControl = false;
        break;
      case (IDLE):
        HandleMachineIdle();
        allowMonitoring = true;
        allowVentControl = true;
        break;
      case (RUNNING):
        HandleMachineRunning();
        allowMonitoring = true;
        allowVentControl = true;
        break;
      case (ERROR):
        HandleMachineError();
        allowMonitoring = true;
        allowVentControl = true;
        break;
    }
  }

  if (allowVentControl)
  {
   HandleStepper();
  }
}

unsigned long lastStepTime = 0;
int stepDelay = 2;
int stepPos = 0;

void HandleStepper()
{
  int currentStep = map(potVal, 0, 1023, 0, 100);
  unsigned long now = millis();

  if (now - lastStepTime >= stepDelay)
  { //Runs async to main loop
    lastStepTime = now;
    stepper.setSpeed(200);

    if (currentStep < 40) 
    {
      stepPos--;
      String msg = "Moving vent left: " + String(stepPos) + GetTime();
      PrintMessage(msg.c_str());
      stepper.step(10);
    }
    else if (currentStep > 60) 
    { 
      stepPos++;
      String msg = "Moving vent right: " + String(stepPos) + GetTime();
      PrintMessage(msg.c_str());
      stepper.step(-10);
    }
  }
}