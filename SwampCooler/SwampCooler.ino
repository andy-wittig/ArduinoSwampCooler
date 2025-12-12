#include <dht11.h>
#include <LiquidCrystal.h>
#include <Stepper.h>
#include <Wire.h>
#include <RTClib.h>

//---Time
RTC_DS3231 rtc;
//-------

String GetTime()
{
  char time[32];
  DateTime curTime = rtc.now();
  sprintf(time, "%02d:%02d:%02d %02d/%02d/%02d", curTime.hour(), curTime.minute(), curTime.second(), curTime.day(), curTime.month(), curTime.year());
  return String(time);
}

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

//---LCD
const int rs = 22, en = 24, d4 = 29, d5 = 27, d6 = 25, d7 = 23;
//Pins on LCD Board:
//rs --> pin 4
//en --> pin 6
//d4 --> pin 11
//d5 --> pin 12
//d6 --> pin 13
//d7 --> pin 14
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
//-----

//---Stepper Motor
#define STEPPER_PIN_1 9
#define STEPPER_PIN_2 10
#define STEPPER_PIN_3 11
#define STEPPER_PIN_4 12

#define STEPS 64
Stepper stepper(STEPS, STEPPER_PIN_1, STEPPER_PIN_3, STEPPER_PIN_2, STEPPER_PIN_4);
//-----------------

//---3-6v Motor
#define ENABLE 5
#define DIRA 3
#define DIRB 4
//-------------

//---Potentiometer Setup
int potPin = A2;
int potVal = 0;
//---------------------

//---Water Sensor
int waterPin = A0;
int waterLevel = 0;
const int waterThreshhold = 212;
//--------------

//---Temp and Humidity
#define DHT11PIN 7
dht11 DHT11;

const float tempThreshhold = 24; //In degrees celcius
float tempInCelcius = 0;
float humidity = 0;
//-------------------

//---LEDs
#define RED_LED_PIN 30
#define YELLOW_LED_PIN 31
#define GREEN_LED_PIN 32
#define BLUE_LED_PIN 33
//-------

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
//-------

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
  //---Time
  Wire.begin();
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));

  U0init(9600); //Init serial connection

  //---LCD
  lcd.begin(16, 2); //Columns and rows

  //---Stepper Motor
  pinMode(STEPPER_PIN_1, OUTPUT);
  pinMode(STEPPER_PIN_2, OUTPUT);
  pinMode(STEPPER_PIN_3, OUTPUT);
  pinMode(STEPPER_PIN_4, OUTPUT);

  //---3-6v Motor
  pinMode(ENABLE, OUTPUT);
  pinMode(DIRA, OUTPUT);
  pinMode(DIRB, OUTPUT);

  //---LEDs
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);

  pinMode(START_BUTTON_PIN, INPUT);
}

//---UART Functions
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
//-------------

void SwitchState(MACHINE_STATE  newState)
{
  currentState = newState;
  String msg = String("State has been switched! ") + GetTime();
  PrintMessage(msg.c_str());
}

void ActivateLed(LED_NAME name)
{
  digitalWrite(RED_LED_PIN, name == RED ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, name == YELLOW ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, name == GREEN ? HIGH : LOW);
  digitalWrite(BLUE_LED_PIN, name == BLUE ? HIGH : LOW);
}

void HandleMachineDisabled() //COMPLETE: Don't allow vent control when disabled!
{ 
  //Turn on LED
  ActivateLed(YELLOW);

  //Turn off fan
  digitalWrite(DIRA, LOW);
  digitalWrite(DIRB, LOW);
  analogWrite(ENABLE, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Disabled :(");
}
void HandleMachineIdle() //TODO: Record timestamp of when states transition!
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
  digitalWrite(DIRA, LOW);
  digitalWrite(DIRB, LOW);
  analogWrite(ENABLE, LOW);
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
  lcd.print("too low!");

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
  digitalWrite(DIRA, LOW);
  digitalWrite(DIRB, LOW);
  analogWrite(ENABLE, LOW);

  String msg = String("Fan Stopped: ") + GetTime();
  PrintMessage(msg.c_str());
}
void StartFan()
{
  digitalWrite(DIRA, HIGH);
  digitalWrite(DIRB, LOW);
  analogWrite(ENABLE, 100);

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

  //---Sensor Readings
  if (allowMonitoring)
  {
    potVal = analogRead(potPin);
    //Serial.print("Potentiometer: "); 
    //Serial.println(potVal);

    waterLevel = analogRead(waterPin);
    //TODO: Either interupt from comparator or via a sample using the ADC (cannot use ADC library)
    //Serial.print("Water Level: "); 
    //Serial.println(waterLevel);

    int readDHT11 = DHT11.read(DHT11PIN);
    tempInCelcius = (float)DHT11.temperature;
    humidity = (float)DHT11.humidity;
    /*
    Serial.print("Humidity (%): ");
    Serial.println(humidity, 2);
    Serial.print("Temperature  (C): ");
    Serial.println(tempInCelcius, 2);
    */
  }
  //-----------

  //Check for start button input -- TODO: Should be monitored using and ISR
  buttonState = digitalRead(START_BUTTON_PIN);

  if (buttonState == HIGH && lastButtonState == LOW) 
  {
    StartButtonPressed();
    delay(50); //Debounce
  }
  lastButtonState = buttonState;

  //State Machine
  switch (currentState) //COMPLETE: Values should not be monitored when disabled.
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

  delay(60000); //COMPLETE: Updates every 1 minute!
}

unsigned long lastStepTime = 0;
int stepDelay = 2;
int stepPos = 0;

void HandleStepper()
{
  int currentStep = map(potVal, 0, 1023, 0, 100);
  unsigned long now = millis();

  if (now - lastStepTime >= stepDelay)
  {
    lastStepTime = now;
    stepper.setSpeed(200);

    //TODO: Stepper position must be reported to Serial. Provide data and time!
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