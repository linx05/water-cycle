#include <TimeLib.h>
#include <DS3231.h>
//#include <RTClib.h>
#include <SoftwareSerial.h>
#include <EEPROMex.h>

//PORTS
byte sensorInterrupt = 0;  // 0 = digital pin 2
const int flowSensor = 2;
const int valveSensor = 3;
const int bluetoothRX = 4;
const int bluetoothTX = 5;
const int tlcA4 = 18;
const int tlcA5 = 19;

SoftwareSerial Bluetooth(bluetoothRX, bluetoothTX); // RX, TX

//RTC Clock
//RTC_DS3231 rtc;
DS3231 clock;
RTCDateTime dt;

//constantes para especificar categoria de Comandos (Command Type)
const String SYNC = "SYNC";
const String MODE = "MODE";
const String VOLUME = "VOLUME";
const String TIME = "TIME";
const String RUN_TIME = "RUN_TIME";
const String EXEC = "EXEC";

//Value Constants
const byte MODE_TIME = 0;
const byte MODE_VOLUME = 1;

//Memory values for storing variable addresses
const int MODE_MEMORY_ADDRESS = EEPROM.getAddress(sizeof(byte)); //Mode Options are 0 or 1 (By Liters or By Time)
const int TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS = MODE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (0 - 23)
const int TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS = TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (0 - 60)
const int MILILITERS_MEMORY_ADDRESS = TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(long)); // long (milliters to store)
const int RUN_TIME_MEMORY_ADDRESS_MINUTES = MILILITERS_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (number of minutes)

//Variables utilizadas para la lectura del sensor de flujo.
volatile int pulseCount; //variable para la cantidad de pulsos recibidos. Como se usa dentro de una interrupcion debe ser volatile
// The hall-effect flow sensor outputs approximately 7.11 pulses per second per litre/minute of flow.
float calibrationFactor = 7.11;
float flowRate;
unsigned int flowMilliLitres;
unsigned long totalMilliLitres;

unsigned long oldTime;

//START MEMORY GETTERS AND SETTERS
int setMemoryValue(int address, int value) {
  return EEPROM.updateInt(address, value);
}

int setMemoryValue(int address, byte value) {
  return EEPROM.updateByte(address, value);
}

int setMemoryValue(int address, long value) {
  return EEPROM.updateLong(address, value);
}

int readMemoryValue (int address) {
  return EEPROM.readInt(address);
}

byte readMemoryValueByte (int address) {
  return EEPROM.readByte(address);
}

long readMemoryValueLong (int address) {
  return EEPROM.readByte(address);
}
//END MEMORY GETTERS AND SETTERS

//START HELPER FUNCTIONS
String GetLine()
{   
  String S = "" ;
  if (Serial.available())
  {    
    char c = Serial.read(); ;
    while ( c != '\n') //Hasta que el caracter sea intro
    {     
      S = S + c ;
      delay(25) ;
      c = Serial.read();
    }
    return( S + '\n');
  }
}

String getValueHelper(String data, char separator, int index)
{
  data.trim();
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;
  
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
        found++;
        strIndex[0] = strIndex[1] + 1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
//END HELPER FUNCTIONS

class ValveState {
  public:
    byte valveMode;
    int timeToRunHour;
    int timeToRunMinute;
    int mililitersToRun;
    int minutesToRun;
    bool openValve;
    unsigned long timeToStop;
    void getSavedState (void);
    void setValveMode (byte mode);
    void setTimeToRun (int hour, int minute);
    void setMililiters (int mililiters);
    void setMinutesToRun (int minutes);
    void saveCurrentState (void);
    ValveState();
};

ValveState::ValveState(void) {
  Serial.println("Valve State Constructor");
}
void ValveState::getSavedState (void) {
  Serial.println("Getting Saved State");
  openValve = false;
  valveMode = readMemoryValueByte(MODE_MEMORY_ADDRESS);
  timeToRunHour = readMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS);
  timeToRunMinute = readMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS);
  mililitersToRun = readMemoryValueLong(MILILITERS_MEMORY_ADDRESS);
  minutesToRun = readMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES);
}
void ValveState::setValveMode (byte mode) {
  Serial.println("Valve Mode: " + String(mode));
  valveMode = mode;
  setMemoryValue(MODE_MEMORY_ADDRESS, valveMode);
  Serial.println("Valve Mode: " + String(valveMode));
}
void ValveState::setTimeToRun (int hour, int minute) {
  timeToRunHour = hour;
  timeToRunMinute = minute;
  setMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS, timeToRunHour);
  setMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS, timeToRunMinute);
}
void ValveState::setMililiters (int mililiters) {
  mililitersToRun = mililiters;
  setMemoryValue(MILILITERS_MEMORY_ADDRESS, mililitersToRun);
}
void ValveState::setMinutesToRun (int minutes) {
  minutesToRun = minutes;
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}
void ValveState::saveCurrentState (void) {
  setMemoryValue(MODE_MEMORY_ADDRESS, valveMode);
  setMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS, timeToRunHour);
  setMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS, timeToRunMinute);
  setMemoryValue(MILILITERS_MEMORY_ADDRESS, mililitersToRun);
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}

ValveState valve;

void pulseCounter () //Función que se ejecuta en interrupción  
{ 
  pulseCount++;  //incrementamos la variable de pulsos
}

uint32_t syncProvider()
{
  dt = clock.getDateTime();
  return dt.unixtime;
}

void setup()
{
  pulseCount        = 0;
  flowRate          = 0.0;
  flowMilliLitres   = 0;
  totalMilliLitres  = 0;
  oldTime           = 0;
  Serial.begin(9600); 
  Bluetooth.begin(9600); 
  while (!Serial) ; // wait until Arduino Serial Monitor opens
  pinMode(flowSensor, INPUT); 
  // The Hall-effect sensor is connected to pin 2 which uses interrupt 0.
  // Configured to trigger on a FALLING state change (transition from HIGH
  // state to LOW state)
  attachInterrupt(sensorInterrupt, pulseCounter, RISING);
  pinMode(valveSensor, OUTPUT); // Establece el pin de la valvula como salida
  // Set RTC Time if necessary
  //  if (! rtc.begin()) {
  //    Serial.println("Couldn't find RTC");
  //    while (1);
  //  }
  //
  //  if (rtc.lostPower()) {
  //    Serial.println("RTC lost power, lets set the time!");
  //    // following line sets the RTC to the date & time this sketch was compiled
  //    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  //  }
  clock.begin();
  setSyncProvider(syncProvider);   // the function to get the time from the RTC
  if(timeStatus() != timeSet) 
    Serial.println("Unable to sync with the RTC");
  else
    Serial.println("RTC has set the system time");     
  valve = ValveState();
  valve.getSavedState();
}

void runValve() {
  if((millis() - oldTime) > 1000)    // Only process counters once per second
  { 
    // Disable the interrupt while calculating flow rate and sending the value to
    // the host
    detachInterrupt(sensorInterrupt);
        
    // Because this loop may not complete in exactly 1 second intervals we calculate
    // the number of milliseconds that have passed since the last execution and use
    // that to scale the output. We also apply the calibrationFactor to scale the output
    // based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor.
    flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
    
    // Note the time this processing pass was executed. Note that because we've
    // disabled interrupts the millis() function won't actually be incrementing right
    // at this point, but it will still return the value it was set to just before
    // interrupts went away.
    oldTime = millis();
    
    // Divide the flow rate in litres/minute by 60 to determine how many litres have
    // passed through the sensor in this 1 second interval, then multiply by 1000 to
    // convert to millilitres.
    flowMilliLitres = (flowRate / 60) * 1000;
    
    // Add the millilitres passed in this second to the cumulative total
    totalMilliLitres += flowMilliLitres;
      
    unsigned int frac;
    
    // Print the flow rate for this second in litres / minute
    Serial.print("Flow rate: ");
    Serial.print(int(flowRate));  // Print the integer part of the variable
    Serial.print(".");             // Print the decimal point
    // Determine the fractional part. The 10 multiplier gives us 1 decimal place.
    frac = (flowRate - int(flowRate)) * 10;
    Serial.print(frac, DEC) ;      // Print the fractional part of the variable
    Serial.print("L/min");
    // Print the number of litres flowed in this second
    Serial.print("  Current Liquid Flowing: ");             // Output separator
    Serial.print(flowMilliLitres);
    Serial.print("mL/Sec");

    // Print the cumulative total of litres flowed since starting
    Serial.print("  Output Liquid Quantity: ");             // Output separator
    Serial.print(totalMilliLitres);
    Serial.println("mL"); 

    // Reset the pulse counter so we can start incrementing again
    pulseCount = 0;
    
    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(sensorInterrupt, pulseCounter, FALLING);
  }
}

void loop() {
  if (Bluetooth.available()) { 
    Serial.println("Bluetooth Device Connected!");     
    String opcion = Bluetooth.readString(); 
    Serial.println("Bluetooth Option: "+opcion); 
    stopValve(); 
    parseSerialBluetooth(opcion); 
  }
  if (valve.openValve == true) {
    Serial.println("Opening Valve Loop: " + String(valve.valveMode));
    digitalWrite(valveSensor, HIGH);
    if (valve.valveMode == MODE_TIME) {
      if (millis() >= valve.timeToStop) {
        stopValve();
      }
      else {
        runValve();
      }   
    }
    else if (valve.valveMode == MODE_VOLUME) {
      int amountToRun = valve.mililitersToRun;
      Serial.println("Original value: " + String(valve.mililitersToRun));
      if (totalMilliLitres >= amountToRun) {
        stopValve();
      }
      else {
        runValve();
      } 
    }
  }
  Serial.println("");
  Serial.println("VALVE MODE: " + String(valve.valveMode));
  Serial.println("TIME OF DAY TO RUN (HOUR): " + String(valve.timeToRunHour));
  Serial.println("TIME OF DAY TO RUN (MIN): " + String(valve.timeToRunMinute));
  Serial.println("MILILITERS: " + String(valve.mililitersToRun));
  Serial.println("RUN TIME: " + String(valve.minutesToRun));
  checkTime(hour(), minute());
}

void checkTime (int hour, int minute) {
  Serial.println("Hour: " + String(hour) + " Min: " + String(minute));
  if((hour == valve.timeToRunHour) && (minute == valve.timeToRunMinute)) {
    Serial.println("Open Valve due to Timer");
    openValve();
  }
}

bool parseSerialBluetooth(String command) {
  String commandInit = getValueHelper(command, ':', 0);
  if (commandInit == "LS") {
    String commandType = getValueHelper(command,':',1);
    if (commandType == MODE) {
      String systemMode = getValueHelper(command,':', 2);
      setSystemMode(systemMode);
    }
    else if (commandType == VOLUME) {
      String volume = getValueHelper(command,':', 2);
      if (volume) {
        setVolume(volume);
      }
    }
    else if (commandType == RUN_TIME) {
      String runTime = getValueHelper(command,':', 2);
      if (runTime) {
        setRunTime(runTime);
      }
    }
    else if (commandType == TIME) {
      String fullTimeString = getValueHelper(command,':', 2);
      if (fullTimeString) {
        setTime(fullTimeString);
      }
    }
    else if (commandType == EXEC) {
      openValve();
    }
    else {
      Serial.println("Other Value: " + command);
    }
    return true;
  }
  return false;
}

bool setRunTime(String minutesToRun) {
  int minutes = minutesToRun.toInt();
  if (minutes) {
    valve.setMinutesToRun(minutes);
  }
  return true;
}

bool setTime(String fullTimeString) {
  String hourString = getValueHelper(fullTimeString,',', 0);
  String minuteString = getValueHelper(fullTimeString,',', 1);
  if (hourString && minuteString) {
    int hour = hourString.toInt();
    int minute = minuteString.toInt();
    if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
      valve.setTimeToRun(hour, minute);
      return true;
    }
  }
  return false;
}

bool setVolume(String volume) {
  long vol = volume.toInt();
  if (vol) {
    valve.setMililiters(vol);
    return true;
  }
}

bool setSystemMode(String mode) {
  Serial.println("Setting System Mode: " + mode);
  if (String(mode) == VOLUME) {
     Serial.println("Setting Mode to VOLUME");
     valve.setValveMode(MODE_VOLUME);
  }
  else if (String(mode) == TIME) {
     Serial.println("Setting Mode to TIME");
     valve.setValveMode(MODE_TIME);
  }
  return true;  
}

void stopValve () {
  Serial.println("Closing Valve");
  valve.openValve = false;
  valve.timeToStop = millis();
  digitalWrite(valveSensor, LOW);
  pulseCount        = 0;
  flowRate          = 0.0;
  flowMilliLitres   = 0;
  totalMilliLitres  = 0;
  oldTime           = 0;
}

void openValve () {
  Serial.println("Opening Valve: " + String(valve.valveMode));
  valve.openValve = true;
  if (valve.valveMode == MODE_TIME) {
    openValveTime();
  }
  else if (valve.valveMode == MODE_VOLUME) {
  }
  return;
}

void openValveTime () {
  unsigned long timeToRun = ((long)valve.minutesToRun) * 60;
  timeToRun = timeToRun * 1000;
  valve.timeToStop = millis() + timeToRun;
  Serial.println("Time to Run: "+ String(timeToRun));
}


