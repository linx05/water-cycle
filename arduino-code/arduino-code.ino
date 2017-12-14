#include <TimeLib.h>
#include <DS3231.h>
//#include <RTClib.h>
#include <SoftwareSerial.h>
#include <EEPROMex.h>

//PORTS
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
const int LITERS_MEMORY_ADDRESS = TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(long)); // long (milliters to store)
const int RUN_TIME_MEMORY_ADDRESS_MINUTES = LITERS_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (number of minutes)

int closeValveInterval; //Interval Container - Used to Cancel Timeout Callback

//Variables utilizadas para la lectura del sensor de flujo.
volatile int NumPulsos; //variable para la cantidad de pulsos recibidos. Como se usa dentro de una interrupcion debe ser volatile
float factor_conversion=7.11; //para convertir de frecuencia a caudal
long t0 = 0; //millis() del bucle anterior

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
 {   String S = "" ;
     if (Serial.available())
        {    char c = Serial.read(); ;
              while ( c != '\n') //Hasta que el caracter sea intro
                {     S = S + c ;
                      delay(25) ;
                      c = Serial.read();
                }
              return( S + '\n') ;
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
    int litersToRun;
    int minutesToRun;
    bool openValve;
    void getSavedState (void);
    void setValveMode (byte mode);
    void setTimeToRun (int hour, int minute);
    void setLiters (int liters);
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
  litersToRun = readMemoryValueLong(LITERS_MEMORY_ADDRESS);
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
void ValveState::setLiters (int liters) {
  litersToRun = liters;
  setMemoryValue(LITERS_MEMORY_ADDRESS, litersToRun);
}
void ValveState::setMinutesToRun (int minutes) {
  minutesToRun = minutes;
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}
void ValveState::saveCurrentState (void) {
  setMemoryValue(MODE_MEMORY_ADDRESS, valveMode);
  setMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS, timeToRunHour);
  setMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS, timeToRunMinute);
  setMemoryValue(LITERS_MEMORY_ADDRESS, litersToRun);
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}

ValveState valve;

void countPulses () //Función que se ejecuta en interrupción  
{ 
  NumPulsos++;  //incrementamos la variable de pulsos
} 

int ObtenerFrecuecia() //Función para obtener frecuencia de los pulsos
{
  Serial.println("Entrando a obtenerFrecuencia");
  int frecuencia;
  NumPulsos = 0; //Ponemos a 0 el número de pulsos
  interrupts(); //Habilitamos las interrupciones
  delay(1000); //muestra de 1 segundo
  noInterrupts(); //Deshabilitamos las interrupciones
  frecuencia=NumPulsos; //Hz(pulsos por segundo)
  Serial.println("Saliendo de obtenerFrecuencia");
  return frecuencia;
}

uint32_t syncProvider()
{
  dt = clock.getDateTime();
  return dt.unixtime;
}

void setup()
{
  Serial.begin(9600); 
  Bluetooth.begin(9600); 
  while (!Serial) ; // wait until Arduino Serial Monitor opens
  pinMode(flowSensor, INPUT); 
  attachInterrupt(0, countPulses, RISING); //(Interrupción 0(Pin2),función,Flanco de subida)
  t0 = millis();
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

void loop() {
  checkTime(hour(), minute());
  if (Bluetooth.available()) {
    Serial.println("Bluetooth Device Connected!");    
    String opcion = Bluetooth.readString();
    Serial.println("Bluetooth Option: "+opcion);
    stopValve();
    parseSerialBluetooth(opcion);
  }
  Serial.println("");
  Serial.println("VALVE MODE: " + String(valve.valveMode));
  Serial.println("TIME OF DAY TO RUN (HOUR): " + String(valve.timeToRunHour));
  Serial.println("TIME OF DAY TO RUN (MIN): " + String(valve.timeToRunMinute));
  Serial.println("LITERS: " + String(valve.litersToRun));
  Serial.println("RUN TIME: " + String(valve.minutesToRun));
  delay(500);
  if (valve.openValve == true) {
    Serial.println("Opening Valve Loop: " + String(valve.valveMode));
    digitalWrite(valveSensor, HIGH);
    if (valve.valveMode == MODE_TIME) {
      unsigned long timeToRun = ((long)valve.minutesToRun) * 60;
      timeToRun = timeToRun * 1000;
      delay(timeToRun);      
    }
    else if (valve.valveMode == MODE_VOLUME) {
      int cantidad = valve.litersToRun;
      Serial.println("Original value: " + String(valve.litersToRun));
      float volumen = 0;
      long dt = 0; //variación de tiempo por cada bucle
      Serial.println("Liters to run: " + String(cantidad));
      while(volumen <= cantidad)
      {
        Serial.println(" Volumen: " + String(volumen) + " Cantidad: " + cantidad);
        float frecuencia = ObtenerFrecuecia(); //obtenemos la frecuencia de los pulsos en Hz
        Serial.println("Frequencia: " + String(frecuencia));
        float caudal = frecuencia / factor_conversion; //calculamos el caudal en L/m
        Serial.println("CaudalLM: " + String(caudal));
        dt = millis()- t0; //calculamos la variación de tiempo
        t0 = millis();
        volumen = volumen + (caudal/60)*(dt/1000);
        Serial.println("Volumen: " + String(volumen));
        Serial.print ("Caudal: ");
        Serial.print (caudal);
        Serial.print (" L/mintVolumen: ");
        Serial.print (volumen);
        Serial.println (" L");
      }
    }
    valve.openValve = false;
    Serial.println("Closing Valve");
    digitalWrite(valveSensor, LOW);
  }
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
    valve.setLiters(vol);
    return true;
  }
}

bool setSystemMode(String mode) {
  Serial.println("Setting System Mode: " + mode);
//  mode.trim();
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
  Serial.println("Stopping Valve");
  valve.openValve = false;
  return digitalWrite(valveSensor, LOW);
}

void openValve () {
  Serial.println("Opening Valve: " + String(valve.valveMode));
  valve.openValve = true;
//  if (valve.valveMode == MODE_TIME) {
//    openValveTime();
//  }
//  else if (valve.valveMode == MODE_VOLUME) {
//    openValveVolume();
//  }
  return;
}

void openValveTime () {
  digitalWrite(valveSensor, HIGH);
  unsigned long timeToRun = ((long)valve.minutesToRun) * 60;
  timeToRun = timeToRun * 1000;
  Serial.println("Time to Run: "+ String(timeToRun));
  // THIS IS CANCER but setTimeout doesn't seem to work :(
  delay(timeToRun);
  return stopValve();
}

void openValveVolume () {
  digitalWrite(valveSensor, HIGH);
  while(volumen<=cantidad)
  {
   float frecuencia=ObtenerFrecuecia(); //obtenemos la frecuencia de los pulsos en Hz
   float caudal_L_m=frecuencia/factor_conversion; //calculamos el caudal en L/m
   dt=millis()-t0; //calculamos la variación de tiempo
   t0=millis();
   volumen=volumen+(caudal_L_m/60)*(dt/1000);
   Serial.print ("Caudal: ");
   Serial.print (caudal_L_m,3);
   Serial.print (" L/mintVolumen: ");
   Serial.print (volumen,3);
   Serial.println (" L");
  }
  return stopValve();
}


