#include <WiFi.h>
#include <PubSubClient.h>

/****************************************
 * Definer konstanter                   *
 ****************************************/
//const char* ssid = "HENRIK-NETT"; const char* password = "84451F&r"; //Henrik nett
const char* ssid = "ASUS"; const char* password = "huleboer"; //Christoffer nett
//const char* ssid = "Christoffer's iPhone"; const char* password = "keiserKuzco";
const char* mqtt_server = "broker.hivemq.com";
const char* clientID = "WaterManagementClient";
const char* compareTopic = "hivemq/water_level";
RTC_DATA_ATTR int bootCount = 0; //Holder styr på antall boots
bool Ack = false; //Kvittering for evt retransmissjon
unsigned long now;
unsigned long refTime;
long duration; //Tiden lydbølgene bruker på å reflektere og returnere av nærmeste gjenstand
float waterlevel; //Avstand fra utralydsensor til vannoverflaten
char i;
const int waterlevelThreshold = 15; //Minste vannivå for når det er akseptabelt å kjøre en vanningssyklus (i %)
const int bottomDist = 27;

/****************************************
 * Verdier som skal sendes over MQTT    *
 ****************************************/
RTC_DATA_ATTR float avgWaterlevel; //Gjennomsnittet av målingene innenfor et tidsintervall

/****************************************
 * Pins                                 *
 ****************************************/
const int trigger = 15;
const int echo = 2;
#define RXp2 16 //RX2 port for UART kommunikasjon
#define TXp2 17 //TX2 port for UART kommunikasjon

//konfigurerer MQTT
WiFiClient espClient;
PubSubClient client(espClient);

/****************************************
 * TILLEGGSFUNKSJONER                   *
 ****************************************/

// Funksjon som kobler til internett
void connectWiFi() {
  Serial.print("Attempting WiFi connection");
  WiFi.begin(ssid, password);
  
  //Venter på tilkobling til WiFi
  int retries = 0;
  while(WiFi.status() != WL_CONNECTED) {
    retries++;
    Serial.print(".");
    if(retries == 60){ 
      Serial.println("Returning to DEEPSLEEP");
      Serial.flush(); 
      esp_deep_sleep_start(); //gir opp etter 30 sekunder og går tilbake til sleep
    }
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connection established");
  Serial.print("IP-adress: ");
  Serial.println(WiFi.localIP());
}

//egendefinert funksjon som kobler til MQTT
void reconnectMQTT() {
  //kjører om igjen til tilkoblingen er etablert
  int retries = 0;
  while(!client.connected()) {
    retries++;
    Serial.println("Establishing MQTT connection...");
    //Prøver å koble til med et definert klient-navn
    if(client.connect(clientID)) {
      Serial.println("MQTT connection established"); 
      client.subscribe("hivemq/water_level"); //Subscriber til topic
      client.subscribe("hivemq/moisttest"); //Fuktighetsmålinger fra fuktighetssensor 1
    } 
    else {
      Serial.print("Failed, rc=");
      Serial.print(client.state()); //printer verdi som forteller hvorfor tilkoblingen mislykkes, for debugging
      Serial.println(" trying again in 5 secons");
      //vent 5 sekunder
      delay(5000);
    }
    if(retries == 6) { //gir opp etter 30 sekunder
      Serial.println("Returning to DEEPSLEEP");
      Serial.flush(); 
      esp_deep_sleep_start(); //gir opp etter 30 sekunder
    }
  }
}//reconnect MQTT slutt

//Callback funksjon som håndterer mottakelse av meldinger over MQTT
void callback(char* topic, byte* message, unsigned int length) {
  // Når ESPen mottar en string via en topic
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  Ack = true;

/****************************************
* Vanning av planter                    *
****************************************/
  if(messageTemp == "101"){ //Få ekte verdi som skal publiseres fra Erik!
    
    //Måler vannstanden
    watelevelMeasurement();

    if(avgWaterlevel < waterlevelThreshold){ //For lavt nivå i tanken
      Serial.println("Water level too low!");
      sendMQTTmessage(avgWaterlevel);
    }
    else if(avgWaterlevel >= waterlevelThreshold){ //Høyt nok nivå i tanken
      Serial.println("Acceptable water level. Starting pump...");
      Serial2.println("1"); //Slår på pumpa
      delay(3000); //Pumpa står på i 5 sek
      Serial2.println("0"); //Slår av pumpa
      delay(2000); //Venter på at vannet i tenken skal roe seg

      watelevelMeasurement();

      sendMQTTmessage(avgWaterlevel);
    }
  }
}

//Funksjonen forsikrer seg om at meldingen har kommet trygt frem ved hjelp av kvittering og retransmissjonsklokke (og retransmissjon ved utløpt klokketid)
//int retransWindiw tilsvarer lengden på retransmisjonsklokka i ms
void retransmission(int retransWindow){
  Serial.println("Waiting for ack");
  refTime = millis();

  //Så lenge det ikke har blitt mottatt en kvittering (se callback() for hvordan ack funker)
  while(Ack != true){ 
    now = millis();
    //Hvis retransmisjonsklokka (retransWindow) har utløpt
    if((now - refTime) > retransWindow){
      Serial.print("No ack recieved within "); Serial.print(retransWindow); 
      Serial.println(" ms. Attempting retransmission...");

      //Retransmissjon
      sendMQTTmessage(avgWaterlevel);

      refTime = millis();
    }

    //Holder clienten oppdatert for å kunne detektere innkommende meldinger
    //callback() tar hånd om behandling av inkommende meldinger og definering av ack
    if(!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
  }
  Serial.println("Ack recieved");
  Ack = false;
}

//Funksjon som sender verdiene over MQTT
void sendMQTTmessage(int message) {
  Ack = false;
  if (!client.connected()) { //looper hvis MQTT ikke er tilkoblet
    reconnectMQTT();
  }

  char tempString[10]; //char som inneholder data
  dtostrf(message, 1, 0, tempString); //gjør om fra float til string, og beholder 0 desimaler
  //Serial.print("Current water level: ");
  //Serial.println(tempString);
  client.publish("hivemq/water_level", tempString); //sender over MQTT til Node-RED på topic "hivemq/temperature"
  Serial.println("Payload sent!");
  Serial.println("");

  retransmission(5000);
}

//Tar 10 målinger av vannstanden og returnerer gjennomsnittet
int watelevelMeasurement(){
  float tempSum = 0; //Midlertidig variabel for å regne gjennomsnittet av målingene

  for(int i=0;i<10;i++){
    digitalWrite(trigger, LOW);
    delay(2);

    digitalWrite(trigger, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigger, LOW);

    duration = pulseIn(echo, HIGH); //i mikrosekunder

    waterlevel = duration * 0.0343 / 2; //tid * lydens hastighet i luft / 2 (for å kun finne avstanden til refleksjonsflaten)
    Serial.println(waterlevel);
    tempSum += waterlevel;
    
    //Enda et steg for å finne vannstand. Denne kan kun settes opp etter at oppsettet er på plass, og sensorens høyde over bunnen er kjent
  }  
  avgWaterlevel = ((27 - tempSum/10)/bottomDist)*100; //26cm mellom sensor og bunn av vanntank
  if(avgWaterlevel < 0){
    avgWaterlevel = 0;
  }
  Serial.print("Measured water level: "); Serial.println(avgWaterlevel);
  return avgWaterlevel;
}

/****************************************
 * Hovedfuksjoner                       *
 ****************************************/

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial2.begin(9600, SERIAL_8N1, RXp2, TXp2); //UART kommunikasjon til arduino Uno
  delay(1000); //tar tid å åpne serial, for debugformål
  pinMode(echo, INPUT);
  pinMode(trigger, OUTPUT);

  //Kobler til internett
  connectWiFi();
  
  //setter verdier for tilkobling til MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  reconnectMQTT();

  Serial.println("Ready to recieve watering request");
}

// Holder ESP-en tilkoplet MQTT-brokeren, og dermed mottakelig for transmisjoner
void loop(){
  if(!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
}