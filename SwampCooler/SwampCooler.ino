#include <dht11.h>
#include <LiquidCrystal.h>
#include <Stepper.h>

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

enum LED_NAME
{
  RED,
  YELLOW,
  GREEN,
  BLUE
};
//-------

#define START_BUTTON_PIN 8
int buttonState = 0;
int lastButtonState = 0;

void setup()
{
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

  //Init Serial Connection
  Serial.begin(9600);
}

enum MACHINE_STATE {
  DISABLED,
  IDLE,
  RUNNING,
  ERROR
};

MACHINE_STATE currentState = IDLE;

void HandleMachineDisabled() 
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
void HandleMachineIdle() 
{ 
  //Change state condition
  if (tempInCelcius > tempThreshhold)
  {
    currentState = RUNNING;
    return;
  }

  if (waterLevel <= waterThreshhold)
  {
    currentState = ERROR;
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
    currentState = IDLE;
    return;
  }

  if (waterLevel < waterThreshhold)
  {
    StopFan();
    currentState = ERROR;
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
  //Change state condition
  //Press reset button

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
}
void StartFan()
{
  digitalWrite(DIRA, HIGH);
  digitalWrite(DIRB, LOW);
  analogWrite(ENABLE, 100);
}

void ActivateLed(LED_NAME name)
{
  digitalWrite(RED_LED_PIN, name == RED ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, name == YELLOW ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, name == GREEN ? HIGH : LOW);
  digitalWrite(BLUE_LED_PIN, name == BLUE ? HIGH : LOW);
}

void StartButtonPressed()
{
  switch (currentState)
  {
    case (DISABLED):
      currentState = IDLE;
      break;
    case (IDLE):
      currentState = DISABLED;
      break;
    case (RUNNING):
      StopFan();
      currentState = DISABLED;
      break;
    case (ERROR):
      currentState = DISABLED;
      break;
  }
}

void loop() 
{
  //---Sensor Readings
  potVal = analogRead(potPin);
  //Serial.print("Potentiometer: "); 
  //Serial.println(potVal);

  waterLevel = analogRead(waterPin);
  Serial.print("Water Level: "); 
  Serial.println(waterLevel);

  int readDHT11 = DHT11.read(DHT11PIN);
  tempInCelcius = (float)DHT11.temperature;
  humidity = (float)DHT11.humidity;
  /*
  Serial.print("Humidity (%): ");
  Serial.println(humidity, 2);
  Serial.print("Temperature  (C): ");
  Serial.println(tempInCelcius, 2);
  */
  //-----------

  //Check for start button input
  buttonState = digitalRead(START_BUTTON_PIN);

  if (buttonState == HIGH && lastButtonState == LOW) 
  {
    StartButtonPressed();
    delay(50); //Debounce
  }
  lastButtonState = buttonState;

  //State Machine
  switch (currentState)
  {
    case (DISABLED):
      HandleMachineDisabled();
      break;
    case (IDLE):
      HandleMachineIdle();
      break;
    case (RUNNING):
      HandleMachineRunning();
      break;
    case (ERROR):
      HandleMachineError();
      break;
  }

  HandleStepper();

  //delay(50);
}

unsigned long lastStepTime = 0;
int stepDelay = 2; // milliseconds per step

void HandleStepper()
{
  int currentStep = map(potVal, 0, 1023, 0, 100);
  unsigned long now = millis();
  Serial.println(currentStep);

  if (now - lastStepTime >= stepDelay)
  {
    lastStepTime = now;
    stepper.setSpeed(200);

    if (currentStep < 40) 
    { 
      stepper.step(10);
    }
    else if (currentStep > 60) 
    { 
      stepper.step(-10);
    }
  }
}
