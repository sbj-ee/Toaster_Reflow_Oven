#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PID_v1.h>
#include "Adafruit_MAX31855.h"


// type definitions
typedef enum REFLOW_STATE
{
  REFLOW_STATE_IDLE,
  REFLOW_STATE_PREHEAT,
  REFLOW_STATE_SOAK,
  REFLOW_STATE_REFLOW,
  REFLOW_STATE_COOL,
  REFLOW_STATE_COMPLETE,
  REFLOW_STATE_TOO_HOT,
  REFLOW_STATE_ERROR
} reflowState_t;

typedef enum REFLOW_STATUS
{
  REFLOW_STATUS_OFF,
  REFLOW_STATUS_ON
} reflowStatus_t;


// constants
#define TEMPERATURE_ROOM        50

////// non-RoHS
#define TEMPERATURE_SOAK_MIN   100
#define TEMPERATURE_SOAK_MAX   150
#define TEMPERATURE_REFLOW_MAX 250    // was 230 - 250 to reclaim components

///// RoHS Values
// #define TEMPERATURE_SOAK_MIN   150
// #define TEMPERATURE_SOAK_MAX   200
// #define TEMPERATURE_REFLOW_MAX 250
#define TEMPERATURE_COOL_MIN   100
#define SENSOR_SAMPLING_TIME   1000
#define SOAK_TEMPERATURE_STEP  3.5   // was 5
#define SOAK_MICRO_PERIOD      9000

// PID parameters
// preheat stage
#define PID_KP_PREHEAT     100
#define PID_KI_PREHEAT     0.025
#define PID_KD_PREHEAT     20

// soaking stage
#define PID_KP_SOAK        300
#define PID_KI_SOAK        0.05
#define PID_KD_SOAK        250

// reflow stage
#define PID_KP_REFLOW      300
#define PID_KI_REFLOW      0.05
#define PID_KD_REFLOW      350

#define PID_SAMPLE_TIME    1000


// thermocouple pins
const int MAXDO  = 3;
const int MAXCS  = 4;
const int MAXCLK = 5;

// other pins
const int preheatLedPin = 10;
const int soakLedPin    = 11;
const int reflowLedPin  = 12;
const int coolingLedPin = 13;
const int errorLedPin   = 8;

const int startButtonPin = 7;
const int buzzerPin      = 9;
const int ssrPin         = 6;

// PID control variables
double setpoint;
double input;
double output;
double kp = PID_KP_PREHEAT;
double ki = PID_KI_PREHEAT;
double kd = PID_KD_PREHEAT;
int windowSize;
unsigned long windowStartTime;
unsigned long nextCheck;
unsigned long nextRead;
unsigned long timerSoak;

// reflow oven controller state machine
reflowState_t reflowState;

// reflow over controller status
reflowStatus_t reflowStatus;

// seconds timer
int timerSeconds;

// specify PID control interface
PID reflowOvenPID(&input, &output, &setpoint, kp, ki, kd, DIRECT);

// initialize thermocouple
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

// initialize display (128x64, I2C address 0x3C)
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(128, 64, &Wire, -1);




void setup() {
  int i = 0;
  
  pinMode(preheatLedPin, OUTPUT);
  pinMode(soakLedPin, OUTPUT);
  pinMode(reflowLedPin, OUTPUT);
  pinMode(coolingLedPin, OUTPUT);
  pinMode(errorLedPin, OUTPUT);
  pinMode(startButtonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ssrPin, OUTPUT);

  turnSsrOff();   // ensure the SSR is off

  // serial communications
  Serial.begin(57600);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.clearDisplay();
  display.display();

  for (i = 0; i < 10; i++) {
    beep();
    delay(1000);
  }


  // set window size
  windowSize = 2000;

  // initialize time keeping variable
  nextCheck = millis();

  // initialize thermocouple reading
  nextRead = millis();
  
}

void loop()
{
  unsigned long now;

  // time to read thermocouple
  if (millis() - nextRead >= SENSOR_SAMPLING_TIME)
  {
    // read the thermocouple next sampling period
    nextRead += SENSOR_SAMPLING_TIME;

    // read current temperature
    input = thermocouple.readCelsius();

    // if thermocouple problem
    if (isnan(input))
    {
      // an illegal operation
      reflowState = REFLOW_STATE_ERROR;
      reflowStatus = REFLOW_STATUS_OFF;
    }
  }

  if (millis() - nextCheck >= 1000)
  {
    // check input in the next seconds
    nextCheck += 1000;

    // if reflow process is on going
    if (reflowStatus == REFLOW_STATUS_ON)
    {
      timerSeconds++;

      // send temperature and timestamp to serial
      Serial.print(timerSeconds);
      Serial.print(" ");
      Serial.print(setpoint);
      Serial.print(" ");
      Serial.print(input);
      Serial.print(" ");
      Serial.println(output);
    }
    else
    {
      // maybe turn off leds
    }

    updateDisplay();
  }

  switch(reflowState)
  {
    case REFLOW_STATE_IDLE:
      // if oven temperature is still above room temperature
      if (input >= TEMPERATURE_ROOM)
      {
        reflowState = REFLOW_STATE_TOO_HOT;
      }
      else if (digitalRead(startButtonPin) == LOW)
      {
        // start reflow process

        // send header for csv file
        Serial.println("Time Setpoint Input Output");

        // initialize seconds timer for serial debug info
        timerSeconds = 0;

        // initialize PID control window starting time
        windowStartTime = millis();

        // ramp up to minimum soak time
        setpoint = TEMPERATURE_SOAK_MIN;

        // Tell the PID to range between 0 and the full window size
        reflowOvenPID.SetOutputLimits(0, windowSize);

        reflowOvenPID.SetSampleTime(PID_SAMPLE_TIME);

        // Reset PID tunings to preheat values for new cycle
        reflowOvenPID.SetTunings(PID_KP_PREHEAT, PID_KI_PREHEAT, PID_KD_PREHEAT);

        // Turn the PID on
        reflowOvenPID.SetMode(AUTOMATIC);

        // Proceed to preheat stage
        reflowState = REFLOW_STATE_PREHEAT; 
      }
      break;

    case REFLOW_STATE_PREHEAT:
        turnOnPreheatLed();
        reflowStatus = REFLOW_STATUS_ON;

        // if minimum soak temperature is achieved
        if (input >= TEMPERATURE_SOAK_MIN)
        {
          // chop soaking period into smaller sub-period
          timerSoak = millis() + SOAK_MICRO_PERIOD;

          // set agressive PID parameters for soaking ramp
          reflowOvenPID.SetTunings(PID_KP_SOAK, PID_KI_SOAK, PID_KD_SOAK);

          // ramp up to first section of soaking temperature
          setpoint = TEMPERATURE_SOAK_MIN + SOAK_TEMPERATURE_STEP;

          // proceed to soaking state
          reflowState = REFLOW_STATE_SOAK;
          turnOffPreheatLed();
        }
        break;

  case REFLOW_STATE_SOAK:
    turnOnSoakLed();
         
    // If micro soak temperature is achieved       
    if (millis() > timerSoak) {
      timerSoak = millis() + SOAK_MICRO_PERIOD;
      
      // Increment micro setpoint
      setpoint += SOAK_TEMPERATURE_STEP;
      
      if (setpoint > TEMPERATURE_SOAK_MAX) {
        // Set agressive PID parameters for reflow ramp
        reflowOvenPID.SetTunings(PID_KP_REFLOW, PID_KI_REFLOW, PID_KD_REFLOW);
      
        // Ramp up to first section of soaking temperature
        setpoint = TEMPERATURE_REFLOW_MAX;   
        
        // Proceed to reflowing state
        reflowState = REFLOW_STATE_REFLOW;
        turnOffSoakLed();
      }
    }
    break; 

  case REFLOW_STATE_REFLOW:
    turnOnReflowLed();
    
    // We need to avoid hovering at peak temperature for too long
    // Crude method that works like a charm and safe for the components
    if (input >= (TEMPERATURE_REFLOW_MAX - 5)) {

      // Set PID parameters for cooling ramp
      reflowOvenPID.SetTunings(PID_KP_REFLOW, PID_KI_REFLOW, PID_KD_REFLOW);

      // Ramp down to minimum cooling temperature
      setpoint = TEMPERATURE_COOL_MIN;   
      
      // Proceed to cooling state
      reflowState = REFLOW_STATE_COOL;
      turnOffReflowLed(); 
    }
    break;   

  case REFLOW_STATE_COOL:
    turnOnCoolingLed();
    
    // If minimum cool temperature is achieve       
    if (input <= TEMPERATURE_COOL_MIN) {
      // Turn off reflow process
      reflowStatus = REFLOW_STATUS_OFF;                
    
      // Proceed to reflow Completion state
      reflowState = REFLOW_STATE_COMPLETE;
      turnOffCoolingLed(); 
    }         
    break;    

  case REFLOW_STATE_COMPLETE:
    buzzer();
    reflowState = REFLOW_STATE_IDLE; 
    break;
        

  case REFLOW_STATE_TOO_HOT:
        
    // If oven temperature drops below room temperature
    if (input < TEMPERATURE_ROOM)
    {
      // Ready to reflow
      reflowState = REFLOW_STATE_IDLE;
    }
    break;

  case REFLOW_STATE_ERROR:
    turnOnErrorLed();
    
    // If thermocouple problem is still present
    if (isnan(input)) {
      // Wait until thermocouple wire is connected
    }
    else
    {
      // Clear to perform reflow process
      reflowState = REFLOW_STATE_IDLE;
      turnOffErrorLed(); 
    }
    break;    
      
  } // end of switch

  // PID computation and SSR control
  if (reflowStatus == REFLOW_STATUS_ON)
  {
    now = millis();
    reflowOvenPID.Compute();

    if((now - windowStartTime) > windowSize)
    { 
      // Time to shift the Relay Window
      windowStartTime += windowSize;
    }
    if(output > (now - windowStartTime)) {
      // Turn relay on to heat oven
      digitalWrite(ssrPin, HIGH);
    } else {
      // Turn relay off to turn oven off
      digitalWrite(ssrPin, LOW);
    }   
  } else {
  // Reflow oven process is off, ensure oven is off
    digitalWrite(ssrPin, LOW);
  }
    
}


  
 



const char* stateString() {
  switch (reflowState) {
    case REFLOW_STATE_IDLE:     return "READY";
    case REFLOW_STATE_PREHEAT:  return "PREHEAT";
    case REFLOW_STATE_SOAK:     return "SOAK";
    case REFLOW_STATE_REFLOW:   return "REFLOW";
    case REFLOW_STATE_COOL:     return "COOL";
    case REFLOW_STATE_COMPLETE: return "COMPLETE";
    case REFLOW_STATE_TOO_HOT:  return "TOO HOT";
    case REFLOW_STATE_ERROR:    return "ERROR";
    default:                    return "";
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // top row: state name left, elapsed time right
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(stateString());
  if (reflowStatus == REFLOW_STATUS_ON) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", timerSeconds);
    display.setCursor(128 - (int)strlen(buf) * 6, 0);
    display.print(buf);
  }

  // middle: current temp / setpoint in larger text
  display.setTextSize(2);
  display.setCursor(0, 16);
  if (isnan(input)) {
    display.print("ERR");
  } else {
    display.print((int)input);
  }
  display.print("/");
  display.print((int)setpoint);
  display.print("C");

  // temperature progress bar (rows 38–47)
  // fills left-to-right from 0 °C to TEMPERATURE_REFLOW_MAX
  const int barY     = 38;
  const int barH     = 10;
  const int innerW   = 126;   // 128 - 2px border

  display.drawRect(0, barY, 128, barH, SSD1306_WHITE);

  if (!isnan(input) && input > 0) {
    int fill = (int)((input / TEMPERATURE_REFLOW_MAX) * innerW);
    if (fill > innerW) fill = innerW;
    if (fill > 0) display.fillRect(1, barY + 1, fill, barH - 2, SSD1306_WHITE);
  }

  // stage-boundary ticks (INVERSE punches through fill so they stay visible)
  int xSoakMin = 1 + (int)((float)TEMPERATURE_SOAK_MIN / TEMPERATURE_REFLOW_MAX * innerW);
  int xSoakMax = 1 + (int)((float)TEMPERATURE_SOAK_MAX / TEMPERATURE_REFLOW_MAX * innerW);
  display.drawFastVLine(xSoakMin, barY + 1, barH - 2, SSD1306_INVERSE);
  display.drawFastVLine(xSoakMax, barY + 1, barH - 2, SSD1306_INVERSE);

  // four stage labels in equal quarters (32 px each); active state inverted
  display.setTextSize(1);
  int labelY = barY + barH + 2;

  if (reflowState == REFLOW_STATE_PREHEAT) {
    display.fillRect(6,  labelY - 1, 20, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(7,   labelY); display.print("PRE");

  if (reflowState == REFLOW_STATE_SOAK) {
    display.fillRect(35, labelY - 1, 26, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(36,  labelY); display.print("SOAK");

  if (reflowState == REFLOW_STATE_REFLOW) {
    display.fillRect(67, labelY - 1, 26, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(68,  labelY); display.print("RFLO");

  if (reflowState == REFLOW_STATE_COOL) {
    display.fillRect(99, labelY - 1, 26, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(100, labelY); display.print("COOL");

  display.setTextColor(SSD1306_WHITE);

  display.display();
}

void turnOnPreheatLed()
{
  digitalWrite(preheatLedPin, HIGH);
}

void turnOffPreheatLed()
{
  digitalWrite(preheatLedPin, LOW); 
}

void turnOnSoakLed()
{
  digitalWrite(soakLedPin, HIGH);
}


void turnOffSoakLed()
{
  digitalWrite(soakLedPin, LOW); 
}


void turnOnReflowLed()
{
  digitalWrite(reflowLedPin, HIGH); 
}

void turnOffReflowLed()
{
  digitalWrite(reflowLedPin, LOW); 
}

void turnOnCoolingLed()
{
  digitalWrite(coolingLedPin, HIGH);
}

void turnOffCoolingLed()
{
  digitalWrite(coolingLedPin, LOW); 
}

void turnOnErrorLed()
{
  digitalWrite(errorLedPin, HIGH);
  beep();
  delay(250);
  beep();
}


void turnOffErrorLed()
{
  digitalWrite(errorLedPin, LOW); 
}


void turnSsrOff()
{
  digitalWrite(ssrPin, LOW);
}


void buzzer()
{
   int i = 0;
 
   for (i = 0; i < 1000; i++) {
      digitalWrite(buzzerPin, HIGH);
      delayMicroseconds(130);
      digitalWrite(buzzerPin, LOW);
      delayMicroseconds(400);
   }  
  
  digitalWrite(buzzerPin, LOW);
  
}


void beep()
{
   int i = 0;
 
   for (i = 0; i < 200; i++) {
      digitalWrite(buzzerPin, HIGH);
      delayMicroseconds(130);
      digitalWrite(buzzerPin, LOW);
      delayMicroseconds(400);
   }  
  
  digitalWrite(buzzerPin, LOW);

}
