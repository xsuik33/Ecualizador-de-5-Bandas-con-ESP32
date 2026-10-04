#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------------------------------------------
// CONFIGURACIÓN DE HARDWARE Y PINES
// ---------------------------------------------------------
const int potPins[5] = {32, 33, 34, 35, 36}; // Pines seguros del ADC1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------------------------------------------------
// VARIABLES DE DSP Y SUAVIZADO (Filtro Exponencial)
// ---------------------------------------------------------
float smoothedGains[5] = {1.0, 1.0, 1.0, 1.0, 1.0};
const float alpha = 0.1; // Factor de suavizado (menor = más suave pero más lento)

unsigned long lastDSPUpdate = 0;
unsigned long lastOledUpdate = 0;
const int dspRefreshRate = 10;   // Leer potenciómetros cada 10ms
const int oledRefreshRate = 50;  // Refrescar OLED cada 50ms (20 FPS)

// ---------------------------------------------------------
// CONFIGURACIÓN DE RED
// ---------------------------------------------------------
const char* ssid = "ESP32-EQ-5Bandas";
WebServer server(80);

// ---------------------------------------------------------
// INTERFAZ WEB EMBEBIDA (HTML, CSS, JS)
// ---------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <title>Simulador EQ 5 Bandas</title>
  <style>
    body { font-family: 'Segoe UI', sans-serif; background: #121212; color: #fff; text-align: center; margin: 0; padding: 20px; }
    h1 { color: #00FF88; font-weight: 300; }
    .container { max-width: 800px; margin: 0 auto; background: #1e1e1e; padding: 20px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
    .chart-container { margin-bottom: 40px; position: relative; width: 100%; height: 250px; background: #2a2a2a; border-radius: 10px; border: 1px solid #444; }
    canvas { width: 100%; height: 100%; display: block; }
    .eq-bars { display: flex; justify-content: space-around; align-items: flex-end; height: 200px; margin-top: 20px; padding: 20px 0; background: #222; border-radius: 10px; }
    .bar-group { display: flex; flex-direction: column; align-items: center; width: 15%; }
    .bar-wrapper { height: 150px; width: 30px; background: #333; border-radius: 5px; position: relative; overflow: hidden; margin-bottom: 10px; }
    .bar-fill { position: absolute; bottom: 0; width: 100%; background: linear-gradient(0deg, #00FF88, #0088FF); transition: height 0.1s ease-out; }
    .label { font-size: 0.85em; color: #aaa; }
    .gain-value { font-size: 1.1em; font-weight: bold; margin-bottom: 5px; color: #00FF88; }
  </style>
</head>
<body>
  <h1>DSP EQ - 5 Bandas</h1>
  <div class="container">
    <div class="chart-container"><canvas id="eqCurve"></canvas></div>
    <div class="eq-bars">
      <div class="bar-group"><div class="gain-value" id="val0">1.0</div><div class="bar-wrapper"><div class="bar-fill" id="bar0" style="height: 50%;"></div></div><div class="label">60 Hz</div></div>
      <div class="bar-group"><div class="gain-value" id="val1">1.0</div><div class="bar-wrapper"><div class="bar-fill" id="bar1" style="height: 50%;"></div></div><div class="label">250 Hz</div></div>
      <div class="bar-group"><div class="gain-value" id="val2">1.0</div><div class="bar-wrapper"><div class="bar-fill" id="bar2" style="height: 50%;"></div></div><div class="label">1 kHz</div></div>
      <div class="bar-group"><div class="gain-value" id="val3">1.0</div><div class="bar-wrapper"><div class="bar-fill" id="bar3" style="height: 50%;"></div></div><div class="label">4 kHz</div></div>
      <div class="bar-group"><div class="gain-value" id="val4">1.0</div><div class="bar-wrapper"><div class="bar-fill" id="bar4" style="height: 50%;"></div></div><div class="label">12 kHz</div></div>
    </div>
  </div>

  <script>
    const canvas = document.getElementById('eqCurve');
    const ctx = canvas.getContext('2d');
    function resizeCanvas() { canvas.width = canvas.clientWidth; canvas.height = canvas.clientHeight; }
    window.addEventListener('resize', resizeCanvas); resizeCanvas();

    function drawCurve(gains) {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.2)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, canvas.height / 2); ctx.lineTo(canvas.width, canvas.height / 2); ctx.stroke();
      const points = []; const margin = 40; const spacing = (canvas.width - (margin * 2)) / 4;
      for(let i=0; i<5; i++) { points.push({x: margin + (i * spacing), y: canvas.height - (gains[i] / 2.0 * canvas.height)}); }
      ctx.beginPath(); ctx.moveTo(0, points[0].y);
      for (let i = 0; i < points.length - 1; i++) { ctx.quadraticCurveTo(points[i].x, points[i].y, (points[i].x + points[i+1].x)/2, (points[i].y + points[i+1].y)/2); }
      ctx.quadraticCurveTo(points[points.length-1].x, points[points.length-1].y, canvas.width, points[points.length-1].y);
      ctx.strokeStyle = '#00FF88'; ctx.lineWidth = 4; ctx.stroke();
      ctx.lineTo(canvas.width, canvas.height); ctx.lineTo(0, canvas.height); ctx.fillStyle = 'rgba(0, 255, 136, 0.1)'; ctx.fill();
    }

    // Consultar telemetría cada 100ms
    setInterval(() => {
      fetch('/datos').then(r => r.json()).then(data => {
          let gains = [data.b0, data.b1, data.b2, data.b3, data.b4];
          drawCurve(gains);
          for(let i=0; i<5; i++) {
            document.getElementById('val'+i).innerText = gains[i].toFixed(2);
            document.getElementById('bar'+i).style.height = ((gains[i] / 2.0) * 100) + '%';
          }
      }).catch(err => console.log("Desconectado del ESP32"));
    }, 100);
  </script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------
// INICIALIZACIÓN (SETUP)
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Iniciar OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED no detectada. Revisa cableado I2C."));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 25);
    display.println("Iniciando EQ...");
    display.display();
  }

  // Iniciar Red AP
  WiFi.softAP(ssid);
  
  // Endpoint de la interfaz HTML
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", index_html);
  });

  // Endpoint de la API JSON
  server.on("/datos", HTTP_GET, []() {
    String json = "{";
    for(int i=0; i<5; i++) {
      // Enviamos el valor ya filtrado por software al navegador
      json += "\"b" + String(i) + "\":" + String(smoothedGains[i], 2);
      if(i < 4) json += ",";
    }
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
}

// ---------------------------------------------------------
// BUCLE PRINCIPAL (LOOP)
// ---------------------------------------------------------
void loop() {
  // 1. Escuchar peticiones Web
  server.handleClient();

  // 2. Leer potenciómetros y aplicar filtro exponencial (Cada 10ms)
  if (millis() - lastDSPUpdate > dspRefreshRate) {
    lastDSPUpdate = millis();
    
    for(int i = 0; i < 5; i++) {
      int raw = analogRead(potPins[i]);
      float targetGain = (raw / 4095.0) * 2.0; // Convertir a ganancia 0.0 - 2.0
      
      // Ecuación del filtro exponencial para eliminar ruido
      smoothedGains[i] = (alpha * targetGain) + ((1.0 - alpha) * smoothedGains[i]);
    }
  }

  // 3. Actualizar la pantalla OLED (Cada 50ms)
  if (millis() - lastOledUpdate > oledRefreshRate) {
    lastOledUpdate = millis();
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(" DSP EQ 5-BANDAS");

    // Dibujar las 5 barras en la OLED usando los valores filtrados
    for(int i = 0; i < 5; i++) {
      // Mapear de ganancia (0.0-2.0) a pixeles de altura (0-50)
      int barHeight = (smoothedGains[i] / 2.0) * 50; 
      
      int xPos = 8 + (i * 24); 
      int yPos = 64 - barHeight;
      int width = 14;          
      
      display.drawRect(xPos, 14, width, 50, SSD1306_WHITE); // Contorno
      display.fillRect(xPos, yPos, width, barHeight, SSD1306_WHITE); // Relleno
    }
    
    display.display();
  }
}