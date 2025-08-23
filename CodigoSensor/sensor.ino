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
#define DHTPIN1 19
#define DHTPIN2 22
#define DHTTYPE DHT22
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
float tempDHT1, humiDHT1;
float tempDHT2, humiDHT2;

// Botão Push ==============
#define BUTTON_PIN 5
bool buttonPressed = false;

// WiFi ====================
const char* ssid = "ESP32_AP";
const char* password = "password_1234";

// Web Server ==============
AsyncWebServer server(80);
AsyncEventSource events("/events");  // Cria o manipulador de eventos SSE

// Variáveis auxiliares
int sampleCount = 0; // Variável para contar as amostras
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

    // Salvar no CSV e enviar via SSE (apenas se todos os dados forem válidos)
    saveDataToCSV(tempsDS18B20, tempDHT1, humiDHT1, tempDHT2, humiDHT2);

    // Envia os dados via SSE para a interface HTML
    String jsonData = "{";
    jsonData += "\"sensor1\":" + String(tempsDS18B20[0]) + ",";
    jsonData += "\"sensor2\":" + String(tempsDS18B20[1]) + ",";
    jsonData += "\"sensor3\":" + String(tempsDS18B20[2]) + ",";
    jsonData += "\"sensor4\":" + String(tempsDS18B20[3]) + ",";
    jsonData += "\"dht1_temp\":" + String(tempDHT1) + ",";
    jsonData += "\"dht1_hum\":" + String(humiDHT1) + ",";
    jsonData += "\"dht2_temp\":" + String(tempDHT2) + ",";
    jsonData += "\"dht2_hum\":" + String(humiDHT2);
    jsonData += "}";

    events.send(jsonData.c_str(), "update"); // Evento "update" com os dados
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

const char* htmlPage = R"=====(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Monitoramento de Sensores em Tempo Real</title>
  <style>
    :root {
      --primary-bg: linear-gradient(180deg, #1f2e31 0%, #0b566e 25%, #1a8095 100%);
      --card-bg: rgba(255, 255, 255, 0.05);
      --text-color: #ffffff;
      --active-color: rgba(0, 255, 0, 0.2);
      --inactive-color: rgba(255, 0, 0, 0.2);
    }
    
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    
    body {
      font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
      background: var(--primary-bg);
      color: var(--text-color);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 2rem;
    }
    
    h2 {
      text-align: center;
      font-size: clamp(2rem, 5vw, 3rem);
      margin-bottom: 2rem;
      text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.4);
    }
    
    .sensor-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
      gap: 1.5rem;
      width: 100%;
      max-width: 1200px;
    }
    
    .sensor-card {
      background: var(--card-bg);
      border-radius: 1rem;
      padding: 1.5rem;
      box-shadow: 0 6px 12px rgba(0, 0, 0, 0.3);
      text-align: center;
      transition: transform 0.3s ease;
    }
    
    .sensor-card:hover {
      transform: translateY(-5px);
    }
    
    .sensor-label {
      font-size: 1.3rem;
      font-weight: 500;
      margin-bottom: 0.5rem;
    }
    
    .sensor-value {
      font-size: 2.5rem;
      font-weight: bold;
    }
    
    .unit {
      font-size: 1.5rem;
      opacity: 0.8;
    }
    
    .status {
      margin-top: 2rem;
      padding: 0.75rem 1.5rem;
      border-radius: 0.5rem;
      font-weight: bold;
      text-align: center;
    }
    
    .active {
      background-color: var(--active-color);
      color: #00ff00;
    }
    
    .inactive {
      background-color: var(--inactive-color);
      color: #ff0000;
    }
    
    @media (max-width: 768px) {
      body {
        padding: 1rem;
      }
      
      .sensor-grid {
        grid-template-columns: 1fr 1fr;
      }
    }
    
    @media (max-width: 480px) {
      .sensor-grid {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <h2>Monitoramento de Sensores</h2>
  
  <div class="sensor-grid">
    <!-- Sensores DS18B20 -->
    <div class="sensor-card">
      <div class="sensor-label">DS18B20 #1</div>
      <div class="sensor-value" id="sensor1">--</div>
      <span class="unit">°C</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DS18B20 #2</div>
      <div class="sensor-value" id="sensor2">--</div>
      <span class="unit">°C</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DS18B20 #3</div>
      <div class="sensor-value" id="sensor3">--</div>
      <span class="unit">°C</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DS18B20 #4</div>
      <div class="sensor-value" id="sensor4">--</div>
      <span class="unit">°C</span>
    </div>
    
    <!-- Sensores DHT -->
    <div class="sensor-card">
      <div class="sensor-label">DHT1 Temperatura</div>
      <div class="sensor-value" id="dht1_temp">--</div>
      <span class="unit">°C</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DHT1 Umidade</div>
      <div class="sensor-value" id="dht1_hum">--</div>
      <span class="unit">%</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DHT2 Temperatura</div>
      <div class="sensor-value" id="dht2_temp">--</div>
      <span class="unit">°C</span>
    </div>
    
    <div class="sensor-card">
      <div class="sensor-label">DHT2 Umidade</div>
      <div class="sensor-value" id="dht2_hum">--</div>
      <span class="unit">%</span>
    </div>
  </div>
  
  <div class="status" id="status">Status: Aguardando leitura...</div>

  <script>
    document.addEventListener('DOMContentLoaded', () => {
      const statusElement = document.getElementById('status');
      const eventSource = new EventSource('/events');
      
      // Formatador de valores numéricos
      const formatValue = (value) => {
        return value !== null && !isNaN(value) ? value.toFixed(2) : '--';
      };
      
      // Manipulador de eventos SSE
      eventSource.addEventListener('update', (event) => {
        try {
          const data = JSON.parse(event.data);
          
          // Atualiza todos os sensores
          document.getElementById('sensor1').textContent = formatValue(data.sensor1);
          document.getElementById('sensor2').textContent = formatValue(data.sensor2);
          document.getElementById('sensor3').textContent = formatValue(data.sensor3);
          document.getElementById('sensor4').textContent = formatValue(data.sensor4);
          document.getElementById('dht1_temp').textContent = formatValue(data.dht1_temp);
          document.getElementById('dht1_hum').textContent = formatValue(data.dht1_hum);
          document.getElementById('dht2_temp').textContent = formatValue(data.dht2_temp);
          document.getElementById('dht2_hum').textContent = formatValue(data.dht2_hum);
          
          // Atualiza status
          statusElement.textContent = `Status: Dados atualizados em ${new Date().toLocaleTimeString()}`;
          statusElement.className = 'status active';
          
          // Reseta o status após 3 segundos
          setTimeout(() => {
            statusElement.className = 'status';
          }, 3000);
          
        } catch (error) {
          console.error('Erro ao processar dados:', error);
          statusElement.textContent = 'Status: Erro ao processar dados';
          statusElement.className = 'status inactive';
        }
      });
      
      // Tratamento de erros
      eventSource.onerror = () => {
        statusElement.textContent = 'Status: Conexão perdida - reconectando...';
        statusElement.className = 'status inactive';
      };
    });
  </script>
</body>
</html>
)=====";

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

  // Rota para mostrar a interface
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"sensor1\":" + String(tempsDS18B20[0]) + ",";
    json += "\"sensor2\":" + String(tempsDS18B20[1]) + ",";
    json += "\"sensor3\":" + String(tempsDS18B20[2]) + ",";
    json += "\"sensor4\":" + String(tempsDS18B20[3]) + ",";
    json += "\"dht1_temp\":" + String(tempDHT1) + ",";
    json += "\"dht1_hum\":" + String(humiDHT1) + ",";
    json += "\"dht2_temp\":" + String(tempDHT2) + ",";
    json += "\"dht2_hum\":" + String(humiDHT2);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.addHandler(&events);

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

  setupWiFiAndServer();

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
