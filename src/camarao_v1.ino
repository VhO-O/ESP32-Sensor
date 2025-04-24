#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "DHT.h"
#include "SPIFFS.h"
#include <ESPAsyncWebServer.h>

// DS18B20 =================
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddresses[] = {
  {0x28, 0x61, 0x64, 0x34, 0xD4, 0x1D, 0x99, 0xCD},
  {0x28, 0x61, 0x64, 0x34, 0xD5, 0x74, 0x94, 0x73},
  {0x28, 0x61, 0x64, 0x34, 0xD5, 0x4C, 0xA5, 0xC8},
  {0x28, 0x61, 0x64, 0x34, 0xD5, 0x79, 0x2F, 0xE8}
};
float tempsDS18B20[4];

// DHT22 ===================
#define DHTPIN1 5
#define DHTPIN2 22
#define DHTTYPE DHT22
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
float tempDHT1, humiDHT1;
float tempDHT2, humiDHT2;

// Botão Push ==============
#define BUTTON_PIN 19
bool buttonPressed = false;

// WiFi ====================
const char* ssid = "ESP32_AP";
const char* password = "password_1234";

// Web Server ==============
AsyncWebServer server(80);

// Variável para contar as amostras
int sampleCount = 0;
const int maxSamples = 1000;  // Limita o número de amostras no arquivo CSV
bool doRead = true; // Configura o estado para leitura

// Variáveis para controle de tempo de coleta das amostras
unsigned long lastReadingTime = 0; // Armazena o tempo da última leitura
const long readingInterval = 5000; // Intervalo de 5 segundos

// Função para salvar dados em CSV
void saveDataToCSV(float dsTemps[], float dht1Temp, float dht1Hum, float dht2Temp, float dht2Hum) {
  if (sampleCount >= maxSamples) {
    SPIFFS.remove("/data.csv");
    sampleCount = 0;
    Serial.println("CSV file reset after 1000 samples");
  }

  File file = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }

  // Adicionar cabeçalhos se for a primeira amostra
  if (sampleCount == 0) {
    file.println("Sample,DS18B20_1,DS18B20_2,DS18B20_3,DS18B20_4,DHT1_Temp,DHT1_Humidity,DHT2_Temp,DHT2_Humidity");
  }

  sampleCount++;
  file.printf("%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", sampleCount,
               dsTemps[0], dsTemps[1], dsTemps[2], dsTemps[3],
               dht1Temp, dht1Hum, dht2Temp, dht2Hum);
  file.close();

  // Log no Serial com 3 casas decimais
  Serial.printf("Data saved to CSV: %d, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f\n",
                sampleCount, dsTemps[0], dsTemps[1], dsTemps[2], dsTemps[3],
                dht1Temp, dht1Hum, dht2Temp, dht2Hum);
}

// Função para leitura dos sensores (ajuste para 3 casas decimais no log)
void sensorReading() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastReadingTime >= readingInterval) {
    lastReadingTime = currentMillis;

    // Leitura dos DS18B20
    sensors.requestTemperatures();
    for (int i = 0; i < 4; i++) {
      tempsDS18B20[i] = sensors.getTempC(sensorAddresses[i]);
    }

    // Leitura dos DHT22
    tempDHT1 = dht1.readTemperature();
    humiDHT1 = dht1.readHumidity();
    tempDHT2 = dht2.readTemperature();
    humiDHT2 = dht2.readHumidity();

    if (isnan(tempDHT1) || isnan(humiDHT1) || isnan(tempDHT2) || isnan(humiDHT2)) {
      Serial.println(F("Failed to read from one or both DHT sensors!"));
      return;
    }

    // Log dos dados com 3 casas decimais
    Serial.printf("DS18B20 Temps: %.3f, %.3f, %.3f, %.3f\n", tempsDS18B20[0], tempsDS18B20[1], tempsDS18B20[2], tempsDS18B20[3]);
    Serial.printf("DHT1: Temp: %.3f °C, Humidity: %.3f %%\n", tempDHT1, humiDHT1);
    Serial.printf("DHT2: Temp: %.3f °C, Humidity: %.3f %%\n", tempDHT2, humiDHT2);

    // Salvar no CSV
    saveDataToCSV(tempsDS18B20, tempDHT1, humiDHT1, tempDHT2, humiDHT2);
  }
}

// Função para cuidar com o botão push (com debounce)
void IRAM_ATTR handleButtonPress() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress > 200) {  // Debounce de 200ms
    buttonPressed = true;
    lastPress = millis();
  }
}

String removeCSVFile(String filename){
  if (SPIFFS.exists(filename)) { // Verifica se o arquivo existe
    if (SPIFFS.remove(filename)) { // Romove o arquivo passado
      Serial.println("File removed");
      sampleCount = 0;
      return "File removed";
    } else {
      Serial.println("Failed to remove file");
      return "Failed to remove file";
    }
  } else {
    Serial.println("File not found");
    return "File not found";
  }   
}

// Função para configurar o WiFi e o servidor
void setupWiFiAndServer() {
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  if (IP) {
    Serial.print("AP IP address: ");
    Serial.println(IP);
  } else {
    Serial.println("Failed to start WiFi AP");
    return;
  }

  // Rota para download do arquivo CSV
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(SPIFFS.exists("/data.csv")) {
      request->send(SPIFFS, "/data.csv", "text/csv");
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });
  
  // Rota para apagar do arquivo CSV
  server.on("/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", removeCSVFile("/data.csv").c_str());
  });

  // Rota para parar as coletas
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    doRead = false;
    Serial.println("Parando a leitura dos sensores...");
    request->send(200, "text/plain", "Data collection stopped");
  });

  // Rota para iniciar as coletas
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest *request) {
    doRead = true;
    Serial.println("Retomando a leitura dos sensores...");
    request->send(200, "text/plain", "Data collection started successfully");
  });

  server.begin();
  Serial.println("Webserver started");
}

void setup() {
  Serial.begin(115200);
  sensors.begin();
  dht1.begin();
  dht2.begin();

  // Initializa SPIFFS (com formatação automática em caso de falha)
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }

  // Configuração do botão push
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, handleButtonPress, FALLING);

  // Configuração do LED
  pinMode(2, OUTPUT);
}

void loop() {
  if (doRead) {
    sensorReading();
  }

  if (buttonPressed) {
    buttonPressed = false;
    
    if (digitalRead(2) == HIGH) {
      Serial.println("Button pressed! Turn off WiFi and Web Server.");
      digitalWrite(2, LOW);
      doRead = true;
      WiFi.softAPdisconnect(true); // Desliga o WiFi
      server.end(); // Desliga o web server
    } else {
      Serial.println("Button pr/essed! CSV file available for download.");
      digitalWrite(2, HIGH);
      doRead = false;
      setupWiFiAndServer(); // Liga o WiFi e inicia o web server
    }
  }
}
