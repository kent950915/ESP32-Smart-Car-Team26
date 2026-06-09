
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h> 

//  新增：ESP32 BLE 藍牙專用函式庫
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ==========================================
//  儀表板 LCD 設定區 (I2C)
// ==========================================
#define SDA_PIN 8
#define SCL_PIN 9
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// 腳位定義 
const int ENA = 15; const int IN1 = 2;  const int IN2 = 4;
const int IN3 = 16; const int IN4 = 17; const int ENB = 5;

const int S1 = 12; const int S2 = 13; const int S3 = 14; 
const int S4 = 18; const int S5 = 19;

// ==========================================
//  幻彩燈條設定區
// ==========================================
#define LED_PIN     21   
#define LED_COUNT   15   

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t COLOR_GREEN = strip.Color(0, 255, 0);   
uint32_t COLOR_RED   = strip.Color(255, 0, 0);   
uint32_t COLOR_BLUE  = strip.Color(0, 0, 255);   

// ==========================================
//  賽車調校區
// ==========================================
int BASE_SPEED = 255;    
int SLIGHT_SPEED = 130;  
int REVERSE_SPEED = 180; 
 
//  修正：開機預設為 false，等待手機 VLS 觸發
bool isRunning = false; 

int lastTurnState = 0; 
int currentMotorState = -1; 
int lastDisplayedState = -1; 

unsigned long lastLcdUpdateTime = 0;
const int LCD_REFRESH_RATE = 200; 

// ==========================================
// 藍牙 BLE 設定區 (VLS & EBS)
// ==========================================
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART 服務
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // 接收特徵

// 處理手機傳來訊息的副程式
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        char cmd = rxValue[0];
        
        // 收到 'G' 或 'g' -> 觸發 VLS 起跑
        if (cmd == 'G' || cmd == 'g') {
          isRunning = true;
          Serial.println("VLS Triggered: GO!");
        } 
        // 收到 'S' 或 's' -> 觸發 EBS 緊急制動
        else if (cmd == 'S' || cmd == 's') {
          isRunning = false;
          Serial.println("EBS Triggered: STOP!");
        }
      }
    }
};

void setupBLE() {
  BLEDevice::init("qwertyuiop"); // 手機上會看到的藍牙名稱
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID_RX,
                       BLECharacteristic::PROPERTY_WRITE
                     );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
}

// ==========================================
// 變換車燈與動畫
// ==========================================
void setCarLight(uint32_t color) {
  for(int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void playBootAnimation() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); 
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(">>> TEAM  26 <<<");
  lcd.setCursor(0, 1); lcd.print("System Booting..");

  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(0, 150, 255)); 
    strip.show(); delay(80); 
  }
  lcd.setCursor(0, 1); lcd.print("[==============]");
  setCarLight(strip.Color(200, 150, 0)); delay(600);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  >> SYSTEM <<  ");
  lcd.setCursor(0, 1); lcd.print(" BLE VLS READY! "); // 提示藍牙已就緒
  setCarLight(COLOR_GREEN); delay(1000);
}

void setup() {
  Serial.begin(115200);
  strip.begin();           
  strip.show();            
  strip.setBrightness(20); 
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); 
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  digitalWrite(ENA, HIGH); digitalWrite(ENB, HIGH);
  
  pinMode(S1, INPUT); pinMode(S2, INPUT); 
  pinMode(S3, INPUT); pinMode(S4, INPUT); pinMode(S5, INPUT);

  // 啟動藍牙伺服器
  setupBLE();

  playBootAnimation();
  stopCar(); // 確保開機靜止
}

// ==========================================
// 儀表板獨立刷新系統 (不卡大腦)
// ==========================================
void updateLCD() {
  if (currentMotorState == lastDisplayedState) return; 
  
  if (currentMotorState == 1) {
    lcd.setCursor(0, 0); lcd.print(" Mode: AUTO NAV ");
    lcd.setCursor(0, 1); lcd.print(" Action: FORWARD");
  } else if (currentMotorState == 2) {
    lcd.setCursor(0, 0); lcd.print(" Mode: TRACKING ");
    lcd.setCursor(0, 1); lcd.print(" Action: L-SLIGHT");
  } else if (currentMotorState == 3) {
    lcd.setCursor(0, 0); lcd.print(" Mode: TRACKING ");
    lcd.setCursor(0, 1); lcd.print(" Action: R-SLIGHT");
  } else if (currentMotorState == 4) {
    lcd.setCursor(0, 0); lcd.print(" Mode: RECOVERY ");
    lcd.setCursor(0, 1); lcd.print(" Action: L-SHARP ");
  } else if (currentMotorState == 5) {
    lcd.setCursor(0, 0); lcd.print(" Mode: RECOVERY ");
    lcd.setCursor(0, 1); lcd.print(" Action: R-SHARP ");
  } else if (currentMotorState == 0) {
    lcd.setCursor(0, 0); lcd.print(" Mode: STANDBY  ");
    lcd.setCursor(0, 1); lcd.print(" Action: STOP   ");
  }
  lastDisplayedState = currentMotorState;
}

// ==========================================
//  多段式馬達動作 (0 延遲)
// ==========================================
void forward() { 
  analogWrite(IN1, BASE_SPEED); analogWrite(IN2, 0); 
  analogWrite(IN3, BASE_SPEED); analogWrite(IN4, 0); 
  if (currentMotorState != 1) { setCarLight(COLOR_RED); currentMotorState = 1; }
}

void turnLeftSlight() { 
  analogWrite(IN1, SLIGHT_SPEED); analogWrite(IN2, 0); 
  analogWrite(IN3, BASE_SPEED);   analogWrite(IN4, 0); 
  if (currentMotorState != 2) { setCarLight(COLOR_BLUE); currentMotorState = 2; }
}

void turnRightSlight() { 
  analogWrite(IN1, BASE_SPEED);   analogWrite(IN2, 0); 
  analogWrite(IN3, SLIGHT_SPEED); analogWrite(IN4, 0); 
  if (currentMotorState != 3) { setCarLight(COLOR_BLUE); currentMotorState = 3; }
}

void turnLeftSharp() { 
  analogWrite(IN1, 0);          analogWrite(IN2, REVERSE_SPEED); 
  analogWrite(IN3, BASE_SPEED); analogWrite(IN4, 0); 
  if (currentMotorState != 4) { setCarLight(COLOR_BLUE); currentMotorState = 4; }
}

void turnRightSharp() { 
  analogWrite(IN1, BASE_SPEED); analogWrite(IN2, 0); 
  analogWrite(IN3, 0);          analogWrite(IN4, REVERSE_SPEED); 
  if (currentMotorState != 5) { setCarLight(COLOR_BLUE); currentMotorState = 5; }
}

void stopCar() { 
  analogWrite(IN1, 0); analogWrite(IN2, 0); 
  analogWrite(IN3, 0); analogWrite(IN4, 0); 
  if (currentMotorState != 0) { setCarLight(COLOR_GREEN); currentMotorState = 0; }
}

// ==========================================
// 主迴圈：大腦核心邏輯
// ==========================================
void loop() {
  if (isRunning) {
    // 1. 極速感測與馬達反應 (完全無延遲)
    int val1 = digitalRead(S1);
    int val2 = digitalRead(S2);
    int val3 = digitalRead(S3);
    int val4 = digitalRead(S4);
    int val5 = digitalRead(S5);

    if (val1 == 0 && val5 == 0)      { forward(); lastTurnState = 0; }
    else if (val1 == 0 && val5 == 1) { turnLeftSharp(); lastTurnState = -1; } 
    else if (val5 == 0 && val1 == 1) { turnRightSharp(); lastTurnState = 1; } 
    else if (val2 == 0)              { turnLeftSlight(); lastTurnState = -1; } 
    else if (val4 == 0)              { turnRightSlight(); lastTurnState = 1; } 
    else if (val3 == 0)              { forward(); lastTurnState = 0; } 
    else {
      if (lastTurnState == -1)      { turnLeftSharp(); } 
      else if (lastTurnState == 1)  { turnRightSharp(); } 
      else                          { stopCar(); }
    }
  } else {
    // 若收到 EBS 指令或開機未啟動，鎖死馬達並亮綠燈
    stopCar();
  }

  // 2. 螢幕非同步更新 (每 200 毫秒只做一次)
  if (millis() - lastLcdUpdateTime >= LCD_REFRESH_RATE) {
    updateLCD();
    lastLcdUpdateTime = millis();
  }
}