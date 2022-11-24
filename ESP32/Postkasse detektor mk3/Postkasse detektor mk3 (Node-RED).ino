#include <WiFi.h>
#include <PubSubClient.h>

/****************************************
 * Definer konstanter                   *
 ****************************************/
//const char* ssid = "HENRIK-NETT"; const char* password = "84451F&r"; //Henrik nett
const char* ssid = "ASUS"; const char* password = "huleboer"; //Christoffer nett
//const char* ssid = "Christoffer's iPhone"; const char* password = "keiserKuzco";
const char* mqtt_server = "broker.hivemq.com";
const char* clientID = "MailboxClient";
RTC_DATA_ATTR int bootCount = 0; //Holder styr på antall boots
bool mailIndicator = false;
bool Ack = false; //Kvittering for evt retransmissjon
unsigned long now;
unsigned long refTime;

/****************************************
 * Verdier som skal sendes over MQTT    *
 ****************************************/
RTC_DATA_ATTR int mailboxState = 0; //Variabel som skal sendes

/****************************************
 * Pins                                 *
 ****************************************/
//const int redLed = 15;
//const int awakeLed = 5;
const int lidButton = 4;

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
      client.subscribe("hivemq/retransmission"); //Subscriber til topic
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

  if((messageTemp == "1")||(messageTemp == "0")){
    Ack = true;
  }
}

//Funksjonen forsikrer seg om at meldingen har kommet trygt frem ved hjelp av kvittering og retransmissjonsklokke (og retransmissjon ved utløpt klokketid)
//int retransWindiw tilsvarer lengden på retransmisjonsklokka i ms
void retransmission(int retransWindow){
  Serial.println("Waiting for ack");
  refTime = millis();

  //Så lenge det ikke har blitt mottatt en kvittering
  while(Ack != true){ 
    now = millis();
    //Hvis retransmisjonsklokka (retransWindow) har utløpt
    if((now - refTime) > retransWindow){
      Serial.print("No ack recieved within "); Serial.print(retransWindow); 
      Serial.println(" ms. Attempting retransmission...");

      //Retransmissjon
      sendMQTTmessage();

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
}//legg til retransmisjonsklokke,

//Funksjon som sender verdiene over MQTT
void sendMQTTmessage() {
  if (!client.connected()) { //looper hvis MQTT ikke er tilkoblet
    reconnectMQTT();
  }

  char tempString[10]; //char som inneholder data
  dtostrf(mailboxState, 1, 0, tempString); //gjør om fra float til string, og beholder 2 desimaler
  Serial.print("Mailbox status: ");
  Serial.println(tempString);
  client.publish("hivemq/mailbox_status", tempString); //sender over MQTT til Node-RED på topic "hivemq/temperature"
  Serial.println("Payload sent!");
}

//Funksjon som printer til seriell moitor hva som trigget ESP-ens oppvåkning
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

//Hva som skal gjøres når ESP-en blir vekket. Koden er lagret i ESP-ens interne RAM (mye raskere lasting)
//Venter på at lokket skal lukkes og oppdaterer variabel som skal bli sendt
void IRAM_ATTR wakeup(){ 
  //Mens lokket er åpent
  while(digitalRead(lidButton) != HIGH){
    Serial.println("Wating for lid to close");

    while(digitalRead(lidButton) != HIGH){
    }
    delay(500); //Buffer for å forhindre falske avlesninger hvis lokket spretter på kontaktflaten
    
    //Etter at lokket er lukket
    if(mailboxState == 0 && digitalRead(lidButton) == HIGH){
      //digitalWrite(redLed,HIGH); //Tenner mail-indikator
      mailboxState = 1; //Oppdaterer mailbox state
      Serial.println("You've got mail!");
    }
    else if(mailboxState == 1 && digitalRead(lidButton) == HIGH){
      //digitalWrite(redLed,LOW);
      mailboxState = 0; //Oppdaterer mailbox state
      Serial.println("Mailbox emptied");
    }
    else if(digitalRead(lidButton) == LOW){
      Serial.println("No mail recieved");
    }
    delay(500); Serial.println();
  }
}

/****************************************
 * Hovedfuksjoner                       *
 ****************************************/

void setup() {
  Serial.begin(115200);
  delay(1000); //tar tid å åpne serial, for debugformål
  //pinMode(redLed,OUTPUT);
  //pinMode(awakeLed,OUTPUT);
  //digitalWrite(awakeLed,HIGH);

  bootCount++;
  Serial.println("Boot number: " + String(bootCount));

  print_wakeup_reason();

  //ESP-en våkner når pin 4 leser LOW, altså når lokket på postkassa åpnes
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4,0);

  //Gjør målinger og oppdaterer verdier som skal sendes
  wakeup(); 

  //Kobler til internett
  connectWiFi();
  
  //setter verdier for tilkobling til MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  //sender melding over MQTT
  sendMQTTmessage();

  //Venter på kvittering før ESP-en kan gå videre 
  retransmission(5000);

  //Frakobler MQTT serveren på en sikker måte
  client.disconnect();
  
  //Går tilbake til sleep
  Serial.println("Going to sleep now"); Serial.println("");
  esp_deep_sleep_start();
}

void loop(){
  Serial.println("WTF, something is sereously wrong if this appears in your serial monitor...");
  //ingen kode kan kjøre her grunnet DEEPSLEEP
}
