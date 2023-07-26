/*************************************************** 
  Control PWM fan using a rotary encoder and out RPM to 7 seg display.

  Designed specifically to work the following:
  - Ardiono pro micro
  - Adafruit LED 7-Segment backpacks 
    http://www.adafruit.com/products/878
  - Rotary encoder
    http://
  - PWM fan

  Used the following as references for writing this:
  https://create.arduino.cc/projecthub/tylerpeppy/25-khz-4-pin-pwm-fan-control-with-arduino-uno-3005a1
  https://lastminuteengineers.com/rotary-encoder-arduino-tutorial/
  https://fdossena.com/?p=ArduinoFanControl/i.md
 ****************************************************/

#include <Wire.h> // Enable this line if using Arduino Uno, Mega, etc.
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <RotaryEncoder.h>

Adafruit_7segment matrix = Adafruit_7segment();
RotaryEncoder encoder(A1, A0);

#define PIN_SW A2
#define PIN_SENSE 7
#define DEBOUNCE_FAN 10 //0 is fine for most fans, crappy fans may require 10 or 20 to filter out noise
#define DEBOUNCE_SW 50 
#define FANSTUCK_THRESHOLD 500 //if no interrupts were received for 500ms, consider the fan as stuck and report 0 RPM

const int DISPLAY_UPDATE_FREQ = 250; // freq in ms to update display
const int LEVEL_UPDATE_DELAY = 1500; // time in ms to show current level udpate
const String OFF_DISPLAY[] = {".   "," .  ","  . ","   .", "  . "," .  "};

const byte OC1A_PIN = 9;
const word PWM_FREQ_HZ = 25000; //Adjust this value to adjust the frequency (Frequency in HZ!) (Set currently to 25kHZ)
const word TCNT1_TOP = 16000000/(2*PWM_FREQ_HZ);

int SW_STATE;            // the current reading from the input pin
int LAST_SW_STATE = HIGH;  // the previous reading from the input pin
unsigned long LAST_SW_DEBOUNCE_TIME = 0; // the last time the output pin was toggled

//Interrupt handler. Stores the timestamps of the last 2 interrupts and handles debouncing
unsigned long volatile ts1=0,ts2=0;
void tachISR() {
    unsigned long m=millis();
    if((m-ts2)>DEBOUNCE_FAN){
        ts1=ts2;
        ts2=m;
    }
}
//Calculates and display the RPM based on the timestamps of the last 2 interrupts. Can be called at any time.
int calcRPM(){
  int rpm = 0;
  if(millis()-ts2<FANSTUCK_THRESHOLD&&ts2!=0){
      rpm = (60000/(ts2-ts1))/2;
  }
  return rpm; 
}

void setPwmDuty(byte duty) {
  OCR1A = (word) (duty*TCNT1_TOP)/100;
}

unsigned long updateDisplay(int display_out, unsigned long last_update_time) {
  if (millis() > last_update_time + DISPLAY_UPDATE_FREQ ) {
    matrix.clear();
    matrix.println(display_out);
    matrix.writeDisplay();
    last_update_time = millis();
  }
  return last_update_time;
}

void setup() {
  matrix.begin(0x70);
  pinMode(OC1A_PIN, OUTPUT);
  pinMode(PIN_SENSE,INPUT_PULLUP); //set the sense pin as input with pullup resistor
  pinMode(PIN_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SENSE),tachISR,FALLING); //set tachISR to be triggered when the signal on the sense pin goes low
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  TCCR1A |= (1 << COM1A1) | (1 << WGM11);
  TCCR1B |= (1 << WGM13) | (1 << CS10);
  ICR1 = TCNT1_TOP;
  setPwmDuty(50);
  Serial.begin(9600);
}

void loop() {
  static unsigned long last_display_update_time = 0;
  static unsigned long level_update_time = 0;
  static int level = 50;
  static int display_mode = 0;
  static int last_display_mode = 0;
  static int pos = 0;
  static unsigned long off_counter = 0;
  static int off_display_index = 0;
  
  int read_sw = digitalRead(PIN_SW);
  encoder.tick();
  int newPos = encoder.getPosition();
  
  // If the switch changed, due to noise or pressing:
  if (read_sw != LAST_SW_STATE) {
    // reset the debouncing timer
    LAST_SW_DEBOUNCE_TIME = millis();
  }
  if ((millis() - LAST_SW_DEBOUNCE_TIME) >= DEBOUNCE_SW) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    // if the button state has changed:
    if (read_sw != SW_STATE) {
      SW_STATE = read_sw;
      // only toggle the display mode if the new button state is LOW
      if (SW_STATE == LOW) {
        if (display_mode == 3) {
          display_mode = last_display_mode;
        }
        else {
          last_display_mode = display_mode;
          display_mode += 1;
          if (display_mode == 2) {
            off_counter = 0;
            matrix.println(OFF_DISPLAY[0]);
            matrix.writeDisplay();
          }
          if (display_mode > 2) {display_mode = 0;}
        }        
      }
    }
  }
  LAST_SW_STATE = read_sw;
  // get level 1-100
  // print it to the display
  // set the pwm duty cycle
  if (pos != newPos) {
    if (display_mode != 3) {
      last_display_mode = display_mode;
    }    
    display_mode = 3;
    level_update_time = millis();
    level += (newPos - pos) * 5;
    if (level >= 100) {level = 100;}
    if (level <= 0) {level = 0;}
    pos = newPos;
    matrix.print(level, DEC);
    matrix.writeDisplay();
    setPwmDuty(level);
  }
  // only update RPM display every so many ms
  if (display_mode == 0) {
    last_display_update_time = updateDisplay(calcRPM(), last_display_update_time);
  }
  if (display_mode == 1) {
    last_display_update_time = updateDisplay(level, last_display_update_time);
  }
  // turn the display "off"
  if (display_mode == 2) {
    off_counter += 1;
    if (off_counter > 60000) {off_counter = 0;}
    if (off_counter % 10000 == 0) {
      off_display_index = off_counter / 10000;
      String off_display = OFF_DISPLAY[off_display_index];
      if (off_display_index >= 0 && off_display_index <= 6) {
        matrix.println(off_display);
        matrix.writeDisplay();
      }
    }
  }
  // after adjusting the level, go back to previous display mode
  if (display_mode == 3) {
    if (millis() >= level_update_time + LEVEL_UPDATE_DELAY) {
      display_mode = last_display_mode;
    }
  }
}
