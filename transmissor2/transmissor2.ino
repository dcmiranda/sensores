#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_GPS.h>
#include <XBee.h>
#include <FuncoesSensores.h>

///////////////////////////////////////////////////
//
// Hardware: Arduino Mega2560
// Sensores: Nenhum
//
///////////////////////////////////////////////////

//#define DEBUG

#define NODEiD "IFSP02"

XBee radio = XBee();

SoftwareSerial portaGPS (2, 3);

Adafruit_GPS GPS(&portaGPS);

// Endereco do radio XBee configurado como coordenador da rede (XBee com etiqueta 1)
XBeeAddress64 destino = XBeeAddress64(0x0013a200, 0x40c6740d);

char hS[8];
char tS[8];
char pS[8];
char latS[9];
char lonS[9];
char altS[7];
char toSend[50];
char timeStamp[13];

///////////////////////////////////////////// Funcoes do GPS
boolean usingInterrupt = false;
void useInterrupt(boolean); // Func prototype keeps Arduino 0023 happy

// Interrupt is called once a millisecond, looks for any new GPS data, and stores it
SIGNAL(TIMER0_COMPA_vect) {
  char c = GPS.read();
}

void useInterrupt(boolean v) {
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
    usingInterrupt = true;
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
    usingInterrupt = false;
  }
}
/////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////
///
///  SETUP
///
///////////////////

void setup() {
  ZBTxStatusResponse txStatus;  
  
#ifdef DEBUG
  Serial.begin(115200);
#endif
  Serial3.begin(9600);
  radio.setSerial(Serial3);

#ifdef DEBUG
  Serial.println("=============[ Inicializando nodo ]=============");
#endif
  delay(5000);

  // 1. Envia, pelo Xbee, um "Alo, fui ligado, com identificacao do no"
  createMessage (toSend, 50, "ON", NODEiD, 0);
  txStatus = sendToNode(radio, destino, toSend);

  // 1 1/2. Liga o GPS
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
#ifdef DEBUG
  Serial.print("---[ Ligando GPS... ");  
#endif
  GPS.begin(9600);
  Serial.print("Baudrate ok... ");
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  Serial.print("Comandos ok... ");
  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  Serial.println("NMEA update rate ok");
  // Request updates on antenna status, comment out to keep quiet
  // GPS.sendCommand(PGCMD_ANTENNA);
 
  // the nice thing about this code is you can have a timer0 interrupt go off
  // every 1 millisecond, and read data from the GPS for you. that makes the
  // loop code a heck of a lot easier!
  GPS.wakeup();
  useInterrupt(true);

  // 2. Espera pelo fix do GPS
#ifdef DEBUG
  Serial.print("---[ Aguardando fix do GPS...");  
#endif

  int letsWaitMore = 60;
  while ((int)GPS.fix == 0)
  {
#ifdef DEBUG
    Serial.print(".");
#endif
    delay(500);
    // if a sentence is received, we can check the checksum, parse it...
    if (GPS.newNMEAreceived()) {
      if (!GPS.parse(GPS.lastNMEA()))
        continue;  // we can fail to parse a sentence in which case we should just wait for another
    }
    // Nao precisamos mais do GPS. Deixa ele em standby
    GPS.standby();
    useInterrupt(false);

    getTimeStamp (GPS, timeStamp);
    createMessage (toSend, 50, "NF", NODEiD, 2, timeStamp, letsWaitMore);
    txStatus = sendToNode(radio, destino, toSend);
    
    if (letsWaitMore == 0)
    {
      createMessage (toSend, 50, "HF", NODEiD, 2, timeStamp, letsWaitMore);
      txStatus = sendToNode(radio, destino, toSend);
#ifdef DEBUG
      Serial.println(" Nao foi possivel conseguir fix do GPS. Halt!");
#endif
      //while(1);
      break;
    }
    letsWaitMore--;
  }

  // 3. Envia, pelo Xbee, um "Alo, estou vivo" com coordenadas, timestamp e lista de sensores disponiveis
  if (GPS.fix) {
    getTimeStamp (GPS, timeStamp);
    dtostrf(GPS.latitudeDegrees, 6, 4, latS);
    dtostrf(GPS.longitudeDegrees, 6, 4, lonS);
    dtostrf(GPS.altitude, 3, 1, altS);

    createMessage (toSend, 50, "UP", NODEiD, 4, timeStamp, latS, lonS, altS);
    txStatus = sendToNode(radio, destino, toSend);

    delay (1000);

    createMessage (toSend, 50, "SL", NODEiD, 2, timeStamp, "x");
    txStatus = sendToNode(radio, destino, toSend);
  }


  // Aguarda por mensagem do coordenador com alguma instrucao de configuracao
  //     especial (mudanca de tempos padrao, de ativacao/desativacao de sensores...)
  // Recebe a lista de sensores a ser utilizada no monitoramento OU se nao chegar nada, usar todos os sensores
  // Envia, pelo Xbee, mensagem com o tempo maximo necessario para warmup
  // Esquenta sensores pelo tempo necessario de cada um, verificando se ele ja foi aquecido/usado no setup anterior
  //     e, nesse caso, nao precisa ser aquecido novamente (usar EEPROM para isso?)
  // Envia, pelo Xbee, um "Alo, estou pronto para coletar" com timestamp
  // Definir uma interrupcao para daqui a 48h, para novo setup

#ifdef DEBUG
  Serial.println("Inicializando sensores...");
#endif

}

////////////////////////////////////////////////////////////////////////////
///
///  LOOP
///
///////////////////
void loop() {
  /* Get a new sensor event */ 
  ZBTxStatusResponse txStatus;  

  ////////////////////
  // Atualiza o timestamp a partir do GPS
  //
  // Liga o GPS
  useInterrupt(true);
  GPS.wakeup();
  while (!GPS.newNMEAreceived());
  GPS.parse(GPS.lastNMEA());   // this also sets the newNMEAreceived() flag to false
  getTimeStamp (GPS, timeStamp);
  // Desliga o GPS
  GPS.standby();
  useInterrupt(false);
  ////////////////////

  createMessage (toSend, 50, "PG", NODEiD, 1, timeStamp);
  txStatus = sendToNode(radio, destino, toSend);

  delay(10000);
}
