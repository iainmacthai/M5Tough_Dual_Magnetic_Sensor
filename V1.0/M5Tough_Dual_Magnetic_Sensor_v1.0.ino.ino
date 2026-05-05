/**
 * Project: M5Tough_Dual_Magnetic_Sensor_v1.0.ino
 * Description: Comparative magnetic sensing tool using MMC5603 and QMC5883P.
 * Features: Dual-sensor reading, M5Tough touch interface, variable pitch audio, 
 *           static-scale graphing, and Serial Plotter integration.
 */

#include <M5Unified.h>        // Core library for M5Tough display, speaker, and touch
#include <Adafruit_MMC56x3.h> // Library for the High-Resolution MMC5603
#include <Adafruit_QMC5883P.h> // Library for the QMC5883P

// --- Object Declarations ---
Adafruit_MMC5603 mmc = Adafruit_MMC5603(12345);
Adafruit_QMC5883P qmc = Adafruit_QMC5883P();
M5Canvas canvas(&M5.Display); // Off-screen buffer to prevent display flicker

// --- State Management & Constants ---
enum ViewMode { NUMBERS, COMPARE };
ViewMode currentMode = NUMBERS; // Tracks which screen is active

int volume = 64;                // Speaker volume (0-255)
float maxX = 0, maxY = 0, maxZ = 0; 

const int graphHeight = 120;    // Pixel height of the graph area
const int graphOffset = 50;    // Vertical Y-position where graph starts
const int SATURATION_LIMIT = 3000; // Fixed scale +/- 3000 uT (30 Gauss)

int mmc_values[320];            // Buffer for MMC graph points
int qmc_values[320];            // Buffer for QMC graph points

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);                // Initialize M5Stack hardware
  
  Serial.begin(115200);         // Fast baud rate for Serial Plotter
  M5.Speaker.setVolume(volume);

  // --- Sensor Initialization ---
  // MMC5603 uses the standard Adafruit Unified Sensor begin
  bool mmc_status = mmc.begin(MMC56X3_DEFAULT_ADDRESS, &Wire);
  
  // QMC5883P requires the I2C address and the Wire pointer
  bool qmc_status = qmc.begin(QMC5883P_DEFAULT_ADDR, &Wire);

  // Critical Error Check: Stop if sensors are not found
  if (!mmc_status || !qmc_status) {
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setCursor(10, 80);
    M5.Display.printf("MMC: %s  QMC: %s", mmc_status ? "OK":"FAIL", qmc_status ? "OK":"FAIL");
    while (1) delay(10);
  }

  // --- QMC Sensor Configuration ---
  qmc.setMode(QMC5883P_MODE_NORMAL);
  qmc.setRange(QMC5883P_RANGE_30G); // Match MMC full-scale range
  qmc.setODR(QMC5883P_ODR_200HZ);   // Set 200Hz Output Data Rate

  // Initialize Graphics Buffer
  canvas.createSprite(320, 240);
  for (int i = 0; i < 320; i++) {
    mmc_values[i] = graphHeight / 2;
    qmc_values[i] = graphHeight / 2;
  }
}

void loop() {
  M5.update(); // Checks for touch events and button presses

  // --- Touch Logic (State Machine & Controls) ---
  if (M5.Touch.getCount() > 0) {
    auto detail = M5.Touch.getDetail();
    if (detail.isPressed()) {
      // Toggle View (Center Top)
      if (detail.x > 90 && detail.x < 230 && detail.y < 50) currentMode = (currentMode == NUMBERS) ? COMPARE : NUMBERS;
      // Volume Controls (Top Corners)
      else if (detail.x < 80 && detail.y < 50) volume = max(0, volume - 10);
      else if (detail.x > 240 && detail.y < 50) volume = min(255, volume + 10);
      // Reset Max Values (Bottom Left)
      else if (detail.x < 80 && detail.y > 190) { maxX = 0; maxY = 0; maxZ = 0; }
      // Hardware Degauss/Reset (Bottom Right - MMC Only)
      else if (detail.x > 240 && detail.y > 190) mmc.reset(); 
      
      M5.Speaker.setVolume(volume);
      delay(150); // Simple debounce
    }
  }

  // --- Data Acquisition ---
  sensors_event_t mmc_e;
  mmc.getEvent(&mmc_e); // MMC uses Unified Sensor Event
  
  float gx, gy, gz;
  float qmc_z_ut = 0;
  if (qmc.isDataReady()) {
    if (qmc.getGaussField(&gx, &gy, &gz)) {
       qmc_z_ut = gz * 100.0; // Convert Gauss to micro-Tesla (uT)
    }
  }

  // --- Serial Plotter Output ---
  // Sending static lines (Zero, Max, Min) pins the Serial Plotter axis scale
  Serial.print("MMC_Z:");      Serial.print(mmc_e.magnetic.z);
  Serial.print(",QMC_Z:");     Serial.print(qmc_z_ut);
  Serial.print(",Zero:0");     
  Serial.print(",Max:3000");   
  Serial.print(",Min:-3000");  
  Serial.println();

  // --- Background Logic Processing ---
  // Track peak absolute field strength on each axis (uses MMC data)
  if (abs(mmc_e.magnetic.x) > abs(maxX)) maxX = mmc_e.magnetic.x;
  if (abs(mmc_e.magnetic.y) > abs(maxY)) maxY = mmc_e.magnetic.y;
  if (abs(mmc_e.magnetic.z) > abs(maxZ)) maxZ = mmc_e.magnetic.z;
  
  // Audio: Pitch based on total Resultant Field (Magnitude)
  float mag = sqrt(sq(mmc_e.magnetic.x) + sq(mmc_e.magnetic.y) + sq(mmc_e.magnetic.z));
  if (mag > 60) {
    int freq = map(constrain((int)mag, 60, SATURATION_LIMIT), 60, SATURATION_LIMIT, 200, 2200);
    M5.Speaker.tone(freq, 40);
  } else {
    M5.Speaker.stop();
  }

  // Update Graph Scrolling Buffers
  for (int i = 0; i < 319; i++) {
    mmc_values[i] = mmc_values[i+1];
    qmc_values[i] = qmc_values[i+1];
  }
  // Map uT values to pixel heights within the fixed graphHeight
  mmc_values[319] = constrain(map((int)mmc_e.magnetic.z, -SATURATION_LIMIT, SATURATION_LIMIT, graphHeight, 0), 0, graphHeight);
  qmc_values[319] = constrain(map((int)qmc_z_ut, -SATURATION_LIMIT, SATURATION_LIMIT, graphHeight, 0), 0, graphHeight);

  // --- UI Rendering (M5Tough Display) ---
  canvas.fillSprite(TFT_BLACK);

  // Draw Persistent Header Buttons
  canvas.fillRect(0, 0, 80, 40, TFT_RED); canvas.drawCenterString("-VOL", 40, 12);
  canvas.fillRect(240, 0, 80, 40, TFT_BLUE); canvas.drawCenterString("+VOL", 280, 12);
  canvas.fillRect(90, 0, 140, 40, TFT_DARKCYAN); 
  canvas.drawCenterString(currentMode == NUMBERS ? "NUMS" : "GRAPH", 160, 12);

  if (currentMode == NUMBERS) {
    // Screen 1: Digital Readout
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_GREEN);
    canvas.setCursor(10, 50);  canvas.printf("MMC Z: %.2f uT", mmc_e.magnetic.z);
    canvas.setTextColor(TFT_YELLOW);
    canvas.setCursor(10, 75);  canvas.printf("QMC Z: %.2f uT", qmc_z_ut);
    
    canvas.drawFastHLine(0, 105, 320, TFT_DARKGREY); // Visual Divider
    
    canvas.setTextColor(TFT_ORANGE);
    canvas.setCursor(10, 120); canvas.printf("MAX X: %.1f", maxX);
    canvas.setCursor(10, 145); canvas.printf("MAX Y: %.1f", maxY);
    canvas.setCursor(10, 170); canvas.printf("MAX Z: %.1f", maxZ);
  } 
  else {
    // Screen 2: Static-Scale Comparison Graph
    canvas.drawRect(0, graphOffset, 320, graphHeight + 2, TFT_DARKGREY);
    canvas.drawFastHLine(0, graphOffset + (graphHeight / 2), 320, TFT_RED); // Zero Line
    
    // Scale Labels (Y-Axis)
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.setCursor(2, graphOffset + 2); canvas.print("+3k");
    canvas.setCursor(2, graphOffset + (graphHeight/2) - 10); canvas.print("0 uT");
    canvas.setCursor(2, graphOffset + graphHeight - 10); canvas.print("-3k");

    // Draw scrolling lines for both sensors
    for (int i = 0; i < 319; i++) {
      canvas.drawLine(i, mmc_values[i] + graphOffset, i + 1, mmc_values[i+1] + graphOffset, TFT_GREEN);
      canvas.drawLine(i, qmc_values[i] + graphOffset, i + 1, qmc_values[i+1] + graphOffset, TFT_YELLOW);
    }
    canvas.setTextColor(TFT_GREEN); canvas.setCursor(10, 180); canvas.print("MMC5603");
    canvas.setTextColor(TFT_YELLOW); canvas.setCursor(180, 180); canvas.print("QMC5883P");
  }

  // Draw Persistent Footer Buttons
  canvas.fillRect(0, 200, 80, 40, TFT_YELLOW); 
  canvas.setTextColor(TFT_BLACK); canvas.drawCenterString("MAX RST", 40, 212);
  canvas.fillRect(240, 200, 80, 40, TFT_PURPLE);
  canvas.setTextColor(TFT_WHITE); canvas.drawCenterString("HW RST", 280, 212);

  canvas.pushSprite(0, 0); // Push off-screen canvas to the physical display
}
