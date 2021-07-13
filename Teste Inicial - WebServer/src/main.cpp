/* Lógica 
 * Se existe WiFi disponível para conexão
 *    Adquirir o tempo e calibrar com NTP OK
 *    Acender/apagar a lâmpada nos momentos corretos OK
 *    Define semanalmente o horário de acender/apagar a lâmpada OK
 *      Salvar os dados não constantes na EEPROM OK
 *      Garantir as atualizações de horário apenas as segundas-feiras as 12h OK
 *      Parar a atualização semanal quando toda a transição for completa  OK
 *    Status da lâmpada + os horários de ligar/desligar + a possibilidade de selecionar entre manual e automático (+ botão para ligar/desligar) - WEB
 *    GET + Já vai no index.html + toggle switch (GET/POST) if(SetManual) - sobe uma página com 2 botões liga/desliga
 * 
 * Se não existe uma rede WiFi a se conectar -- DEPOIS EU RESOLVO
 *    Se o WiFi.status é qualquer coisa diferente de WL_CONNECTED por muito tempo - instancia um AP
 * 
 * WatchDog ativo em caso de travamento
 * 
 * Autor: João Senírio de Sousa Costa 
 * Projeto: 
 * 
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <LittleFS.h>
#include <EEPROM.h>

#include "Eeprom_WriteRead.h"

#define RelayPin 5

#define time_Resync 3600
#define secondsToNoon SECS_PER_DAY/2
#define minutesReduction 30
#define dayToUpdate 3 //Sunday = 1, Monday = 2 [...], Saturday = 7
#define EepromPosition_ManualMode 18

typedef struct Time_OnOff{
  const time_t seconds2FirstTurn_on = (17 * SECS_PER_HOUR);  
  time_t seconds2FirstTurn_off;                             
  time_t seconds2SecondTurn_on;
  const time_t seconds2SecondTurn_off = (6 * SECS_PER_HOUR);
}Time_OnOff; 

//Packet NTP documents https://labs.apnic.net/?p=462
static const char *ntpServerName = "gps.ntp.br";
const char *ssid = "2.4G HOMEWIFI_392";
const char *password = "07425264";
//const char *ssid = "Asus_Neider";
//const char *password = "6c4a2d968e11";
const char *ssidAP = "Chicken Hue";

const int timeZone = -3;   
const int NTP_PACKET_SIZE = 48; //NTP time is in the first 48 bytes of message 
const unsigned int localPort = 8888; // local port to listen for UDP packets

time_t prevDisplay = 0;  //When the digital clock was displayed
time_t nextMonday = 0; 
time_t SecsElaspsedToday = 0; 

uint8 ntp_sinc = 0;

boolean ToUpdate = true; 
boolean Handle_ToUpdate = false; 
boolean RelayStatus; 
boolean ManualMode;
boolean LateTime = false;

byte packetBuffer[NTP_PACKET_SIZE]; //Buffer to hold incoming & outgoing packets
Time_OnOff Time2Turn_OnOff;

// -- Objects definition --
WiFiServer server(80);
WiFiUDP Udp;
WiFiClient client;  
IPAddress wifi_ip(192, 168, 0, 135);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(181, 213, 132, 3);
IPAddress dns2(181, 213, 132, 2);

// -- Functions prototype --
void connectWiFi(); 
void readFS(String* webpage, boolean MainPage);
void sendMainPage(String webpage);
void sendSecundaryPage(String webpage);
void sendNTPpacket(IPAddress &address);
void UpdateTime();
void sendRelayStatus(boolean* RelayStatus);
void saveEEPROM_timeValues(Time_OnOff *p);
void ReadEEPROM_timeValues(Time_OnOff *p);
int command(String http_req);
time_t getNtpTime();

// -- Void Setup --
void setup() {
  Serial.begin(115200);
  connectWiFi(); //Initializing WiFi connection   

  EEPROM.begin(32); //Initialize EEPROM with 32 bytes

  if(!LittleFS.begin()){ //Initializing FileSystem
    Serial.println("Error while starting LittleFS");
    while(true);    
  } 

  Udp.begin(localPort);
  setSyncProvider(getNtpTime);  //Identify the external time provider
  setSyncInterval(time_Resync); //Set the number of seconds between re-sync

  //Rescue volatile time values storage on EEPROM
  /*saveEEPROM_timeValues(&Time2Turn_OnOff);
  EEPROM.write(16, ToUpdate);
  EEPROM.write(17, Handle_ToUpdate);
  EEPROM.commit();*/
  /*EEPROM.write(EepromPosition_ManualMode, 0);
  EEPROM.commit();*/
  ReadEEPROM_timeValues(&Time2Turn_OnOff);
  ManualMode = EEPROM.read(EepromPosition_ManualMode);
  
  //Initialize relay as output - off
  pinMode(RelayPin, OUTPUT); 
  digitalWrite(RelayPin, LOW); 

  server.begin();  

  //time_t int64 - 8bytes - vão até a 16º posição
  //boolean - 1byte
  
  SecsElaspsedToday = elapsedSecsToday(now());
  Serial.print("\nSecsElapsedToday: ");
  Serial.println(SecsElaspsedToday);
  Serial.print("Seconds to first turn off: ");
  Serial.println(Time2Turn_OnOff.seconds2FirstTurn_off);
  Serial.print("Seconds to second turn on: ");
  Serial.println(Time2Turn_OnOff.seconds2SecondTurn_on);

  Serial.print("ToUpdate value: ");
  Serial.println(EEPROM.read(16));
  Serial.print("Handle_ToUpdate value: ");
  Serial.println(EEPROM.read(17));
}
 
// -- Void Loop -- 
void loop(){
  String http_request = "";
  String index_response = ""; 
  uint8 prev_ntpSinc; 

  //handle all web clients commands
  client = server.available(); 
  if(client){
    while(client.connected()){
      if(client.available()){
        http_request = "";
        http_request = client.readString();        
        //Serial.println(http_request);
        client.flush();
      }
      if(http_request.length() > 0){
        if(!(http_request.startsWith("GET /favicon"))){
          switch(command(http_request)){
          case 1:
            if(ManualMode){
              readFS(&index_response, false);
              sendSecundaryPage(index_response);
            }else{
              readFS(&index_response, true);
              sendMainPage(index_response);
            }
            break;      
          case 2: 
            sendRelayStatus(&RelayStatus);
            break; 
          case 3: 
            EEPROM.write(EepromPosition_ManualMode, 1);
            EEPROM.commit();
            ManualMode = true;
            readFS(&index_response, false);
            sendSecundaryPage(index_response);
            break;  
          case 4: 
            if(ManualMode){
              digitalWrite(RelayPin, LOW);
              RelayStatus = false;
            }
            break;
          case 5: 
            if(ManualMode){
              digitalWrite(RelayPin, HIGH);
              RelayStatus = true;
            }
            break;
          case 6: 
            EEPROM.write(EepromPosition_ManualMode, 0);
            EEPROM.commit();
            ManualMode = false;
            readFS(&index_response, true);
            sendMainPage(index_response);
            break;
          }
        }
        break;
      }
    }
    client.stop(); 
  }

  //Set Manual mode when time is desyncronized by NTP
  if((ntp_sinc > 23) && (!LateTime)){
    LateTime = true;
    EEPROM.write(EepromPosition_ManualMode, 1);
    EEPROM.commit();
    ManualMode = true;
  }else if((ntp_sinc == 0) && (LateTime)){
    LateTime = false; 
    EEPROM.write(EepromPosition_ManualMode, 0);
    EEPROM.commit();
    ManualMode = false;
  }

  //Only update agended time to turn on/off if afternoon values are different  
  if((Time2Turn_OnOff.seconds2FirstTurn_on != Time2Turn_OnOff.seconds2FirstTurn_off)){
    if((weekday(now()) == dayToUpdate) && (elapsedSecsToday(now()) > secondsToNoon) && (EEPROM.read(16) == 1)){ //Only update on pre-defined day and time
      UpdateTime();
      EEPROM.write(16, 0); //saved flag to indicate that update already occurred - ToUpdate
      EEPROM.write(17, 1); 
      EEPROM.commit(); 
      Serial.println("Dia de trocar o horário!");
      Serial.print("Novos segundos para apagar a luz na tarde: ");
      Serial.println(Time2Turn_OnOff.seconds2FirstTurn_off);
      Serial.print("Novos segundos para acender a luz na tarde: ");
      Serial.println(Time2Turn_OnOff.seconds2SecondTurn_on); 

      Serial.print("ToUpdate value: ");
      Serial.println(EEPROM.read(16));
      Serial.print("Handle_ToUpdate value: ");
      Serial.println(EEPROM.read(17));
    }else if((weekday(now()) > dayToUpdate) && (EEPROM.read(17) == 1)){
      EEPROM.write(16, 1);
      EEPROM.write(17, 0);
      EEPROM.commit();

      Serial.print("ELSE- ToUpdate value: ");
      Serial.println(EEPROM.read(16));
      Serial.print("ELSE - Handle_ToUpdate value: ");
      Serial.println(EEPROM.read(17));
    }
  }

  if(!ManualMode){
    SecsElaspsedToday = elapsedSecsToday(now()); 
    if((SecsElaspsedToday >= Time2Turn_OnOff.seconds2FirstTurn_on) && (SecsElaspsedToday < Time2Turn_OnOff.seconds2FirstTurn_off)){
      digitalWrite(RelayPin, HIGH); //Relay on
      RelayStatus = true;
    }else if(SecsElaspsedToday > Time2Turn_OnOff.seconds2FirstTurn_off){
      digitalWrite(RelayPin, LOW); //Relay off
      RelayStatus = false; 
    }

    if((SecsElaspsedToday >= Time2Turn_OnOff.seconds2SecondTurn_on) && (SecsElaspsedToday < Time2Turn_OnOff.seconds2SecondTurn_off)){
      digitalWrite(RelayPin, HIGH); //Relay on
      RelayStatus = true;
    }else if((SecsElaspsedToday > Time2Turn_OnOff.seconds2SecondTurn_off) && (SecsElaspsedToday < Time2Turn_OnOff.seconds2FirstTurn_on)){
      digitalWrite(RelayPin, LOW); //Relay off
      RelayStatus = false;   
    }

    if(SecsElaspsedToday < Time2Turn_OnOff.seconds2SecondTurn_on){
      digitalWrite(RelayPin, LOW); //Relay off
      RelayStatus = false;
    }
  }
}

// -- Development of assistant functions --
void connectWiFi(){
  Serial.print("\nConnecting ");

  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("Chicken Hue");
  WiFi.setAutoConnect(false);  //Don't connect to last used AP
  WiFi.setAutoReconnect(true); //Reconnect to last used AP in case it is disconnected
  WiFi.begin(ssid, password);
  WiFi.config(wifi_ip, gateway, subnet, dns1, dns2); //define fixes IP

  while(WiFi.status() != WL_CONNECTED){        
      Serial.print(".");
      delay(500);
  }

  Serial.print("\nDevice is connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  WiFi.softAP(ssidAP);
  //Serial.println(WiFi.softAPIP());
  //if(MDNS.begin("esp8266")) Serial.println("MDS responder started");
}
 
//-----------------------------------------------------------------------------------------------------------------
void readFS(String* webpage, boolean MainPage){
  if(MainPage){
    if(LittleFS.exists("./MainPage.html")){
      File archive = LittleFS.open("./MainPage.html", "r");
      *webpage = archive.readString();    
    }
  }else{
    if(LittleFS.exists("./ManualMode.html")){
      File archive = LittleFS.open("./ManualMode.html", "r");
      *webpage = archive.readString();    
    }
  }
}

//-----------------------------------------------------------------------------------------------------------------
void sendMainPage(String webpage){
  String TurnOff = String(Time2Turn_OnOff.seconds2FirstTurn_off, DEC);
  String TurnOn = String(Time2Turn_OnOff.seconds2SecondTurn_on, DEC);  
  webpage.replace("#seconds2TurnOff#", TurnOff);
  webpage.replace("#seconds2TurnOn#", TurnOn);

  client.println("HTTP/1.0 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println("");
  client.println(webpage);
}

//-----------------------------------------------------------------------------------------------------------------
void sendSecundaryPage(String webpage){
  client.println("HTTP/1.0 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println("");
  client.println(webpage);
}

//-----------------------------------------------------------------------------------------------------------------
time_t getNtpTime(){
  IPAddress ntpServerIP; //NTP server's ip address

  while(Udp.parsePacket() > 0); //Discard any previously received packets
  Serial.println("Transmit NTP Request");
  //Get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP); //Convert domain into an IPAdress
  Serial.print("NTP Server: ");
  Serial.print(ntpServerName);
  Serial.print(" - ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  
  uint32_t beginWait = millis();
  while (millis() - beginWait < 2000){
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE){
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  //Read packet into the buffer
      unsigned long secsSince1900;
      //Convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      ntp_sinc = 0;
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  ntp_sinc++;
  return 0; 
}//Return 0 if unable to get the time

//-----------------------------------------------------------------------------------------------------------------
void sendNTPpacket(IPAddress &address){
  //Set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  //Initialize values needed to form NTP request
  //(see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   //LI, Version, Mode
  packetBuffer[1] = 0;     //Stratum, or type of clock
  packetBuffer[2] = 6;     //Polling Interval
  packetBuffer[3] = 0xEC;  //Peer Clock Precision
  //8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  //All NTP fields have been given values, now
  //You can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//-----------------------------------------------------------------------------------------------------------------
void UpdateTime(){
  Time2Turn_OnOff.seconds2FirstTurn_off -= (minutesReduction * SECS_PER_MIN);
  Time2Turn_OnOff.seconds2SecondTurn_on -= (minutesReduction * SECS_PER_MIN);
  saveEEPROM_timeValues(&Time2Turn_OnOff);
}

//-----------------------------------------------------------------------------------------------------------------
void saveEEPROM_timeValues(Time_OnOff *p){
    setEEPROMtime_t(p->seconds2FirstTurn_off, 0); 
    setEEPROMtime_t(p->seconds2SecondTurn_on, 8);
    EEPROM.commit();
}

//-----------------------------------------------------------------------------------------------------------------
void ReadEEPROM_timeValues(Time_OnOff *p){
  (p->seconds2FirstTurn_off) = getEEPROMtime_t(0);
  (p->seconds2SecondTurn_on) = getEEPROMtime_t(8);
}

//-----------------------------------------------------------------------------------------------------------------
void sendRelayStatus(boolean* RelayStatus){
  client.println("HTTP/1.0 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println("");
  client.println(*RelayStatus);
}

//-----------------------------------------------------------------------------------------------------------------
int command(String http_req){
  String httpReq_firstLine = "";
  int index_r = 0;

  index_r = http_req.indexOf('\r');
  httpReq_firstLine = http_req.substring(0, index_r);
  Serial.println(httpReq_firstLine);

  if(httpReq_firstLine.equals("GET / HTTP/1.1")) return 1;  
  else if(httpReq_firstLine.equals("GET /lightStatus HTTP/1.1")) return 2;
  else if(httpReq_firstLine.equals("GET /ManualPage HTTP/1.1")) return 3;
  else if(httpReq_firstLine.equals("GET /SetLightStatus?Set0 HTTP/1.1")) return 4;
  else if(httpReq_firstLine.equals("GET /SetLightStatus?Set1 HTTP/1.1")) return 5;
  else if(httpReq_firstLine.equals("GET /MainPage HTTP/1.1")) return 6;

  return 0;
}