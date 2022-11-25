#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme; //I2C

/****************************************
 * Definerte konstanter og verdier      *
 ****************************************/
const char* ssid     = "HENRIK-NETT";
const char* password = "84451F&r";
const char* mqtt_server = "broker.hivemq.com";
const char* clientID = "bme280Client";

// -- Definerer debug-modus
bool debug = false;

// -- Konfigurerer MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// -- Verdier som skal sendes over MQTT
float tempToSend;
float humToSend;
float tempVals[4];
float humVals[4];

// -- Definerte verdier for husholdningen:
int tempTooLow = 18;
int tempTooHigh = 28;
int humTooLow = 39; // Ideell luftfuktghet innendørs er mellom 40-60%
int humTooHigh = 61;

// -- DEEPSLEEP
#define uS_TO_S_FACTOR 1000000ULL  // Konverteringsfaktor for mikrosekunder til sekunder
#define TIME_TO_SLEEP  60        // Tiden ESP32 vil være i deepsleep (i sekunder) 

// -- For å lagre verdier på ESP32 selv om den er i sleep
RTC_DATA_ATTR int bootCount;

/****************************************
 * TILLEGGSFUNKSJONER                   *
 ****************************************/

// -- Funksjon som fungerer som delay(), men som er mer nøyaktig
void millisDelay(int milliseconds) {
  unsigned long timeNow = millis();
   
  while(millis() < timeNow + milliseconds){ // venter i oppgitt millisekunder
    // vent
  }
}

// -- Funksjon som initialiserer BME280-sensoren
void initBme() {
  if(!bme.begin(0x76)) { // definerer hvilken adresse I2C er tilkoblet
    if(debug == true) {
      Serial.println("Finner ikke BME280-sensoren, sjekk at alt er riktig koblet! Kontroller i2c addresse.");
    }
    int retries;
    while(!bme.begin(0x76)) { //loop
      retries++;
      if(retries == 30) {
        if(debug == true) {
          Serial.println("Går tilbake til DEEPSLEEP");
          Serial.flush();
        } 
        esp_deep_sleep_start(); //gir opp etter 15 sekunder og går tilbake til sleep 
      }
      millisDelay(500);
    }
  }
}

// -- Funksjon som kobler til internett
void connectWiFi() {
  WiFi.begin(ssid, password);
  
  //Venter på tilkobling til WiFi
  int retries = 0;
  while(WiFi.status() != WL_CONNECTED) {
    retries++;
    if(debug == true) {
      Serial.print(".");
    }
    if(retries == 60){ //gir opp etter 30 sekunder og går tilbake til sleep
      if(debug == true) {
        Serial.println("Går tilbake til DEEPSLEEP");
        Serial.flush(); 
      }
      esp_deep_sleep_start(); 
    }
    millisDelay(500);
  }

  if(debug == true) {
    Serial.println("");
    Serial.println("WiFi tilkoblet.");
    Serial.print("IP-adresse: ");
    Serial.println(WiFi.localIP());
  }
}

//egendefinert funksjon som kobler til MQTT
void reconnectMQTT() {
  //kjører om igjen til tilkoblingen er etablert
  int retries = 0;
  while(!client.connected()) {
    retries++;
    if(debug == true) {
      Serial.println("Prøver å etablere kobling til MQTT...");
    }
    //Prøver å koble til med et definert klient-navn
    if(client.connect(clientID)) {
      if(debug == true) {
        Serial.println("Tilkobling vellykket!"); 
      }
    } else {
      if(debug == true) {
        Serial.print("feilet, rc=");
        Serial.print(client.state()); //printer verdi som forteller hvorfor tilkoblingen mislykkes, for debugging
        Serial.println(" prøv på nytt om 5 sekunder");
      }
        //vent 5 sekunder
      millisDelay(5000);
    }
    if(retries == 6) { //gir opp etter 30 sekunder
      if(debug == true) {
        Serial.println("Går tilbake til DEEPSLEEP");
        Serial.flush(); 
      }
      esp_deep_sleep_start(); // går i sleep
    }
  }
}

// -- Funksjon som regner ut gjennomsnitt av verdier i array
float average(float *array, int len){
  float sumVal = 0L;  // variabler som holder verdiene
  for (int i = 0 ; i <= len ; i++) // For-løkke som legger til alle verdier i array
    sumVal += array[i];
  return  ((float) sumVal) / len;  // gjennomsnitt består ofte av tall med desimaler, så verdien blir satt til float
}

// -- Funksjon som lagrer temperaturverdiene som leses av
float saveTempValues() {
  for (int i = 0; i <= 4 ; i++) {
    tempVals[i] = bme.readTemperature(); // lagrer av temp verdier fra sensor i en array
    millisDelay(100); // venter 100ms mellom hver måling
  }
  tempToSend = average(tempVals, 5); // gjennomsnittet av målingene blir lagret i en egen variabel som skal sendes.
  
  return tempToSend;
}

// -- Funksjon som lagrer målingene for luftfuktighet
float saveHumValues() {
  for (int i = 0; i <= 4 ; i++) {
    humVals[i] = bme.readHumidity(); // leses av og lagrer verdiene fra sensor
    millisDelay(100); // venter 100ms mellom hver måling
  }
  humToSend = average(humVals, 5); // regner gjennomsnittet av array med målinger
  
  return humToSend; //Returnerer verdi som skal sendes over MQTT
}

// -- Funksjon som sender verdiene over MQTT
void sendMQTTmessage(float tempVal, float humVal, int statVal) {
  // Kobler til internett
  connectWiFi();

  //setter verdier for tilkobling til MQTT
  client.setServer(mqtt_server, 1883);
  
  if (!client.connected()) { //looper hvis MQTT ikke er tilkoblet
    reconnectMQTT();
  }
  
  char tempString[10]; //char som inneholder data
  dtostrf(tempVal, 1, 2, tempString); //gjør om fra float til string, og beholder 2 desimaler
  if(debug == true) {
    Serial.print("Temperatur: ");
    Serial.println(tempString);
  }
  client.publish("hivemq/temperature", tempString); //sender over MQTT til Node-RED på topic "hivemq/temperature"
  
  char humidityString[10]; //char som inneholder data
  dtostrf(humVal, 1, 2, humidityString); //gjør om til string
  if(debug == true) {
    Serial.print("Luftfuktighet: "); //Serialprinter
    Serial.println(humidityString);
  }
  client.publish("hivemq/humidity", humidityString); //Sender på gitt topic

  if(statVal < 5) {
    char statString[10]; //char som inneholder data
    dtostrf(statVal, 1, 2, statString); //gjør om til string
    if(debug == true) {
      Serial.print("Luftfuktighet: "); //Serialprinter
      Serial.println(statString);
    }
    client.publish("hivemq/statVal", statString); //Sender på gitt topic
  }
}

/****************************************
 * Hovedfuksjoner                       *
 ****************************************/

void setup() {
  ++bootCount;
  if(debug == true) {
    Serial.begin(115200);
    millisDelay(500); //tar tid å åpne serial, for debugformål
    Serial.println("Wake up nr: " + String(bootCount));
  }
  
  //Når timer er brukt er kun RTC kontroller, RTC peripherals og RTC memory aktiv
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR); //setter ESP til å sove i et gitt tidsrom
  if(debug == true) {
    Serial.println("Setter ESP til å sove i " + String(TIME_TO_SLEEP) + " sekunder");
  }
  
  // Initialiserer BME-sensor
  initBme();

  // Gjennomfører målinger og lagrer verdiene
  saveTempValues();
  saveHumValues();

  if(tempToSend >= tempTooHigh) {
    int statVal = 0;
    sendMQTTmessage(tempToSend, humToSend, statVal); //sender data over MQTT
    client.disconnect(); //Frakobler MQTT serveren på en sikker måte
    millisDelay(500); //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
    esp_deep_sleep_start();
  } else if(tempToSend <= tempTooLow) {
    int statVal = 1;
    sendMQTTmessage(tempToSend, humToSend, statVal); //sender data over MQTT
    client.disconnect(); //Frakobler MQTT serveren på en sikker måte
    millisDelay(500); //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
    esp_deep_sleep_start();
  } else if(humToSend >= humTooHigh) {
    int statVal = 2;
    sendMQTTmessage(tempToSend, humToSend, statVal); //sender data over MQTT
    client.disconnect(); //Frakobler MQTT serveren på en sikker måte
    millisDelay(500); //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
    esp_deep_sleep_start();
  } else if(humToSend <= humTooLow) {
    int statVal = 3;
    sendMQTTmessage(tempToSend, humToSend, statVal); //sender data over MQTT
    client.disconnect(); //Frakobler MQTT serveren på en sikker måte
    millisDelay(500); //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
    esp_deep_sleep_start();
  } else if(bootCount >= 5) {   //Tar målinger hvert minutt, men sender kun hvert 5. minutt
    sendMQTTmessage(tempToSend, humToSend, 10); //sender data over MQTT
    bootCount = 0; //Setter bootCount til 0
    client.disconnect(); //Frakobler MQTT serveren på en sikker måte
    millisDelay(500); //Delay slik at data skal rekke å bli sendt før ESP32 går i sleep
    esp_deep_sleep_start();
  } else {
    esp_deep_sleep_start();
  }
}

void loop(){
  //ingen kode kan kjøre her grunnet DEEPSLEEP
}
