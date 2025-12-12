#include <dht11.h>
#include <LiquidCrystal.h>
#include <Stepper.h>
#include <Wire.h>
#include <RTClib.h>

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
const int waterThreshhold = 10;

//---Temp & Humidity---
#define DHT11PIN 7
dht11 DHT11;

const float tempThreshhold = 24; //In degrees celcius
float tempInCelcius = 0;
float humidity = 0;

//---LEDs---
#define RED_LED_PIN 30
#define YELLOW_LED_PIN 31
#define GREEN_LED_PIN 32
#define BLUE_LED_PIN 33

//---Buttons---
#define START_BUTTON_PIN 8
int buttonState = 0;
int lastButtonState = 0;

//---UART
#define RDA 0x80
#define TBE 0x20
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;

//---Function Definitions
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

  U0init(9600); //Init serial connection

  //---LCD---
  lcd.begin(16, 2); //Columns and rows

  //---Stepper Motor---
  pinMode(STEPPER_PIN_1, OUTPUT); //TODO: Cannot use pinmode!
  pinMode(STEPPER_PIN_2, OUTPUT);
  pinMode(STEPPER_PIN_3, OUTPUT);
  pinMode(STEPPER_PIN_4, OUTPUT);

  //---3-6v Motor---
  pinMode(ENABLE, OUTPUT);
  pinMode(DIRA, OUTPUT);
  pinMode(DIRB, OUTPUT);

  //---LEDs---
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);

  //---Buttons---
  pinMode(START_BUTTON_PIN, INPUT);
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
  //TODO: Can't use digital write here:
  digitalWrite(RED_LED_PIN, name == RED ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, name == YELLOW ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, name == GREEN ? HIGH : LOW);
  digitalWrite(BLUE_LED_PIN, name == BLUE ? HIGH : LOW);
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
  digitalWrite(DIRA, LOW); //TODO
  digitalWrite(DIRB, LOW); //TODO
  analogWrite(ENABLE, LOW); //<-- Okay to use here.

  String msg = String("Fan Stopped: ") + GetTime();
  PrintMessage(msg.c_str());
}
void StartFan()
{
  digitalWrite(DIRA, HIGH); //TODO
  digitalWrite(DIRB, LOW); //TODO
  analogWrite(ENABLE, FAN_SPEED); //<-- Okay to use here.

  String msg = String("Fan Started: ") + GetTime();
  PrintMessage(msg.c_str());
}

void StartButtonPressed()
{
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

bool allowMonitoring = true;
bool allowVentControl = true;
void loop() 
{
  while (U0kbhit() == 0) { };

  //---Sensor Readings---
  if (allowMonitoring)
  {
    potVal = analogRead(potPin); //TODO: canot use analogRead!
    waterLevel = analogRead(waterPin); //TODO: canot use analogRead!
    //TODO: Either interupt from comparator or via a sample using the ADC (cannot use ADC library)

    int readDHT11 = DHT11.read(DHT11PIN);
    tempInCelcius = (float)DHT11.temperature;
    humidity = (float)DHT11.humidity;
  }

  //Check for start button input
  buttonState = digitalRead(START_BUTTON_PIN); //TODO: Should be monitored using an ISR
  //can use attachInterrupt()

  if (buttonState == HIGH && lastButtonState == LOW) 
  {
    StartButtonPressed();
    delay(50); //Debounce TODO: Can't use delay, allowed to use the millis()
  }
  lastButtonState = buttonState;

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

  if (allowVentControl)
  {
    HandleStepper();
  }

  delay(60000); //Update every one minute TODO: Can't use delay
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