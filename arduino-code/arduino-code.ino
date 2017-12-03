#include <SoftwareSerial.h>
//#include <EEPROM.h>
#include <EEPROMex.h>

SoftwareSerial Bluetooth(4,2); // RX, TX
String opcion;

//constantes para especificar categoria de Comandos (Command Type)
const String SYNC = "SYNC";
const String MODE = "MODE";
const String VOLUME = "VOLUME";
const String TIME = "TIME";
const String EXEC = "EXEC";

//Memory values for storing variable addresses
const int MODE_MEMORY_ADDRESS = EEPROM.getAddress(sizeof(byte)); //Mode Options are 0 or 1 (By Liters or By Time)
const int TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS = MODE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (0 - 23
const int TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS = TIME_OF_DAY_RUN_HOUR_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (0 - 60)
const int MILLITERS_MEMORY_ADDRESS = TIME_OF_DAY_RUN_MINUTE_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(long)); // long (milliters to store)
const int RUN_TIME_MEMORY_ADDRESS_MINUTES = MILLITERS_MEMORY_ADDRESS + EEPROM.getAddress(sizeof(int)); // int (number of minutes)

int cantidad=0; //Volumen de agua requerida por el usuario en litros.
int valor=0; //Valor que viene "tecla" usando una conversion numerica: atoi().

//Variables utilizadas para la lectura del sensor de flujo.
volatile int NumPulsos; //variable para la cantidad de pulsos recibidos. Como se usa dentro de una interrupcion debe ser volatile
int PinSensor = 5;    //Sensor conectado en el pin 2
float factor_conversion=7.11; //para convertir de frecuencia a caudal
float volumen=0;
long dt=0; //variación de tiempo por cada bucle
long t0=0; //millis() del bucle anterior
 
//Valvula solonoide.
int solenoidPin = 3; // Este es el pin de salida para la valvula solonoide

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
  EEPROM.readInt(address);
}

byte readMemoryValueByte (int address) {
    EEPROM.readByte(address);
}

long readMemoryValueLong (int address) {
    EEPROM.readByte(address);
}
//END MEMORY GETTERS AND SETTERS

void setup()
{
  Serial.begin(9600); 
  Bluetooth.begin(9600);
  pinMode(PinSensor, INPUT); 
  attachInterrupt(0,ContarPulsos,RISING); //(Interrupción 0(Pin2),función,Flanco de subida)
  t0=millis();
  
  pinMode (solenoidPin, OUTPUT); // Establece el pin de la valvula como salida 
}

void loop() {
  if (Bluetooth.available()) {
      String opcion = Bluetooth.readString();
      Serial.println("Opcion: "+opcion);
      parseSerialBluetooth(opcion);
  }
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

bool setSystemMode(String mode) {
  if (mode == TIME) {
     // TODO: set mode value to "VOLUME";
     Serial.println("Setting Mode to VOLUME");
  }
    else if (mode == VOLUME) {
     // TODO: set mode value to "TIME";
     Serial.println("Setting Mode to VOLUME");
  }
  else {
    //Empty Block
  }
  return true;  
}

bool parseSerialBluetooth(String command) {
  String commandInit = getValueHelper(command, ':', 0);
  if (commandInit == "YS") {
    String commandType = getValueHelper(command,':',1);
    if (commandType == MODE) {
      String systemMode = getValueHelper(command,':', 2);
      setSystemMode(systemMode);
    }
    else {
      Serial.println("Other Value: " + command);
    }
    return true;
  }
  return false;
}

void openValve () {
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


