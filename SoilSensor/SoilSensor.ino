#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp32-hal-cpu.h>

#define deep_sleep_interval 3600000000 //Dette tilsvarer 1 times intervall i mikrosekunder
/*
 The whole plan is to make a code that can easily add more sensor as needed. Thinking of a bigger area where some areas are dryer than others. You can idealy use less water from the wateroutlet.
 This is an important step for many countries where watersupply is limited due to climate change or conflict situations.
 */

//Informasjonen esp32 trenger for å koble seg på trådløs nett, og mqtt server. ClientID er unik for denne esp32.
const char* ssid     = "HENRIK-NETT"; //"Gilberg";
const char* password = "84451F&r"; //"Peugeot505";
const char* mqtt_server = "broker.hivemq.com";
const char* clientID = "soilClient";

//konfigurerer MQTT
WiFiClient espClient;
PubSubClient client(espClient);

//Legger til antall analog-innganger
const int moisterPin1 = 32;
const int moisterPin2 = 33;
const int moisterPin3 = 34;

/*
  Her kan vi legge til flere eller færre sensorer til esp-enheten. For å legge til må SensorCount økes med en for hver sensor, sensorAdress og sensorpin
  må også tilføres en ekstra adresse og analog-inngang.
*/
const int SensorCount = 3;
int Sc = int(SensorCount) - 1;
int sensordata[SensorCount];
const char* sensorAdress[SensorCount] = {"hivemq/moist1", "hivemq/moist2", "hivemq/moist3"};
byte sensorpin[SensorCount] = {moisterPin1, moisterPin2, moisterPin3};

// Funksjon som kobler til internett
void connectWiFi() {
  WiFi.begin(ssid, password);
  
  //Venter på tilkobling til WiFi
  int retries = 0;
  while(WiFi.status() != WL_CONNECTED) {
    retries++;
    Serial.print(".");
    if(retries == 60){ 
      Serial.println("Går tilbake til DEEPSLEEP");
      Serial.flush(); 
      esp_deep_sleep_start(); //gir opp etter 30 sekunder og går tilbake til sleep
    }
    millisDelay(500);
  }

  Serial.println("");
  Serial.println("WiFi tilkoblet.");
  Serial.print("IP-adresse: ");
  Serial.println(WiFi.localIP());
}

//egendefinert funksjon som kobler til MQTT
void reconnectMQTT() {
  //kjører om igjen til tilkoblingen er etablert
  int retries = 0;
  while(!client.connected()) {
    retries++;
    Serial.println("Prøver å etablere kobling til MQTT...");
    //Prøver å koble til med et definert klient-navn
    if(client.connect(clientID)) {
      Serial.println("Tilkobling vellykket!"); 
    } else {
      Serial.print("feilet, rc=");
      Serial.print(client.state()); //printer verdi som forteller hvorfor tilkoblingen mislykkes, for debugging
      Serial.println(" prøv på nytt om 5 sekunder");
      //vent 5 sekunder
      millisDelay(5000);
    }
    if(retries == 6) { //gir opp etter 30 sekunder
      Serial.println("Går tilbake til DEEPSLEEP");
      Serial.flush(); 
      esp_deep_sleep_start(); //gir opp etter 30 sekunder
    }
  }
}//reconnect MQTT slutt

/*
Denne funksjonen leser sensorverdier og sender verdiene til server. For-løkken itererer gjennom sensor data.
"sValue" lagrer sensorverdien. "sValueProsent" inverterer verdien slik at høyst måling gir minst prosent, og lavest måling gir høyst. Intervallet hvor verdiene opererer
er bestemt utifra testing i våte og tørre omgivelser. "sensStat" lagrer en verdi for hvilken tilstand sensoren har utifra måleresultatet: "vanning, ok eller ikke tilkoblet".
Første if-setning: hindrer prosenten og få negativ verdi ved lave sensormålinger.
Andre if-setning: hindrer prosenten i å overstige 100 prosent, hvis verdien skulle bli høyere enn avgrensning.
*/
void moisterSens(){
   for(int i=0; i <= Sc; i++){ 
    const int Treshold = 1750; //Setter grense for hvor jorda er fuktig nok og ikke. Verdi funnet ved uttesting av fuktig og tør jord.
    const int HighPass = 10; //Når sensor ikke er tilkoblet vil gpio bli tilkoblet jord. Det er derfor alt under HighPass verdi vil tolkes som enten feil på sensor, eller ikke tilkoblet.
    int sValue = sensordata[i];
    int sValueProsent = map((sValue), 2500, 1200, 0, 100);    //1200 er sensoren i et glass med vann, luft er 2400.

    int sensStat = sensorStatus(sValue, HighPass, Treshold);

    if((sValue < 10) || (sValueProsent != abs(sValueProsent))) {
      sValueProsent = 0;   
    }

    if(sValueProsent > 100) {
      sValueProsent = 100; 
    }
    Serial.println("runde" + String(i));
    mosisterMqtt(i, sensStat, sValueProsent);
    delay(10);
  
  }
  
}

/*
Funksjonen tar inn sensorverdi, og grenseverdier som input. Utifra grenseverdiene avgjøres det hvilken tilstand sensoren befinner seg i.
Vått eller tørt eller frakoblet (eventuelt andre feil). sensorStatus() returnerer variabel sensStat. sensStat sin verdi blir videresendt til nodeRed,
hvor nodeRed gjennom en ganske enkel kode skriver ut en status tekst utifra hvilken verdi den får tilsendt.
*/
int sensorStatus(int sValue, int HighPass, int Treshold){
  int sensStat;
  
  if(sValue >= Treshold) {
    sensStat = 101;
  //  Serial.print("Sensor");
  //  Serial.print(i);
  //  Serial.print(": ");
  //  Serial.println(1);
  }

  else if (sValue >= HighPass) {
    sensStat = 102;
 //   Serial.print("Sensor");
 //   Serial.print(i);
 //   Serial.print(": ");
 //   Serial.println(2);
  }

  else{
    sensStat = 103;
    //Serial.print("Sensor");
   // Serial.print(i);
    //Serial.print(": ");
   // Serial.println(3);
  }
  return sensStat;
};

/*
moisterMqtt tar inn variablene i, sensStat og sValueProsent. i benyttes for å adressere hvilken sensor som har blitt avlest verdi, og ved hjelp av switch case så
videresendes verdiene til riktig adresse på nodeRed serveren. Sjekker først at esp32 har kontakt med mqtt, hvis ikke kjøres reconnect funksjonen. I hver case omdannes 
verdiene til en string ved hjelp av dtostrf(). Denne stringen blir så publisert til nodeRed gjennom den bestemte adressen som samsvarer med mqtt adresse inn på nodeRed.
*/
void mosisterMqtt(int i, int sensStat, int sValueProsent){
  int s = sensStat;
  int v = sValueProsent;
  if (!client.connected()) { //looper hvis MQTT ikke er tilkoblet
    reconnectMQTT();
  }
  
  switch (i) {
    case 1:
      char mValue2[10]; //char som inneholder data
        dtostrf(s, 1, 0, mValue2); //gjør om fra float til string, og beholder ingen desimaler
        client.publish(sensorAdress[i], mValue2);

      char mStat2[10]; //char som inneholder data
        dtostrf(v, 1, 0, mStat2); //gjør om fra float til string, og beholder 2 desimaler
        client.publish(sensorAdress[i], mStat2);

      break;

     case 2:
      char mValue3[10]; //char som inneholder data
        dtostrf(s, 1, 0, mValue3); //gjør om fra float til string, og beholder 2 desimaler
        client.publish(sensorAdress[i], mValue3);

      char mStat3[10]; //char som inneholder data
        dtostrf(v, 1, 0, mStat3); //gjør om fra float til string, og beholder 2 desimaler
        client.publish(sensorAdress[i], mStat3);

       break;

      default:
       char mValue1[10]; //char som inneholder data
        dtostrf(s, 1, 0, mValue1); //gjør om fra float til string, og beholder 2 desimaler
        client.publish(sensorAdress[i], mValue1);

      char mStat1[10]; //char som inneholder data
        dtostrf(v, 1, 0, mStat1); //gjør om fra float til string, og beholder 2 desimaler
        client.publish(sensorAdress[i], mStat1);

        break;
  } 
}

/*
Esp32 går inn i hybernation. Hybernation er den modusen hvor esp32 bruker minst strøm. Bare RTC-timer er aktiv, dette gjør at bare timeren kan vekke esp32. Som er den eneste vi trenger.
De tre øverste linjene deaktiverer oscillator og ULP-coprocessor, og esp32 går inn i hybernation. Aktiverer timer som oppvåkning. Esp32 går i dvale så lenge som timerintervallet er satt.
*/
void deepSleep () {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);  
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_sleep_enable_timer_wakeup(deep_sleep_interval);                   
  esp_deep_sleep_start();
}

//Funksjon som fungerer som delay(), men som er mer nøyaktig
void millisDelay(int milliseconds) {
  unsigned long timeNow = millis();
   
  while(millis() < timeNow + milliseconds){ //venter i oppgitt millisekunder
    // vent
  }
}

//legg inn så fuktsensoren gir en verdi med engang den tilkobles
void moisterRead(){
  int Sc = (int(SensorCount) - 1);
  for(int i = 0; i <= Sc; i++){
    sensordata[i] = analogRead(sensorpin[i]);
    Serial.print(sensordata[i]);
    Serial.println(": er det jeg sender");
  };
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);  //Setter cpu klokkehastigheten til 80MHz som er det laveste klokke hastigheten vi kan bruke wifi funksjonaliteten på.
  pinMode(moisterPin1, INPUT);
  pinMode(moisterPin2, INPUT);
  pinMode(moisterPin3, INPUT);

  /*
  Prøvde å legge denne inn i en funksjon, men det skapte kluss med avlesningene og ble ustabilt. Dermed ble den plassert i setup funksjonen hvor den kjøre problemfritt. 
  For-løkken er henter inn data fra alle sensorene som er lagt inn i systemet, og lagrer verdiene i en array. sensordata benyttes videre i moisterSens().
  */
  for(int i = 0; i <= Sc; i++){
    sensordata[i] = analogRead(sensorpin[i]);
    Serial.print(i);
  };
  
  // Kobler til internett
  connectWiFi();

  //setter verdier for tilkobling til MQTT. Siste verdien er nettverks-port.
  client.setServer(mqtt_server, 1883);

  //Kjører funksjonene
  moisterSens();

  //Frakobler MQTT serveren på en sikker måte
  client.disconnect();

  //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
  millisDelay(500); 

  //Esp32 går i sleep
  deepSleep ();
}

void loop() {

}

 
