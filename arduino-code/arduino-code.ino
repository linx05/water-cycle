#include <SoftwareSerial.h>
#include <EEPROMex.h>
#include <SimpleTimer.h>

//PORTS
const int flowSensor = 2;
const int valveSensor = 3;
const int bluetoothRX = 4;
const int bluetoothTX = 5;
const int tlcA4 = 18;
const int tlcA5 = 19;

SoftwareSerial Bluetooth(bluetoothRX, bluetoothTX); // RX, TX

// the timer object
SimpleTimer timer;

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
const int MILLITERS_MEMORY_ADDRESS = TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(long)); // long (milliters to store)
const int RUN_TIME_MEMORY_ADDRESS_MINUTES = MILLITERS_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (number of minutes)

int cantidad=0; //Volumen de agua requerida por el usuario en litros.
int closeValveInterval; //Interval Container - Used to Cancel Timeout Callback

//Variables utilizadas para la lectura del sensor de flujo.
volatile int NumPulsos; //variable para la cantidad de pulsos recibidos. Como se usa dentro de una interrupcion debe ser volatile
float factor_conversion=7.11; //para convertir de frecuencia a caudal
float volumen=0;
long dt=0; //variación de tiempo por cada bucle
long t0=0; //millis() del bucle anterior

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
    long mililitersToRun;
    int minutesToRun;
    void getSavedState (void);
    void setValveMode (byte mode);
    void setTimeToRun (int hour, int minute);
    void setMililiters (long mililiters);
    void setMinutesToRun (int minutes);
    void saveCurrentState (void);
    ValveState();
};

ValveState::ValveState(void) {
  Serial.println("Valve State Constructor!");
//  getSavedState();
}
void ValveState::getSavedState (void) {
  Serial.println("Getting Saved State");
  valveMode = readMemoryValueByte(MODE_MEMORY_ADDRESS);
  timeToRunHour = readMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS);
  timeToRunMinute = readMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS);
  mililitersToRun = readMemoryValueLong(MILLITERS_MEMORY_ADDRESS);
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
void ValveState::setMililiters (long mililiters) {
  mililitersToRun = mililiters;
  setMemoryValue(MILLITERS_MEMORY_ADDRESS, mililitersToRun);
}
void ValveState::setMinutesToRun (int minutes) {
  minutesToRun = minutes;
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}
void ValveState::saveCurrentState (void) {
  setMemoryValue(MODE_MEMORY_ADDRESS, valveMode);
  setMemoryValue(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS, timeToRunHour);
  setMemoryValue(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS, timeToRunMinute);
  setMemoryValue(MILLITERS_MEMORY_ADDRESS, mililitersToRun);
  setMemoryValue(RUN_TIME_MEMORY_ADDRESS_MINUTES, minutesToRun);
}

ValveState valve;

void setup()
{
  Serial.begin(9600); 
  Bluetooth.begin(9600);
  pinMode(flowSensor, INPUT); 
  attachInterrupt(0,ContarPulsos,RISING); //(Interrupción 0(Pin2),función,Flanco de subida)
  pinMode(valveSensor, OUTPUT); // Establece el pin de la valvula como salida
  t0= millis();
//  Serial.println(MODE_MEMORY_ADDRESS);
//  Serial.println(TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS);
//  Serial.println(TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS);
//  Serial.println(MILLITERS_MEMORY_ADDRESS);
//  Serial.println(RUN_TIME_MEMORY_ADDRESS_MINUTES);
  valve = ValveState();
  valve.getSavedState();
}

void loop() {
  if (Bluetooth.available()) {
    Serial.println("Bluetooth Device Connected!");    
    String opcion = Bluetooth.readString();
    Serial.println("Bluetooth Option: "+opcion);
    stopValve();
    parseSerialBluetooth(opcion);
  }
  Serial.println("MODE: " + String(valve.valveMode));
  Serial.println("TIME OF DAY TO RUN (HOUR): " + String(valve.timeToRunHour));
  Serial.println("TIME OF DAY TO RUN (MIN): " + String(valve.timeToRunMinute));
  Serial.println("MILILITERS: " + String(valve.mililitersToRun));
  Serial.println("RUN TIME: " + String(valve.minutesToRun));
  
  delay(1000);
}

void ContarPulsos () //Función que se ejecuta en interrupción  
{ 
  NumPulsos++;  //incrementamos la variable de pulsos
} 

int ObtenerFrecuecia() //Función para obtener frecuencia de los pulsos
{
  int frecuencia;
  NumPulsos = 0;   //Ponemos a 0 el número de pulsos
  interrupts();    //Habilitamos las interrupciones
  delay(1000);   //muestra de 1 segundo
  noInterrupts(); //Deshabilitamos  las interrupciones
  frecuencia=NumPulsos; //Hz(pulsos por segundo)
  return frecuencia;
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
      //TODO: RUN OPEN VALVE
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
  digitalWrite(valveSensor, LOW);
}

void openValve () {
  if (valve.valveMode == MODE_TIME) {
    openValveTime();
  }
  else if (valve.valveMode == MODE_VOLUME) {
    openValveVolume();
  }
}

void openValveTime () {
  digitalWrite(valveSensor, HIGH);
  long timeToRun = valve.minutesToRun * 60 * 1000;
  //After valve has run it's course of time close the valve
  closeValveInterval = timer.setTimeout(timeToRun, stopValve);
}

void openValveVolume () {
  while(volumen<=cantidad)
  {
   float frecuencia=ObtenerFrecuecia(); //obtenemos la frecuencia de los pulsos en Hz
   float caudal_L_m=frecuencia/factor_conversion; //calculamos el caudal en L/m
   dt=millis()-t0; //calculamos la variación de tiempo
   t0=millis();
   volumen=volumen+(caudal_L_m/60)*(dt/1000);
  
   //Enviamos por el puerto serie-Control
   Serial.print ("Caudal: ");
   Serial.print (caudal_L_m,3);
   Serial.print ("L/mintVolumen: ");
   Serial.print (volumen,3);
   Serial.println (" L");
  }
}


