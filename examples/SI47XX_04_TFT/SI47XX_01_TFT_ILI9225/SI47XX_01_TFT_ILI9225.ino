/*
  This sketch uses an Arduino Pro Mini, 3.3V (8MZ) with a SPI TFT from MICROYUM (2" - 176 x 220).
  It is also a complete radio capable to tune LW, MW, SW on AM and SSB mode and also receive the
  regular comercial stations. If you are using the same circuit used on examples with OLED and LCD,
  you have to change some buttons wire up. This TFT device takes five pins from Arduino.
  For this reason, it is necessary change the pins of some buttons.
  Fortunately, you can use the ATmega328 analog pins as digital pins.

  wire up on Arduino UNO, Pro mini
  TFT               Pin
  SCK/SCL           13
  SDA/SDI/MOSI      11
  CS/SS             10
  DC/A0/RS          9
  RET/RESET/RTS     8

  By PU2CLR, Ricardo,  Feb  2020.
*/

#include <SI4735.h>

#include <SPI.h>
#include "TFT_22_ILI9225.h" //   // See https://github.com/Nkawu/TFT_22_ILI9225/wiki

#include "Rotary.h"

// Test it with patch_init.h or patch_full.h. Do not try load both.
#include "patch_init.h" // SSB patch for whole SSBRX initialization string

const uint16_t size_content = sizeof ssb_patch_content; // see ssb_patch_content in patch_full.h or patch_init.h

// TFT MICROYUM or ILI9225 based device pin setup
#define TFT_RST 8
#define TFT_RS 9
#define TFT_CS 10  // SS
#define TFT_SDI 11 // MOSI
#define TFT_CLK 13 // SCK
#define TFT_LED 0  // 0 if wired to +3.3V directly
#define TFT_BRIGHTNESS 200

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

#define RESET_PIN 12

// Enconder PINs
#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 3

// Buttons controllers
#define MODE_SWITCH 4      // Switch MODE (Am/LSB/USB)
#define BANDWIDTH_BUTTON 5 // Used to select the banddwith. Values: 1.2, 2.2, 3.0, 4.0, 0.5, 1.0 KHz
#define BAND_BUTTON_UP 6   // Next band
#define BAND_BUTTON_DOWN 7 // Previous band
#define AGC_SWITCH 14      // Pin A0 - Switch AGC ON/OF
#define STEP_SWITCH 15     // Pin A1 - Used to select the increment or decrement frequency step (1, 5 or 10 KHz)
#define BFO_SWITCH 16      // Pin A3 - Used to select the enconder control (BFO or VFO)

#define MIN_ELAPSED_TIME 100
#define MIN_ELAPSED_RSSI_TIME 150
#define DEFAULT_VOLUME 50 // change it for your favorite sound volume

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define SSB 1

bool bfoOn = false;
bool disableAgc = true;
bool ssbLoaded = false;
bool fmStereo = true;

int currentBFO = 0;

long elapsedRSSI = millis();
long elapsedButton = millis();

// Encoder control variables
volatile int encoderCount = 0;

// Some variables to check the SI4735 status
uint16_t currentFrequency;
uint8_t currentStep = 1;
uint8_t currentBFOStep = 25;

uint8_t bwIdxSSB = 2;
const char * bandwitdthSSB[] = {"1.2", "2.2", "3.0", "4.0", "0.5", "1.0"};

uint8_t bwIdxAM = 1;
const char * bandwitdthAM[] = {"6", "4", "3", "2", "1", "1.8", "2.5"};

const char * bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
uint8_t currentMode = FM;

char bufferDisplay[40]; // Useful to handle string
char bufferFreq[10];
char bufferBFO[15];
char bufferStepVFO[10];
char bufferStepBFO[10];
char bufferBW[10];
char bufferAGC[10];
char bufferBand[12];
char bufferStereo[10];

/*
   Band data structure
*/
typedef struct
{
  const char *bandName; // Band description
  uint8_t bandType;     // Band type (FM, MW or SW)
  uint16_t minimumFreq; // Minimum frequency of the band
  uint16_t maximumFreq; // maximum frequency of the band
  uint16_t currentFreq; // Default frequency or current frequency
  uint16_t currentStep; // Defeult step (increment and decrement)
} Band;

/*
   Band table
*/
Band band[] = {
  {"FM  ", FM_BAND_TYPE, 8400, 10800, 10390, 10},
  {"LW  ", LW_BAND_TYPE, 100, 510, 300, 1},
  {"AM  ", MW_BAND_TYPE, 520, 1720, 810, 10},
  {"80m ", SW_BAND_TYPE, 1800, 4500, 3700, 1}, // 80 meters - 160 meters
  {"60m ", SW_BAND_TYPE, 4500, 6300, 6000, 5}, //
  {"41m ", SW_BAND_TYPE, 6800, 7800, 7100, 5}, // 40 meters
  {"31m ", SW_BAND_TYPE, 9200, 10000, 9600, 5},
  {"25m ", SW_BAND_TYPE, 11200, 12500, 11940, 5},
  {"22m ", SW_BAND_TYPE, 13400, 13900, 13600, 5},
  {"20m ", SW_BAND_TYPE, 14000, 14500, 14200, 1}, // 20 meters
  {"19m ", SW_BAND_TYPE, 15000, 15900, 15300, 5},
  {"17m ", SW_BAND_TYPE, 18000, 18300, 18100, 1}, // 17 meters
  {"15m ", SW_BAND_TYPE, 21000, 21900, 21200, 1}, // 15 mters
  {"12m ", SW_BAND_TYPE, 24890, 26200, 24940, 1}, // 12 meters
  {"CB  ", SW_BAND_TYPE, 26200, 27900, 27500, 1}, // CB band (11 meters)
  {"10m ", SW_BAND_TYPE, 28000, 30000, 28400, 1}
};


const char * const text_arduino_library = "SI4735 Arduino Library";
const char * const text_example  = "Example - Modify it to your liking.";
const char * const text_author  = "By PU2CLR";

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 0;

uint8_t rssi = 0;
uint8_t snr = 0;
uint8_t stereo = 1;
uint8_t volume = DEFAULT_VOLUME;

// Devices class declarations
Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);
SI4735 si4735;

void setup()
{
  // Encoder pins
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(BANDWIDTH_BUTTON, INPUT_PULLUP);
  pinMode(BAND_BUTTON_UP, INPUT_PULLUP);
  pinMode(BAND_BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BFO_SWITCH, INPUT_PULLUP);
  pinMode(AGC_SWITCH, INPUT_PULLUP);
  pinMode(STEP_SWITCH, INPUT_PULLUP);
  pinMode(MODE_SWITCH, INPUT_PULLUP);

  // Use this initializer if using a 1.8" TFT screen:
  tft.begin();
  tft.setOrientation(1);
  tft.clear();
  showTemplate();

  // Encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  si4735.setup(RESET_PIN, 1);

  // Set up the radio for the current band (see index table variable bandIdx )
  useBand();
  si4735.setVolume(volume);
  showStatus();
}

/*
   Shows the static content on  display
*/
void showTemplate()
{

  int maxY1 = tft.maxY() - 1;
  int maxX1 = tft.maxX() -1;
  
  tft.setFont(Terminal6x8);

  tft.drawRectangle(0, 0, maxX1, maxY1, COLOR_WHITE);
  tft.drawRectangle(2, 2, maxX1 - 2, 40, COLOR_YELLOW);
  
  tft.drawLine(150, 0, 150, 40, COLOR_YELLOW);

  tft.drawLine(0, 80, maxX1, 80, COLOR_YELLOW); //
  tft.drawLine(60, 40, 60, 80, COLOR_YELLOW);            // Mode Block
  tft.drawLine(120, 40, 120, 80, COLOR_YELLOW);          // Band name

  tft.drawText(5, 150, "SNR.:", COLOR_RED);
  tft.drawText(5, 163, "RSSI:", COLOR_RED);

  tft.drawLine(0, 145, maxX1, 145, COLOR_YELLOW);

  tft.drawRectangle(45, 150,  maxX1 - 2, 156, COLOR_YELLOW);
  tft.drawRectangle(45, 163,  maxX1 - 2, 169, COLOR_YELLOW);

  tft.drawText(10, 90, text_arduino_library, COLOR_YELLOW);
  tft.drawText(10, 110, text_example, COLOR_YELLOW);
  tft.drawText(10, 130, text_author, COLOR_YELLOW);
  
}

// Just clear the buffer string array;
void clearBuffer(char *b)
{
  b[0] = '\0';
}

/*
    Prevents blinking during the frequency display.
    Erases the old digits if it has changed and print the new digit values.
*/
void printValue(int col, int line, char *oldValue, char *newValue, uint16_t color, uint8_t space) {
  int c = col;
  char * pOld;
  char * pNew;

  pOld = oldValue;
  pNew = newValue;

  // prints just changed digits
  while (*pOld && *pNew)
  {
    if (*pOld != *pNew)
    {
      tft.drawChar(c, line, *pOld, COLOR_BLACK);
      tft.drawChar(c, line, *pNew, color);
    }
    pOld++;
    pNew++;
    c += space;
  }

  // Is there anything else to erase?
  while (*pOld)
  {
    tft.drawChar(c, line, *pOld, COLOR_BLACK);
    pOld++;
    c += space;
  }

  // Is there anything else to print?
  while (*pNew)
  {
    tft.drawChar(c, line, *pNew, color);
    pNew++;
    c += space;
  }

  // Save the current content to be tested next time
  strcpy(oldValue, newValue);
}

/*
    Reads encoder via interrupt
    Use Rotary.h and  Rotary.cpp implementation to process encoder via interrupt
*/
void rotaryEncoder()
{ // rotary encoder events
  uint8_t encoderStatus = encoder.process();
 
  if (encoderStatus)
      encoderCount = (encoderStatus == DIR_CW)? 1 : -1;
}

/*
   Shows frequency information on Display
*/
void showFrequency()
{
  float freq;
  int iFreq, dFreq;
  uint16_t color;

  tft.setFont(Trebuchet_MS16x21);

  if (si4735.isCurrentTuneFM())
  {
    freq = currentFrequency / 100.0;
    dtostrf(freq, 3, 1, bufferDisplay);
  }
  else
  {
    freq = currentFrequency / 1000.0;
    if (currentFrequency < 1000)
      sprintf(bufferDisplay, "%3d", currentFrequency);
    else
      dtostrf(freq, 2, 3, bufferDisplay);
  }
  color = (bfoOn) ? COLOR_CYAN : COLOR_YELLOW;
  printValue(10, 10, bufferFreq, bufferDisplay, color, 20);
}

/*
    Show some basic information on display
*/
void showStatus()
{
  char unit[5];
  si4735.getStatus();
  si4735.getCurrentReceivedSignalQuality();
  // SRN

  currentFrequency = si4735.getFrequency();
  showFrequency();

  tft.setFont(Terminal6x8);
  printValue(155, 10, bufferStepVFO, bufferDisplay, COLOR_BLACK, 7);

  if (si4735.isCurrentTuneFM())
  {
    tft.drawText(155, 30, "MHz", COLOR_RED);
    showBFOTemplate(COLOR_BLACK);
    tft.drawText(124, 45, bufferBW, COLOR_BLACK);
  }
  else
  {
    sprintf(bufferDisplay, "Step: %2.2d", currentStep);
    printValue(155, 10, bufferStepVFO, bufferDisplay, COLOR_YELLOW, 7);
    tft.drawText(155, 30, "KHz", COLOR_RED);
  }

  if (band[bandIdx].bandType == SW_BAND_TYPE)
    sprintf(bufferDisplay, "%s %s", band[bandIdx].bandName, bandModeDesc[currentMode]);
  else
    sprintf(bufferDisplay, "%s", band[bandIdx].bandName);
  printValue(4, 60, bufferBand, bufferDisplay, COLOR_CYAN, 7);

  // AGC
  si4735.getAutomaticGainControl();
  sprintf(bufferDisplay, "AGC %s", (si4735.isAgcEnabled()) ? "ON  " : "OFF");
  printValue(65, 60, bufferAGC, bufferDisplay, COLOR_CYAN, 7);


  // Bandwidth
  if (currentMode == LSB || currentMode == USB || currentMode == AM) {
    char * bw;
    uint16_t colorBFO = COLOR_CYAN;

    tft.drawText(150, 60, bufferStereo, COLOR_BLACK); // Erase Stereo/Mono information

    if (currentMode == AM) {
      bw = (char *) bandwitdthAM[bwIdxAM];
      colorBFO = COLOR_BLACK;
    }
    else {
      bw = (char *) bandwitdthSSB[bwIdxSSB];
      showBFO();
    }
    sprintf(bufferDisplay, "BW: %s KHz", bandwitdthSSB[bwIdxSSB]);
    printValue(124, 45, bufferBW, bufferDisplay, COLOR_CYAN, 7);
    showBFOTemplate(COLOR_CYAN);
  }

}

/* *******************************
   Shows RSSI status
*/
void showRSSI()
{
  int rssiLevel;
  int snrLevel;

  tft.setFont(Terminal6x8);
  if (currentMode == FM)
  {
    sprintf(bufferDisplay, "%s", (si4735.getCurrentPilot()) ? "STEREO" : "MONO");
    printValue(150, 60, bufferStereo, bufferDisplay, COLOR_CYAN, 7);
  }

  rssiLevel = 47 + map(rssi, 0, 127, 0, ( tft.maxX()  - 43) );
  snrLevel = 47 + map(snr, 0, 127, 0, ( tft.maxX()  - 43) );

  tft.fillRectangle(46, 151,  tft.maxX() - 3, 155, COLOR_BLACK);
  tft.fillRectangle(46, 164, tft.maxX() - 3, 168, COLOR_BLACK);

  tft.fillRectangle(46, 151,  rssiLevel, 155, COLOR_LIGHTCYAN);
  tft.fillRectangle(46, 164, snrLevel, 168, COLOR_LIGHTCYAN);

}

void showBFOTemplate(uint16_t color)
{
  tft.setFont(Terminal6x8);

  tft.drawText(150, 60, bufferStereo, COLOR_BLACK);

  tft.drawText(124, 55, "BFO.:", color);
  tft.drawText(124, 65, "Step:", color);

  tft.drawText(160, 55, bufferBFO, COLOR_BLACK);
  tft.drawText(160, 65, bufferStepBFO, COLOR_BLACK);
}

void showBFO()
{
  tft.setFont(Terminal6x8);

  sprintf(bufferDisplay, "%+4d", currentBFO);
  printValue(160, 55, bufferBFO, bufferDisplay, COLOR_CYAN, 7);

  sprintf(bufferDisplay, "%4d", currentBFOStep);
  printValue(160, 65, bufferStepBFO, bufferDisplay, COLOR_CYAN, 7);

}

/*
   Goes to the next band (see Band table)
*/
void bandUp()
{
  // save the current frequency for the band
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStep = currentStep;

  bandIdx = (bandIdx < lastBand) ? (bandIdx + 1) : 0;

  useBand();
}

/*
   Goes to the previous band (see Band table)
*/
void bandDown()
{
  // save the current frequency for the band
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStep = currentStep;

  bandIdx = (bandIdx > 0) ? (bandIdx - 1) : lastBand;

  useBand();
}

/*
   This function loads the contents of the ssb_patch_content array into the CI (Si4735) and starts the radio on
   SSB mode.
*/
void loadSSB()
{
  si4735.reset();
  si4735.queryLibraryId(); // Is it really necessary here? I will check it.
  si4735.patchPowerUp();
  delay(50);
  // si4735.setI2CFastMode(); // Recommended
  si4735.setI2CFastModeCustom(500000); // It is a test and may crash.
  si4735.downloadPatch(ssb_patch_content, size_content);
  si4735.setI2CStandardMode(); // goes back to default (100KHz)

  // Parameters
  // AUDIOBW - SSB Audio bandwidth; 0 = 1.2KHz (default); 1=2.2KHz; 2=3KHz; 3=4KHz; 4=500Hz; 5=1KHz;
  // SBCUTFLT SSB - side band cutoff filter for band passand low pass filter ( 0 or 1)
  // AVC_DIVIDER  - set 0 for SSB mode; set 3 for SYNC mode.
  // AVCEN - SSB Automatic Volume Control (AVC) enable; 0=disable; 1=enable (default).
  // SMUTESEL - SSB Soft-mute Based on RSSI or SNR (0 or 1).
  // DSP_AFCDIS - DSP AFC Disable or enable; 0=SYNC MODE, AFC enable; 1=SSB MODE, AFC disable.
  si4735.setSSBConfig(bwIdxSSB, 1, 0, 0, 0, 1);
  delay(25);
  ssbLoaded = true;
}

/*
   Switch the radio to current band
*/
void useBand()
{
  showBFOTemplate(COLOR_BLACK);

  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentMode = FM;
    si4735.setTuneFrequencyAntennaCapacitor(0);
    si4735.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
    bfoOn = ssbLoaded = false;
  }
  else
  {
    // set the tuning capacitor for SW or MW/LW
    si4735.setTuneFrequencyAntennaCapacitor( (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) ? 0 : 1);

    if (ssbLoaded)
    {
      si4735.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep, currentMode);
      si4735.setSSBAutomaticVolumeControl(1);
      si4735.setSsbSoftMuteMaxAttenuation(0); // Disable Soft Mute for SSB
    }
    else
    {
      currentMode = AM;
      si4735.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
      si4735.setAutomaticGainControl(1, 0);
      si4735.setAmSoftMuteMaxAttenuation(0); // // Disable Soft Mute for AM
      bfoOn = false;
    }
  }
  delay(100);
  currentFrequency = band[bandIdx].currentFreq;
  currentStep = band[bandIdx].currentStep;
  showStatus();
}

void loop()
{
  // Check if the encoder has moved.
  if (encoderCount != 0)
  {
    if (bfoOn)
    {
      currentBFO = (encoderCount == 1) ? (currentBFO + currentBFOStep) : (currentBFO - currentBFOStep);
      si4735.setSSBBfo(currentBFO);
      showBFO();
    }
    else
    {
      if (encoderCount == 1)
        si4735.frequencyUp();
      else
        si4735.frequencyDown();
      // Show the current frequency only if it has changed
      currentFrequency = si4735.getFrequency();
    }
    showFrequency();
    encoderCount = 0;
  }

  // Check button commands
  if ((millis() - elapsedButton) > MIN_ELAPSED_TIME)
  {
    // check if some button is pressed
    if (digitalRead(BANDWIDTH_BUTTON) == LOW)
    {
      if (currentMode == LSB || currentMode == USB)
      {
        bwIdxSSB++;
        if (bwIdxSSB > 5)
          bwIdxSSB = 0;
        si4735.setSSBAudioBandwidth(bwIdxSSB);
        // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
        if (bwIdxSSB == 0 || bwIdxSSB == 4 || bwIdxSSB == 5)
          si4735.setSBBSidebandCutoffFilter(0);
        else
          si4735.setSBBSidebandCutoffFilter(1);
      }
      else if (currentMode == AM)
      {
        bwIdxAM++;
        if (bwIdxAM > 6)
          bwIdxAM = 0;
        si4735.setBandwidth(bwIdxAM, 0);
      }
      showStatus();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(BAND_BUTTON_UP) == LOW)
      bandUp();
    else if (digitalRead(BAND_BUTTON_DOWN) == LOW)
      bandDown();
    else if (digitalRead(BFO_SWITCH) == LOW)
    {
      if (currentMode == LSB || currentMode == USB)
      {
        bfoOn = !bfoOn;
        if (bfoOn)
        {
          showBFOTemplate(COLOR_CYAN);
          showBFO();
          showStatus();
        }
        else
        {
          showBFOTemplate(COLOR_BLACK);
        }
        clearBuffer(bufferFreq);
      }
      else if (currentMode == FM)
      {
        si4735.seekStationUp();
        currentFrequency = si4735.getFrequency();
      }
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
      showFrequency();
    }
    else if (digitalRead(AGC_SWITCH) == LOW)
    {
      disableAgc = !disableAgc;
      // siwtch on/off ACG; AGC Index = 0. It means Minimum attenuation (max gain)
      si4735.setAutomaticGainControl(disableAgc, 1);
      showStatus();
    }
    else if (digitalRead(STEP_SWITCH) == LOW)
    {
      // This command should work only for SSB mode
      if (bfoOn && (currentMode == LSB || currentMode == USB))
      {
        currentBFOStep = (currentBFOStep == 25) ? 10 : 25;
        showBFO();
      }
      else
      {
        if (currentStep == 1)
          currentStep = 5;
        else if (currentStep == 5)
          currentStep = 10;
        else if (currentStep == 10)
          currentStep = 50;
        else
          currentStep = 1;
        si4735.setFrequencyStep(currentStep);
        band[bandIdx].currentStep = currentStep;
        showStatus();
      }
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(MODE_SWITCH) == LOW)
    {
      if (currentMode != FM)
      {
        if (currentMode == AM)
        {
          // If you were in AM mode, it is necessary to load SSB patch (avery time)
          loadSSB();
          currentMode = LSB;
        }
        else if (currentMode == LSB)
        {
          currentMode = USB;
        }
        else if (currentMode == USB)
        {
          currentMode = AM;
          bfoOn = ssbLoaded = false;
        }
        // Nothing to do if you are in FM mode
        band[bandIdx].currentFreq = currentFrequency;
        band[bandIdx].currentStep = currentStep;
        useBand();
      }
    }
    elapsedButton = millis();
  }

  // Show RSSI status only if this condition has changed
  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 12)
  {
    si4735.getCurrentReceivedSignalQuality();
    // int aux = si4735.getReceivedSignalStrengthIndicator();
    int aux = si4735.getCurrentRSSI();
    if (rssi != aux)
    {
      rssi = aux;
      snr = si4735.getStatusSNR();
      showRSSI();
    }
    elapsedRSSI = millis();
  }
  delay(10);
}
