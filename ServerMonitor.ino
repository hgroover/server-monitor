 /*
  Server monitor
 
 Monitors temperature, current and door switch and reports to 
 an external website using GET urls.
 
 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 
 Schematic sources under ServerMonitorBoard
 EPCOS 4.7k thermistor
 resistors
 capacitors
 LED
 external mag switch
 external current loop x4
 
 Henry Groover <henry.groover@gmail.com>
 
 Original loosely based on WebHost Arduino example sketch for http access
 created 18 Dec 2009 by David A. Mellis and modified 9 Apr 2012
 by Tom Igoe, based on work by Adrian McEwen
 
 Thj
 */

#include "math.h"
#include <SPI.h>
#include <Ethernet.h>
#include <utility/w5100.h>

// Do we have ethernet?
#define HAS_ETHERNET  1

// Do we have CT connections? Set to number on analog pins 2-5
#define HAS_CURRENT_SENSOR  4

// Do we have a switch / button?
#define HAS_BUTTON 1

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0E, 0xCD, 0x59 };
// if you don't want to use DNS (and reduce your sketch size)
// use the numeric IP instead of the name for the server:
//IPAddress server(74,125,232,128);  // numeric IP for Google (no DNS)
#define SERVER_NAME  "henrygroover.net"
#define GET_SCRIPT  "/mickey4004feed.php?q=log"
char server[] = SERVER_NAME; //"www.google.com";    // name address for Google (using DNS)
// Static IP address fallback for server if DNS fails
IPAddress serverFallback(72,52,205,33);

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(10,0,1,54);
IPAddress defdns(10,0,1,1);
IPAddress gw(10,0,1,254);

const int buttonPin = 2;     // the number of the pushbutton pin
const int ledPin = 13;       // blinkenlights
const int temperaturePin = A0; // Number of analog thermistor pin
//const int currentPin = A1; // Analog pin connected to CT circuit

// variables will change:
int buttonState = 0;         // variable for reading the pushbutton status
int lastButtonState = 0;

//int minCurrentADC = 1024;
//int maxCurrentADC = 0;
//unsigned long nCurrentADC = 0;
//// Keep track of peaks since min / max last read
//// A measurable percentage will be false peaks
//int peakCounter = 0;
// Convert range (abs(max-min)) to mA RMS
float mA_RMS = 13.8095238095;
//int cur0 = 0;
//int cur1 = 0;
//int cur2 = 0;

// Get request
int getRequested = 0;
int starting = 1;
unsigned long lastBlink;
int blinkState = LOW;
unsigned long lastRequest = 0;

// Request rate in ms
unsigned long requestRate = 30 * 1000;

// Blink rate data
#define blinkOffNormal 3500
#define blinkOnNormal 1500
#define blinkOffError 500
#define blinkOnError  2000
int blinkOff = blinkOffNormal; // ms off for normal
int blinkOn = blinkOnNormal; // ms on for normal

// Serialize requests
unsigned long requestSerial = 1;

float vcc = 4.91;                       // only used for display purposes, if used
                                        // set to the measured Vcc.
float pad = 9850;                       // balance/pad resistor value, set this to
                                        // the measured resistance of your pad resistor
//float thermr = 4700; //10000;                   // thermistor nominal resistance

// manufacturer data for episco k164 10k thermistor
// simply delete this if you don't need it
// or use this idea to define your own thermistors
#define EPISCO_K164_10k 4300.0f,298.15f,10000.0f  // B,T0,R0
#define EPCOS_100k 4066.0f,298.15f,100000.0f  // B,T0,R0

#define EPCOS_4_7k  4300.0f,298.15f,4700.0f // B,T0,R0

#define CURRENT_THERMISTOR  EPCOS_4_7k
//#define CURRENT_THERMISTOR  EPCOS_4_7k

// Initialize the Ethernet client library
// with the IP address and port of the server 
// that you want to connect to (port 80 is default for HTTP):
#if HAS_ETHERNET
EthernetClient client;
#endif

#if HAS_CURRENT_SENSOR
// Current measurement object
class CurrentSensor
{
public:
   CurrentSensor();
   
   void setPin( int p ) { pin = p; }
   
   // Get RMS current in mA
   float rms();
   
   // Get raw count values
   int minCount() { return minCurrentADC; }
   int maxCount() { return maxCurrentADC; }
   
   // Get number of samples
   unsigned long numSamples() { return nCurrentADC; }
   
   // Reset sample count and min/max
   void reset();
   
   // Read sample
   void sample();
   
protected:
   int pin;
   int minCurrentADC;
   int maxCurrentADC;
   unsigned long nCurrentADC;
  // Keep track of peaks since min / max last read
  // A measurable percentage will be false peaks
  int peakCounter;
  int cur0;
  int cur1;
  int cur2;
};

CurrentSensor cs1;
#if (HAS_CURRENT_SENSOR > 1)
CurrentSensor cs2;
 #if (HAS_CURRENT_SENSOR > 2)
CurrentSensor cs3;
   #if (HAS_CURRENT_SENSOR > 3)
CurrentSensor cs4;
   #endif
 #endif
#endif

CurrentSensor::CurrentSensor()
{
 pin = 0; 
 reset();
 cur0 = 0;
 cur1 = 0;
 cur2 = 0;
}

void CurrentSensor::reset()
{
 minCurrentADC = 1024;
 maxCurrentADC = 0;
 nCurrentADC = 0;
 peakCounter = 0;
}

float CurrentSensor::rms()
{
    int countRange = 0;
    if (maxCurrentADC > minCurrentADC || maxCurrentADC != 0)
    {
      countRange = abs(maxCurrentADC - minCurrentADC);
    }
    return (mA_RMS * countRange);
}

void CurrentSensor::sample()
{
  int currentADC;
  // Read current
  nCurrentADC++;
  cur2 = cur1;
  cur1 = cur0;
  currentADC = analogRead(pin);
  cur0 = currentADC;
  // This doesn't work for detecting actual peaks - we have a small amount of noise
  // It is useful for keeping only the recent min/max history
  if (cur2 < cur1 && cur1 > cur0)
  {
    //Serial.println( "Peak @" + String(nCurrentADC) + " ADC=" + String(cur1) );
    peakCounter++;
    /*
    if (peakCounter > 180 && starting != 0 && getRequested == 0)
    {
      starting = 0;
      getRequested = 1;
    }
    */
    /*
    if (peakCounter > 240 && getRequested == 0)
    {
      //Serial.println( "Reset peak counter @" + String(nCurrentADC) + " min=" + String(minCurrentADC) + " max=" + String(maxCurrentADC) );
      peakCounter = 0;
      nCurrentADC = 0;
      minCurrentADC = 1024;
      maxCurrentADC = 0;
    }
    */
  }
//  else if (cur2 > cur1 && cur1 < cur0)
//  {
//    Serial.println( "Trough @" + String(nCurrentADC) +  " ADC=" + String(cur1) );
//  }
  if (currentADC < minCurrentADC)
  {
    minCurrentADC = currentADC;
  }
  if (currentADC > maxCurrentADC)
  {
    maxCurrentADC = currentADC;
  }
}

#endif


/****
double Thermistor(int RawADC) {
 double Temp;
 Temp = log(((10240000/RawADC) - 10000));
 Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * Temp * Temp ))* Temp );
 Temp = Temp - 273.15;            // Convert Kelvin to Celsius
 //Temp = (Temp * 9.0)/ 5.0 + 32.0; // Convert Celcius to Fahrenheit
 return Temp;
}
****/

/****
double Thermistor(int RawADC) {
 // Inputs ADC Value from Thermistor and outputs Temperature in Celsius
 //  requires: include <math.h>
 // Utilizes the Steinhart-Hart Thermistor Equation:
 //    Temperature in Kelvin = 1 / {A + B[ln(R)] + C[ln(R)]^3}
 //    where A = 0.001129148, B = 0.000234125 and C = 8.76741E-08
 long Resistance;  double Temp;  // Dual-Purpose variable to save space.
 Resistance=((10240000/RawADC) - 10000);  // Assuming a 10k Thermistor.  Calculation is actually: Resistance = (1024 * BalanceResistor/ADC) - BalanceResistor
 Temp = log(Resistance); // Saving the Log(resistance) so not to calculate it 4 times later. // "Temp" means "Temporary" on this line.
 Temp = 1 / (0.001129148 + (0.000234125 * Temp) + (0.0000000876741 * Temp * Temp * Temp));   // Now it means both "Temporary" and "Temperature"
 Temp = Temp - 273.15;  // Convert Kelvin to Celsius                                         // Now it only means "Temperature"

 // BEGIN- Remove these lines for the function not to display anything
  Serial.print("ADC: "); Serial.print(RawADC); Serial.print("/1024");  // Print out RAW ADC Number
  Serial.print(", Volts: "); printDouble(((RawADC*4.860)/1024.0),3);   // 4.860 volts is what my USB Port outputs.
  Serial.print(", Resistance: "); Serial.print(Resistance); Serial.print("ohms");
 // END- Remove these lines for the function not to display anything

 // Uncomment this line for the function to return Fahrenheit instead.
 //Temp = (Temp * 9.0)/ 5.0 + 32.0; // Convert to Fahrenheit
 return Temp;  // Return the Temperature
}
****/

/*
 * Inputs ADC Value from Thermistor and outputs Temperature in Celsius
 *  requires: include <math.h>
 * Utilizes the Steinhart-Hart Thermistor Equation:
 *    Temperature in Kelvin = 1 / {A + B[ln(R)] + C[ln(R)]3}
 *    where A = 0.001129148, B = 0.000234125 and C = 8.76741E-08
 *
 * These coefficients seem to work fairly universally, which is a bit of a 
 * surprise. 
 *
 * Schematic:
 *   [Ground] -- [10k-pad-resistor] -- | -- [thermistor] --[Vcc (5 or 3.3v)]
 *                                     |
 *                                Analog Pin 0
 *
 * In case it isn't obvious (as it wasn't to me until I thought about it), the analog ports
 * measure the voltage between 0v -> Vcc which for an Arduino is a nominal 5v, but for (say) 
 * a JeeNode, is a nominal 3.3v.
 *
 * The resistance calculation uses the ratio of the two resistors, so the voltage
 * specified above is really only required for the debugging that is commented out below
 *
 * Resistance = (1024 * PadResistance/ADC) - PadResistor 
 *
 * I have used this successfully with some CH Pipe Sensors (http://www.atcsemitec.co.uk/pdfdocs/ch.pdf)
 * which be obtained from http://www.rapidonline.co.uk.
 *
 */

/****
float Thermistor(int RawADC) {
  long Resistance;  
  float Temp;  // Dual-Purpose variable to save space.

  Resistance=((1024 * pad / RawADC) - pad); 
  Temp = log(Resistance); // Saving the Log(resistance) so not to calculate  it 4 times later
  Temp = 1 / (0.001129148 + (0.000234125 * Temp) + (0.0000000876741 * Temp * Temp * Temp));
  Temp = Temp - 273.15;  // Convert Kelvin to Celsius                      

  // BEGIN- Remove these lines for the function not to display anything
  //Serial.print("ADC: "); 
  //Serial.print(RawADC); 
  //Serial.print("/1024");                           // Print out RAW ADC Number
  //Serial.print(", vcc: ");
  //Serial.print(vcc,2);
  //Serial.print(", pad: ");
  //Serial.print(pad/1000,3);
  //Serial.print(" Kohms, Volts: "); 
  //Serial.print(((RawADC*vcc)/1024.0),3);   
  //Serial.print(", Resistance: "); 
  //Serial.print(Resistance);
  //Serial.print(" ohms, ");
  // END- Remove these lines for the function not to display anything

  // Uncomment this line for the function to return Fahrenheit instead.
  //temp = (Temp * 9.0)/ 5.0 + 32.0;                  // Convert to Fahrenheit
  return Temp;                                      // Return the Temperature
}
*****/

void printDouble(double val, byte precision) {
  // prints val with number of decimal places determine by precision
  // precision is a number from 0 to 6 indicating the desired decimal places
  // example: printDouble(3.1415, 2); // prints 3.14 (two decimal places)
  Serial.print (int(val));  //prints the int part
  if( precision > 0) {
    Serial.print("."); // print the decimal point
    unsigned long frac, mult = 1;
    byte padding = precision -1;
    while(precision--) mult *=10;
    if(val >= 0) frac = (val - int(val)) * mult; else frac = (int(val) - val) * mult;
    unsigned long frac1 = frac;
    while(frac1 /= 10) padding--;
    while(padding--) Serial.print("0");
    Serial.print(frac,DEC) ;
  }
}


// Temperature function outputs float , the actual 
// temperature
// Temperature function inputs
// 1.AnalogInputNumber - analog input to read from 
// 2.OuputUnit - output in celsius, kelvin or fahrenheit
// 3.Thermistor B parameter - found in datasheet 
// 4.Manufacturer T0 parameter - found in datasheet (kelvin)
// 5. Manufacturer R0 parameter - found in datasheet (ohms)
// 6. Your balance resistor resistance in ohms  

float Temperature(int AnalogInputNumber,/*int OutputUnit,*/float B,float T0,float R0,float R_Balance)
{
  float R,T;

  int tempADC = analogRead(AnalogInputNumber);
  R=1024.0f*R_Balance/float(tempADC)-R_Balance;
  T=1.0f/(1.0f/T0+(1.0f/B)*log(R/R0));

//  switch(OutputUnit) {
//    case T_CELSIUS :
      T-=273.15f;
//    break;
//   case T_FAHRENHEIT :
//      T=9.0f*(T-273.15f)/5.0f+32.0f;
//    break;
//    default:
//    break;
//  };
  //Serial.print( "TempADC=" + String(tempADC) + " R0=" + String(int(R0)) + " T=" );
  //printDouble(T,1);
  //Serial.println();

  return T;
}

void setup() {
 // Open serial communications and wait for port to open:
  Serial.begin(9600);
   while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }

  pinMode(buttonPin, INPUT); // Digital
  pinMode(ledPin, OUTPUT);
  //pinMode(temperaturePin, INPUT);
  digitalWrite(ledPin, blinkState);
  lastBlink = millis();
#if HAS_CURRENT_SENSOR
  cs1.setPin( A1 );
  #if (HAS_CURRENT_SENSOR > 1)
  cs2.setPin( A2 );
    #if (HAS_CURRENT_SENSOR > 2)
  cs3.setPin( A3 );
      #if (HAS_CURRENT_SENSOR > 3)
  cs4.setPin( A4 );
      #endif
    #endif
  #endif
#endif
  Serial.println( "Starting monitor v1.01" );
#if HAS_ETHERNET
  Serial.println( "Acquiring address via DHCP..." );
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, defdns, gw);
  }
  // give the Ethernet shield a second to initialize:
  delay(1000);
  Serial.print("connecting, ip=");
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(".");
  }
  // Timeout is in 100us units
  W5100.setRetransmissionTime(10000); // 1s
  W5100.setRetransmissionCount(5);  
  Serial.println( " Monitoring...");
#else
  Serial.println("no ethernet, serial only...");
#endif
}

void loop()
{
#if HAS_CURRENT_SENSOR
  cs1.sample();
  #if (HAS_CURRENT_SENSOR > 1)
  cs2.sample();
    #if (HAS_CURRENT_SENSOR > 2)
  cs3.sample();
      #if (HAS_CURRENT_SENSOR > 3)
  cs4.sample();
      #endif
    #endif
  #endif
 #endif
   // Check for alarm conditions
    double Temp = Temperature(A0, CURRENT_THERMISTOR, 10000.0f);
#if HAS_BUTTON
    // read the state of the pushbutton value:
    buttonState = digitalRead(buttonPin);
    if (buttonState != lastButtonState)
    {
      // Door opened?
      if (buttonState == LOW && getRequested == 0) getRequested = 1;
      lastButtonState = buttonState;
    }
#endif
  if (Temp >= 32.0)
  {
    //if (blinkOn != blinkOnError)
    //{
    //  Serial.println( "Temperature high: " + String(int(Temp * 10)) );
    //}
    blinkOn = blinkOnError;
    blinkOff = blinkOffError;
  }
  else if (blinkOn != blinkOnNormal)
  {
    //Serial.println( "Temperature normal: " + String(int(Temp * 10)) );
    blinkOn = blinkOnNormal;
    blinkOff = blinkOffNormal;
  }
  if (getRequested > 0)
  {
    getRequested = -1;
    lastRequest = millis();
  #if HAS_ETHERNET
  // if you get a connection, report back via serial:
  // If name server lookup fails, try hardcoded server IP
  // DBG
  //Serial.println( "Trying to connect..." );
  if (//client.connect(server, 80) || 
      client.connect(serverFallback, 80)) {
    // DBG
    Serial.println( "Sending http request..." );
    //Serial.println("connected");
    // Make a HTTP request:
    //client.println("GET /search?q=arduino HTTP/1.1");
    //client.println("Host: www.google.com");
    //Temp = Thermistor(analogRead(temperaturePin));
    client.println("GET " GET_SCRIPT "&ser=" + String(requestSerial) 
      + "&temp=" + String(int(Temp*10)) 
#if HAS_BUTTON
    // check if the pushbutton is pressed.
    // if it is, the buttonState is HIGH:
    + "&dc=" + String(buttonState == HIGH ? 1 : 0)
#endif
 #if HAS_CURRENT_SENSOR
      + "&ma1=" + String(int(cs1.rms())) 
      #if 0
      + "&cmin=" + String(minCurrentADC) + "&cmax=" + String(maxCurrentADC) + "&ccount=" + String(nCurrentADC) 
      + "&hist=" + String(cur2) + "," + String(cur1) + "," + String(cur0) 
      #endif
  #if (HAS_CURRENT_SENSOR > 1)
      + "&ma2=" + String(int(cs2.rms()))
    #if (HAS_CURRENT_SENSOR > 2)
      + "&ma3=" + String(int(cs3.rms()))
      #if (HAS_CURRENT_SENSOR > 3)
      + "&ma4=" + String(int(cs4.rms()))
      #endif
    #endif
  #endif
 #endif
      + " HTTP/1.1" );
    client.println("Host: " SERVER_NAME);
    client.println("Connection: close");
    client.println();
    requestSerial++;
    #if (HAS_CURRENT_SENSOR > 0)
    cs1.reset();
      #if (HAS_CURRENT_SENSOR > 1)
    cs2.reset();
        #if (HAS_CURRENT_SENSOR > 2)
    cs3.reset();
          #if (HAS_CURRENT_SENSOR > 3)
    cs4.reset();
          #endif
        #endif
      #endif
    #endif
    /***
 #if HAS_CURRENT_SENSOR
    minCurrentADC = 1024;
    maxCurrentADC = 0;
    nCurrentADC = 0;
#endif
   ***/
  } 
  else {
    // kf you didn't get a connection to the server:
    Serial.println("connection failed");
    getRequested = 0;
  }
  #else
    //Temp = Thermistor(analogRead(temperaturePin));
    double Temp = Temperature(A0, CURRENT_THERMISTOR, 10000.0f);
    Serial.println("Serial=" + String(requestSerial) + " Temp=" + String(int(Temp*10))
  #if HAS_CURRENT_SENSOR
    + " mA=" + String(cs1.rms()) 
    #if 0
    + " cmin=" + String(minCurrentADC) + " cmax=" + String(maxCurrentADC) + " ccount=" + String(nCurrentADC) 
    + " hist=" + String(cur2) + "," + String(cur1) + "," + String(cur0) 
    #endif
    #endif
    );
    getRequested = 0;
  #endif
  }

#if HAS_ETHERNET  
  // if there are incoming bytes available 
  // from the server, read them and print them:
  if (client.available()) {
    char c = client.read();
    Serial.print(c);
  }
#endif

  // if the server's disconnected, stop the client:
  if (getRequested < 0 
  #if HAS_ETHERNET
    && !client.connected()
    #endif
    ) {
      #if HAS_ETHERNET
    //Serial.println();
    //Serial.println("disconnecting.");
    client.stop();
    #endif
    getRequested = 0;

    // do nothing forevermore:
    //while(true);
  }
#if HAS_BUTTON && 0
    // read the state of the pushbutton value:
  buttonState = digitalRead(buttonPin);

  // check if the pushbutton is pressed.
  // if it is, the buttonState is HIGH:
  if (buttonState == HIGH && getRequested == 0) {     
    // turn LED on:    
    //digitalWrite(ledPin, HIGH);  
    getRequested = 1;
    Serial.println("Sending request again...");
  } 
 #endif
//  else {
//    // turn LED off:
//    digitalWrite(ledPin, LOW); 
//  }

  unsigned long curTime = millis();
  if (curTime < lastBlink || (blinkState == LOW && curTime - lastBlink >= blinkOff) || (blinkState == HIGH && curTime - lastBlink >= blinkOn))
  {
     lastBlink = curTime;
     blinkState = (blinkState == HIGH) ? LOW : HIGH;
     //Serial.println("Time=" + String(curTime) + " state=" + String(blinkState));
     digitalWrite(ledPin, blinkState);
  }
  if (getRequested == 0 && (lastRequest > curTime || curTime - lastRequest >= requestRate))
  {
    getRequested = 1;
  }
}

