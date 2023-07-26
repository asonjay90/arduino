#include <Audio.h>
#include <EEPROM.h>
#include <OneButton.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

AudioPlaySdWav           playWav1;
AudioOutputI2S           audioOutput;
AudioConnection          patchCord1(playWav1, 0, audioOutput, 0);
AudioConnection          patchCord2(playWav1, 1, audioOutput, 1);
AudioControlSGTL5000     sgtl5000_1;


// Teensy 3.5 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
File dir;
// User inputs
#define BUTTON_PIN 16 // front user facig button
#define LED_PIN 15 // digital output pin for LED
#define TRIGGER_PIN 38 // timer trigger
#define POWER_PIN 14 // KEY switch pin for power supply enable
// Initialize button and trigger
OneButton userButton = OneButton(
    BUTTON_PIN,  // Input pin for the button
    true,        // Button is active LOW
    true         // Enable internal pull-up resistor
);
OneButton triggerButton = OneButton(
    TRIGGER_PIN,  // Input pin for the button
    true,        // Button is active LOW
    true         // Enable internal pull-up resistor
);
// LED variables
boolean enableBlink = false; // Blink the front LED
boolean quickBlink = false; // Blink the front led faster
int blinkInterval = 1000; // time to blink in ms (1 sec)
unsigned long ledCounter = 0; // Counter for led to avoid using delays
// Alarm variables
int alarmCount = 0; // Number of alarms on SD card
int alarmState = 0; // 0 = off - 1 = armed - 2 = triggered
int currentAlarm = 0; // The number at the end of the filname. e.g. ALARM0.wav
String alarmPrefix = "/Alarms/ALARM"; // Dir and filename minus index
String greetingPrefix = "/Greetings/GREETING";
// Modes
boolean configMode = false; // Used to cycle and change currentAlarm
int shutDownTime = 60000; // How long should we idle before shutting down (1 min)
int idleStartTime = 0;  // Time when idling begins
boolean goodbyeGreetingPlayed = false; // prevent goodbye greeting from looping

void setup() {
  // Serial goodness
  Serial.begin(9600);
  delay(100);
  Serial.println("Initializing Bumpin' Bethoven Timer...");
  //Initialize pins
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, HIGH);
  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(8);
  // SD Card stuff
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  // Set button handles
  userButton.attachClick(buttonClick);
  userButton.attachLongPressStop(buttonHoldRelease);
  triggerButton.attachLongPressStop(triggerTimerStart); // When timer "starts" the button is released
  triggerButton.attachLongPressStart(triggerTimerStop); // When timer "stop" the button is pressed
  alarmCount = getFileCount("Alarms") - 1; // Alarm names should start at 0
  currentAlarm = EEPROM.read(0);
  idleStartTime = millis();

  playFile("Greeting", 0); // Play power on greeting
}

void loop()
{
  // Required for onebutton library
  userButton.tick();
  triggerButton.tick();
  // Handle led blinking states without delays
  blinkLed();
  // Loop alarm if triggered
  alarmLoop();
  // Power off when idle
  idlePowerOff();
}

static void idlePowerOff() {
  if (alarmState == 0 && idleStartTime > 0) {
    int idleTime = millis() - idleStartTime;
    if (idleTime > shutDownTime && !goodbyeGreetingPlayed){
      Serial.println("Idle for too long - Shutting down!");
      playFile("Greeting", 2); // Play goodbye greeting
      digitalWrite(POWER_PIN, LOW);
      goodbyeGreetingPlayed = true;
    }
  }
  else {
    goodbyeGreetingPlayed = false;
  }
}

static void alarmLoop() {
  if (alarmState == 2 && !playWav1.isPlaying()) {
    // Alarm is triggered but not playing
    Serial.println("Button not pressed! Looping alarm.");
    playFile("Alarm", currentAlarm);
  }
}

static void triggerTimerStart() {
  Serial.println("Timer has started!");
  if (!playWav1.isPlaying()) {
    playFile("Greeting", 3); // Play clock started greeting
  }
  alarmState = 1; // Set alarm state to ARMED
  enableBlink = true; // Start blinking to show we're armed
}

static void triggerTimerStop() {
  if (alarmState == 1) {
    Serial.println("Timer has finished!");
    alarmState = 2;  // Set alarm state to TRIGGERED
    playFile("Alarm", currentAlarm);
    quickBlink = true;
  }  
}

static void buttonClick() {
  Serial.println("Button clicked!");
  if (configMode == false && alarmState == 2) { // NOT in config mode and alarm is curently triggered
      alarmState = 0;  // Set alarm state to OFF
      idleStartTime = millis(); // Start idle timer
      enableBlink = false; // Stop blinking LED
      quickBlink = false;
      stopAlarm();    // Stop laying alarm
    }
    if (configMode == true && alarmState == 0) { // in config mode and alarm is not armed or triggered (i.e. cycle alarms)
      stopAlarm();
      currentAlarm++;
      if (currentAlarm > alarmCount) {
        currentAlarm = 0;
      }
      playFile("Alarm", currentAlarm);
    }
}

static void buttonHoldRelease() {
  Serial.println("Button hold released!");
  if (alarmState == 0) {  // Can only enter or exit config mode when not armed or triggered
    configMode = !configMode; // toggle config mode
    enableBlink = configMode;
    quickBlink = configMode;
    if (configMode) {
      Serial.println("Config mode enabled");
      idleStartTime = 0; // Stop idle timer
      playFile("Alarm", currentAlarm);
    }
    else {
      Serial.println("Config mode disabled");
      stopAlarm();
      EEPROM.write(0, currentAlarm); // write current alarm to EEPROM at address zero
      playFile("Greeting", 1); // Play confirmation greeting
      idleStartTime = millis(); // Start idle timer

    }
  }
  if (alarmState == 2) {
    stopAlarm(); // If button is held and alarm is triggered, stop alarm
    alarmState = 0;
    idleStartTime = millis(); // Start idle timer
    enableBlink = false;
    quickBlink = false;
  }
}

void blinkLed() {
  unsigned long currentMillis = millis();
  int blinkRate = blinkInterval;
  if (quickBlink) {
    blinkRate = blinkInterval /5; // Blink 4x the speed
  }
  if (enableBlink == true) {
    if (currentMillis - ledCounter >= blinkRate) {
      ledCounter = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
  else {
    digitalWrite(LED_PIN, HIGH); // LED should be solid when on and idle
  }
}

void stopAlarm() {
  Serial.println("Stopping Alarm");
  playWav1.stop();
}

void playFile(String type, int index) {
  // Play a greeting or alarm.
  // All alarms or greetings are in the same dir and have the same name with different number suffix (index)
  // e.g. /Alarms/ALARM1.WAV or /Greetings/GREETING2.WAV
  // type: Alarm or Greeting
  // index: the file number to play
  String fileName = alarmPrefix + index + ".WAV";
  if (type == "Greeting") { 
    fileName = greetingPrefix + index + ".WAV";
  }  
  char fileCharName[fileName.length() + 1]; // Converting string to char array
  fileName.toCharArray(fileCharName, fileName.length() + 1);
  Serial.println("Atempting to play alarm: " + fileName );
  if (playWav1.isPlaying()) {
    playWav1.stop();  // If something else is playing, stop that first
  }
  playWav1.play(fileCharName); // Play the file and wait a bit to check if its playing
  delay(25);
  if (!playWav1.isPlaying()) {
      Serial.println("Failed to play file!");
  }
}

int getFileCount(String dirToCount) {
  String dirPath = "/" + dirToCount;
  char charDirPath[dirPath.length() + 1];
  dirPath.toCharArray(charDirPath, dirPath.length() + 1);
  File dir = SD.open(charDirPath);
  int fileCount = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    fileCount++;
    entry.close();
  }
  return fileCount;
}
