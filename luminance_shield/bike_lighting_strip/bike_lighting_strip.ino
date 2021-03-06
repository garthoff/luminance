/*
  Nathan Seidle
 SparkFun Electronics 2011
 
 This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 
 Controlling an LED strip with individually controllable RGB LEDs. This stuff is awesome.
 
 The SparkFun (individually controllable) RGB strip contains a bunch of WS2801 ICs. These
 are controlled over a simple data and clock setup. The WS2801 is really cool! Each IC has its
 own internal clock so that it can do all the PWM for that specific LED for you. Each IC
 requires 24 bits of 'greyscale' data. This means you can have 256 levels of red, 256 of blue,
 and 256 levels of green for each RGB LED. REALLY granular.
 
 To control the strip, you clock in data continually. Each IC automatically passes the data onto
 the next IC. Once you pause for more than 500us, each IC 'posts' or begins to output the color data
 you just clocked in. So, clock in (24bits * 32LEDs = ) 768 bits, then pause for 500us. Then
 repeat if you wish to display something new.
 
 This example code will display bright red, green, and blue, then 'trickle' random colors down 
 the LED strip.
 
 */

//Add the SPI library so we can communicate with the CMA3000 sensor
#include <SPI.h>
#include <avr/sleep.h>


#define RELEASE_MODE 1
#define ENABLE_SLEEP 0

#define DEBUG 1
#define DEBUG_STATE 0
#define DEBUG_ACCEL 1
#define DEBUG_SLEEP 0


#define POS_OUT_LOGIC 0

#if POS_OUT_LOGIC
#define OUTPUT_HIGH HIGH
#define OUTPUT_LOW LOW
#else
#define OUTPUT_HIGH LOW
#define OUTPUT_LOW HIGH
#endif

//
// pins defined
//
// wake up pins
const int wakePin = 2;

const int ledPin = 13;
const int CS = 10;  //Assign the Chip Select signal to pin 10.

#define XAXIS  0
#define YAXIS  1
#define ZAXIS  2

#define RANGE_2G  0
#if RANGE_2G
#define COUNTS_PER_G  56  // 56 counts/G for 2G
// Activate measurement mode: 2g/100Hz
#define ACTIVE_CONFIG   (G_RANGE_2 | INT_LEVEL_LOW | I2C_DIS | MODE_100 | INT_DIS)
// Motion detect mode: 2g/10Hz
#define SLEEP_CONFIG    (G_RANGE_2 | INT_LEVEL_LOW | MDET_NO_EXIT | I2C_DIS | MODE_MD_10)
#define MD_THRESH   0x20
// threshold for responding to signals
#define TURN_THRESH 10
#define NOD_THRESH  10
#define MAX_OFFSET  3
#define 
#else
#define COUNTS_PER_G  14  // 14 counts/G for 8G
// Activate measurement mode: 2g/40Hz
#define ACTIVE_CONFIG   (INT_LEVEL_LOW | I2C_DIS | MODE_40 | INT_DIS)
// Motion detect mode: 2g/10Hz
#define SLEEP_CONFIG    (INT_LEVEL_LOW | MDET_NO_EXIT | I2C_DIS | MODE_MD_10)
#define MD_THRESH   0x04
// threshold for responding to signals
#define TURN_THRESH 10
#define NOD_THRESH  10
#define MAX_OFFSET  3
#endif

#define DELAY_TIME  20

// CMA3000 Registers
#define WHO_AM_I 0x00  // Identification register
#define REVID 0x01     // ASIC revision ID, fixed in metal
#define CTRL 0x02      // Configuration (por, operation modes)
#define STATUS 0x03    // Status (por, EEPROM parity)
#define RSTR 0x04      // Reset Register
#define INT_STATUS 0x05 // Interrupt status register
#define DOUTX 0x06     // X channel output data register
#define DOUTY 0x07     // Y channel output data register
#define DOUTZ 0x08     // Z channel output data register
#define MDTHR 0x09     // Motion detection threshold value register
#define MDFFTMR 0x0A   // Free fall and motion detection time register
#define FFTHR 0x0B     // Free fall threshold value register
#define I2C_ADDR 0x0C  // I2C device address
// 0Dh-19h Reserved

// Control Register setup
#define G_RANGE_2 0x80 // 2g range
#define INT_LEVEL_LOW 0x40 // INT active low
#define MDET_NO_EXIT 0x20 // Remain in motion detection mode
#define I2C_DIS 0x10 // I2C disabled
#define MODE_PD 0x00 // Power Down
#define MODE_100 0x02 // Measurement mode 100 Hz ODR
#define MODE_400 0x04 // Measurement mode 400 Hz ODR
#define MODE_40 0x06 // Measurement mode 40 Hz ODR
#define MODE_MD_10 0x08 // Motion detection mode 10 Hz ODR
#define MODE_FF_100 0x0A // Free fall detection mode 100 Hz ODR
#define MODE_FF_400 0x0C // Free fall detection mode 400 Hz ODR
#define INT_DIS 0x01 // Interrupts enabled

// Interrupt Status Register
#define FFDET  0x04
#define MDET_MSK     0x03
#define MDET_NONE    0x00
#define MDET_X_AXIS  0x01
#define MDET_Y_AXIS  0x02
#define MDET_Z_AXIS  0x03

// FUNCTION PROTOTYPES
unsigned char readRegister(unsigned char registerAddress);
unsigned char writeRegister(unsigned char registerAddress, unsigned char Data);

typedef enum {
  sys_init = 0,
  sys_idle,
  sys_active,
  sys_left_turn,
  sys_right_turn,
  sys_stop,
  sys_nod,
  sys_sleep,
  sys_wake,
}
sys_state_t;

static sys_state_t state;

const unsigned long wakeDuration = 750; // 0.75 seconds

int sleepStatus = 0;  // variable to store a request for sleep
unsigned long wakeTime = 0;

//This buffer will hold values read from the ADXL345 registers.
char intStatus=0;
#define NOAXIS 0xff
int wakeAxis = NOAXIS;

int revID;

const unsigned int discardCnt = 1;
const unsigned int maxSamples = 7;
unsigned int sampleCnt = 0;

//These variables will be used to hold the x,y and z axis accelerometer values.
//int x,y,z;
int a[3] = {
  0, 0, 0};
int last[3] = {
  0, 0, 0};
//int sum[3] = {
//  0, 0, 0};
float  g[3] = {
  0.0, 0.0, 0.0};
float avg[3] = {
  0, 0, 0};
float base[3] = {
  0, 0, 0};

#define CHAN_CNT 2
#define CHAN_0 0
#define CHAN_1 1

const int PWR[CHAN_CNT] = {
  9, 6};
const int SDI[CHAN_CNT] = {
  8, 5};
const int CKI[CHAN_CNT] = {
  7, 4};

#define STRIP_LENGTH 8
//# LEDs on this strip
long strip_colors[STRIP_LENGTH];

const int pwmPinCnt = 3;
#define PWM_MIN_VAL  0
#define PWM_MAX_VAL  64
#define PWM_DEF_VAL  ((int)((PWM_MAX_VAL+PWM_MIN_VAL)/2.0))
int pwmDutyCycle[pwmPinCnt] = {
  PWM_DEF_VAL, PWM_DEF_VAL, PWM_DEF_VAL};

// modulating background color
//float oSpeed = .000075;
const float oSpeed = 0.0025;
const int cycleDelay = 100;

const float rOffset = PWM_DEF_VAL/(float)PWM_MAX_VAL;
const float gOffset = PWM_DEF_VAL/(float)PWM_MAX_VAL;
const float bOffset = PWM_DEF_VAL/(float)PWM_MAX_VAL;

int rValue = PWM_MAX_VAL;
int gValue = PWM_MAX_VAL;
int bValue = PWM_MAX_VAL;

void wakeUpNow()
{
  // execute code here after wake-up before returning to the loop() function
  // timers and code using timers (serial.print and more...) will not work here.

  // TODO check what the actual event is
  intStatus = readRegister(INT_STATUS);

  if (intStatus) {
    // FF or MD
    state = sys_wake;
  } 
  else {
    // if state = sys_shutdown
    // dont handle?
    state = sys_active;
  }  
}

void setup() {
  pinMode(ledPin, OUTPUT);

  for (int i=0; i<2; i++) {
    digitalWrite(PWR[i], OUTPUT_LOW);
    pinMode(PWR[i], OUTPUT);

    digitalWrite(SDI[i], OUTPUT_LOW);
    digitalWrite(CKI[i], OUTPUT_LOW);
    pinMode(SDI[i], OUTPUT);
    pinMode(CKI[i], OUTPUT);
  }

  Serial.begin(57600);
  Serial.println("Hello!");

  //Clear out the array
  for(int x = 0 ; x < STRIP_LENGTH ; x++) {
    strip_colors[x] = ((long)0xff)<<((x%3)*8);
    Serial.println(strip_colors[x],HEX);
  }
  //post_frame();
  post_frame(CHAN_0);
  post_frame(CHAN_1);


#if RELEASE_MODE
  // disable unused things
  ADCSRA &= ~(0x80);  // disable the ADC

  // could disable the Analog Comparitor (did not see power savings)
  //ADCSRB &= ~(0x40);  // make sure the Analog Comparator is not trying to use ADC MUX
  //ACSR |= 0x80;       // disable the Analog Comparator
  //ACSR &= ~(0x40);    // make sure the AC is not using the internal ref

  // could disable digital input circuitry on ADC and AC pins
  //DIDR0 = 0x30;  // ADC5D-ADC4D (we are using A0-A3 for leds)
  //DIDR1 = 0x03;  // AIN1D-AIN0D
#endif

  //Initiate an SPI communication instance.
  SPI.begin();
  //Configure the SPI connection for the CMA3000.
  SPI.setDataMode(SPI_MODE0);
  //Create a serial connection to display the data on the terminal.
  Serial.begin(57600);

  //Set up the Chip Select pin to be an output from the Arduino.
  pinMode(CS, OUTPUT);
  //Before communication starts, the Chip Select pin needs to have pull up enabled.
  digitalWrite(CS, HIGH);

  // enable interrupt input
  pinMode(2, INPUT);
  digitalWrite(2, HIGH);

  revID = readRegister(REVID); // Read REVID register
  delayMicroseconds(44);

  // setup motion detection
  writeRegister(MDTHR, MD_THRESH);
  delayMicroseconds(44);
  writeRegister(MDFFTMR, 0x12);  // using defaults of 300ms
  //delayMicroseconds(44);

  // Activate measurement mode: 2g/400Hz, force SPI only comm, interrupts active low
  //writeRegister(CTRL, SLEEP_CONFIG);
  writeRegister(CTRL, ACTIVE_CONFIG);
  delayMicroseconds(44);

  // Dummy read to generate first INT pin Lo to Hi
  // transition in all situations, also while debugging
  for (int i=0; i<3; i++) {
    a[i] = readRegister(DOUTX+i);
  }

  //Create an interrupt that will trigger when a motion event is detected.
  attachInterrupt(0, wakeUpNow, LOW);

  state = sys_idle;
}

void loop() {

  for (int i=0; i<3; i++) {
    a[i] = readRegister(DOUTX+i); // Read DOUT* register
    // convert from 2s complement
    if (0x80 & a[i]) {
      a[i] = a[i]-0xff;
    }
    //Convert the accelerometer value to G's. 
    g[i] = a[i] / (float)COUNTS_PER_G;
    delayMicroseconds(44);
  }

  if(intStatus > 0) {
    if(FFDET & intStatus) {
      Serial.println("FFDET");
      Serial.print(a[XAXIS]);
      Serial.print(',');
      Serial.print(a[YAXIS]);
      Serial.print(',');
      Serial.println(a[ZAXIS]);
    }

    if (MDET_MSK & intStatus) {
      //state = sys_active;
      sampleCnt = 0;

#if DEBUG_ACCEL
      Serial.print("MDET: ");
#endif
      intStatus &= MDET_MSK;
      if (MDET_X_AXIS == intStatus) {
        wakeAxis = XAXIS;
#if DEBUG_ACCEL
        Serial.println("X");
#endif
      } 
      else if (MDET_Y_AXIS == intStatus) {
        wakeAxis = YAXIS;
#if DEBUG_ACCEL
        Serial.println("Y");
#endif
      }
    } 
    else if (MDET_Z_AXIS == intStatus) {
      wakeAxis = ZAXIS;
#if DEBUG_ACCEL
      Serial.println("Z");
#endif
    } 
    else {
      wakeAxis = NOAXIS;
    }

    intStatus = readRegister(INT_STATUS);    
  }
  sampleCnt++;

  if (a[YAXIS] < -9) {
    state = sys_left_turn;
  } 
  else if (a[XAXIS] > 11) {
    state = sys_right_turn;
  } 
  else if (a[XAXIS] < -11) {
    state = sys_stop;
  } 
  else {
    state = sys_wake;
  }

#if DEBUG_ACCEL
  //Print the results to the terminal.
  Serial.print("accel: ");
  Serial.print(a[XAXIS], DEC);
  Serial.print(',');
  Serial.print(a[YAXIS], DEC);
  Serial.print(',');
  Serial.println(a[ZAXIS], DEC);
  delay(20);
#endif

  switch (state) {
  case sys_left_turn:
    setColor(255,0,0);
    break;
  case sys_right_turn:
    setColor(0,255,0);
    break;
  case sys_nod:
    setColor(0,0,255);
    break;
  case sys_stop:
    setColor(0,0,255);
    break;
  default:
    setColor(0,0,0);
    break;
  }

  //addRandom();
  //updateColor();
  //loopColor();
  //post_frame(); //Push the current color frame to the strip
  post_frame(CHAN_0); //Push the current color frame to the strip
  post_frame(CHAN_1);

  delay(cycleDelay);                  // wait for a while

#if DEBUG  
  Serial.print(rValue);
  Serial.print(", ");
  Serial.print(gValue);
  Serial.print(", ");
  Serial.println(bValue);
  delay(20);
#endif
  if ((millis() - wakeTime) >= wakeDuration) {
#if DEBUG_SLEEP
    Serial.println("Entering Sleep mode");
    delay(100);     // this delay is needed, the sleep
    //function will provoke a Serial error otherwise!!
#endif
#if ENABLE_SLEEP
    sleepNow();     // sleep function called here
#endif
  }

  delay(DELAY_TIME); 
}


void loopColor(void) {
  int x;
  long tmp;

  tmp = strip_colors[STRIP_LENGTH-1];

  //First, shuffle all the current colors down one spot on the strip
  for(x = (STRIP_LENGTH - 1) ; x > 0 ; x--) {
    strip_colors[x] = strip_colors[x - 1];
  }
  strip_colors[0] = tmp;
}

//Throws random colors down the strip array
void addRandom(void) {
  int x;

  //First, shuffle all the current colors down one spot on the strip
  for(x = (STRIP_LENGTH - 1) ; x > 0 ; x--) {
    strip_colors[x] = strip_colors[x - 1];
  }

  //Now form a new RGB color
  long new_color = 0;
  for(x = 0 ; x < 3 ; x++){
    new_color <<= 8;
    new_color |= random(0xFF); //Give me a number from 0 to 0xFF
    //new_color &= 0xFFFFF0; //Force the random number to just the upper brightness levels. It sort of works.
  }

  strip_colors[0] = new_color; //Add the new random color to the strip
}

void setColor(int red, int green, int blue) {
  int x;

  //Now form a new RGB color
  long new_color = 0;

  new_color |= red;
  new_color <<= 8; 
  new_color |= green;
  new_color <<= 8; 
  new_color |= blue;

  strip_colors[0] = new_color; //Add the new random color to the strip

  //First, shuffle all the current colors down one spot on the strip
  for(x = (STRIP_LENGTH - 1) ; x > 0 ; x--) {
    strip_colors[x] = strip_colors[x - 1];
  }

#if DEBUG  
  Serial.print(red);
  Serial.print(", ");
  Serial.print(green);
  Serial.print(", ");
  Serial.println(blue);
#endif
}


void updateColor() {
  int x;
  float tm = (float) millis() * oSpeed;

  bValue = (int)constrain((PWM_MAX_VAL*(sin(tm)/2+rOffset)), PWM_MIN_VAL, PWM_MAX_VAL);
  gValue = (int)constrain((PWM_MAX_VAL*(sin(tm+TWO_PI/3)/2+gOffset)), PWM_MIN_VAL, PWM_MAX_VAL);
  rValue = (int)constrain((PWM_MAX_VAL*(sin(tm+2*TWO_PI/3)/2+bOffset)), PWM_MIN_VAL, PWM_MAX_VAL);

  //Now form a new RGB color
  long new_color = 0;
  //  for(x = 0 ; x < 3 ; x++){
  //    new_color <<= 8;
  //    new_color |= random(0xFF); //Give me a number from 0 to 0xFF
  //    //new_color &= 0xFFFFF0; //Force the random number to just the upper brightness levels. It sort of works.
  //  }
  new_color |= rValue;
  new_color <<= 8; 
  new_color |= gValue;
  new_color <<= 8; 
  new_color |= bValue;

  strip_colors[0] = new_color; //Add the new random color to the strip

  //First, shuffle all the current colors down one spot on the strip
  for(x = (STRIP_LENGTH - 1) ; x > 0 ; x--) {
    strip_colors[x] = strip_colors[x - 1];
  }

#if DEBUG  
  Serial.print(rValue);
  Serial.print(", ");
  Serial.print(gValue);
  Serial.print(", ");
  Serial.println(bValue);
#endif
}

//Takes the current strip color array and pushes it out
void post_frame (int ch) {
  //Each LED requires 24 bits of data
  //MSB: R7, R6, R5..., G7, G6..., B7, B6... B0 
  //Once the 24 bits have been delivered, the IC immediately relays these bits to its neighbor
  //Pulling the clock low for 500us or more causes the IC to post the data.

  for(int LED_number = 0 ; LED_number < STRIP_LENGTH ; LED_number++) {
    long this_led_color = strip_colors[LED_number]; //24 bits of color data

    for(byte color_bit = 23 ; color_bit != 255 ; color_bit--) {
      //Feed color bit 23 first (red data MSB)

      digitalWrite(CKI[ch], OUTPUT_LOW); //Only change data when clock is low

      long mask = 1L << color_bit;
      //The 1'L' forces the 1 to start as a 32 bit number, otherwise it defaults to 16-bit.

      if(this_led_color & mask) 
        digitalWrite(SDI[ch], OUTPUT_HIGH);
      else
        digitalWrite(SDI[ch], OUTPUT_LOW);

      digitalWrite(CKI[ch], OUTPUT_HIGH); //Data is latched when clock goes high
    }
  }

  //Pull clock low to put strip into reset/post mode
  digitalWrite(CKI[ch], OUTPUT_LOW);
  delayMicroseconds(500); //Wait for 500us to go into reset
}

unsigned char resetAccelerometer()
{
  writeRegister(RSTR, 0x02);
  delayMicroseconds(44);
  writeRegister(RSTR, 0x0A);
  delayMicroseconds(44);
  writeRegister(RSTR, 0x04);

  return 0;
}

// Write a byte to the acceleration sensor
unsigned char writeRegister(unsigned char registerAddress, unsigned char value)
{
  unsigned char result = 0;

  registerAddress <<= 2; // Address to be shifted left by 2
  registerAddress |= 0x02; // RW bit to be set

  //Set Chip Select pin low to signal the beginning of an SPI packet.
  digitalWrite(CS, LOW);
  //Transfer the register address over SPI.
  SPI.transfer(registerAddress);
  //Transfer the desired register value over SPI.
  SPI.transfer(value);
  //Set the Chip Select pin high to signal the end of an SPI packet.
  digitalWrite(CS, HIGH);

  return result;
}


// Read a byte from the acceleration sensor
unsigned char readRegister(unsigned char registerAddress)
{
  unsigned char result = 0;

  registerAddress <<= 2; // Address to be shifted left by 2 and RW bit to be reset

  //Set the Chip select pin low to start an SPI packet.
  digitalWrite(CS, LOW);
  //Transfer the starting register address that needs to be read.
  SPI.transfer(registerAddress);
  //Continue to read the data value fo the register
  result = SPI.transfer(0x00);
  //Set the Chips Select pin high to end the SPI packet.
  digitalWrite(CS, HIGH);

  // Return new data from RX buffer
  return result;
}






