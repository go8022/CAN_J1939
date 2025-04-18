/* Gemini 2.0Flash 2025-04-18 trials.
3. 설정 및 주의 사항
CAN Shield 설정: SPI_CS_PIN을 실제 CAN shield의 CS 핀 번호에 맞게 수정해야 합니다. CAN 통신 속도(CAN_500KBPS)도 차량의 CAN 네트워크 설정에 맞춰야 합니다.
LCD 설정: lcd(0x27, 16, 2)에서 I2C 주소(0x27)와 LCD 크기(16x2)를 실제 LCD 모듈에 맞게 수정해야 합니다.
SD 카드 설정: SD_CS_PIN을 실제 SD 카드 모듈의 CS 핀 번호에 맞게 수정해야 합니다.
WiFi 설정: ssid와 password를 실제 WiFi 네트워크 정보로 변경해야 합니다.
이메일 설정: smtpServer, smtpPort, emailSender, passwordSender, emailRecipient를 실제 이메일 계정 정보로 변경해야 합니다. 특히 Gmail의 경우 "앱 비밀번호"를 생성하여 사용하는 것이 보안상 좋습니다.
J1939 데이터 파싱: generateFileName() 함수 내의 getJ1939StartTime() 함수와 readAndLogCANData() 함수 내의 PGN 추출 로직은 실제 J1939 데이터 포맷을 분석하여 직접 구현해야 합니다. 어떤 PGN에서 시동 시간 정보를 얻을 수 있는지, 그리고 해당 PGN 내에서 시간 정보가 어떤 바이트로 구성되어 있는지 확인해야 합니다.
시간 동기화: 정확한 파일 이름과 로그 시간을 위해 NTP 서버 등을 이용하여 ESP32의 시간을 동기화하는 것을 고려해 볼 수 있습니다.
에러 처리: SD 카드 초기화 실패, CAN 초기화 실패, WiFi 연결 실패, 이메일 전송 실패 등 다양한 오류 상황에 대한 적절한 처리 로직을 추가하는 것이 좋습니다.
전원 관리: 장시간 로깅을 위해 안정적인 전원 공급 방안을 고려해야 합니다.
4. 추가적인 구현 아이디어
LCD 버튼 활용: LCD 버튼 shield를 이용하여 로깅 시작/정지, 파일 저장 주기 변경 등의 기능을 추가할 수 있습니다.
실시간 LCD 표시: 현재 로깅 상태, 저장된 파일 개수, WiFi 연결 상태 등을 LCD에 실시간으로 표시하면 사용자 편의성을 높일 수 있습니다.
로깅 필터링 강화: 단순 PGN 필터링 외에 특정 SPN 값에 따른 필터링 기능을 추가할 수 있습니다.
이 코드는 기본적인 구조를 제공하며, 실제 동작을 위해서는 사용하시는 하드웨어와 J1939 데이터 규격에 맞춰 코드를 수정하고 보완해야 합니다. 특히 J1939 데이터 처리 부분은 많은 노력이 필요할 수 있습니다.
*/

#include <SPI.h>
// CAN Shield 라이브러리 (예: mcp_can.h)
#include <mcp_can.h>
// LCD 라이브러리 (예: LiquidCrystal_I2C.h)
#include <LiquidCrystal_I2C.h>
// SD 카드 라이브러리
#include <SD.h>
// ESP32 WiFi 라이브러리
#include <WiFi.h>
// ESP32 이메일 라이브러리 (예: ESP32_Mail_Client.h)
#include <ESP32_Mail_Client.h>

// CAN 설정
const int SPI_CS_PIN = 10; // CAN Shield CS 핀
MCP_CAN CAN(SPI_CS_PIN);

// LCD 설정
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD I2C 주소 및 크기

// SD 카드 설정
const int SD_CS_PIN = 5; // SD Card CS 핀
File logFile;
String currentFileName;
bool isLogging = false;
unsigned long lastLogTime = 0;
const unsigned long logInterval = 200; // 200ms

// 저장 간격 설정 (30분)
const unsigned long saveInterval = 30 * 60 * 1000;
unsigned long lastSaveTime = 0;

// 필터링할 PGN 목록
const unsigned long targetPGNs[] = {0x0CF00300, 0x18FF335A, 0x18FF345A, 0x18FF355A, 0x18FF3B03, 0x18FEF100, 0x18F0010B, 0x0CF00203, 0x0CF00503, 0x18FE4A03, 0x0C010305};
const int numTargetPGNs = sizeof(targetPGNs) / sizeof(targetPGNs[0]);

// WiFi 설정
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// 이메일 설정
SMTPSession smtp;
const char* smtpServer = "smtp.gmail.com";
const int smtpPort = 465; // 또는 587 (TLS)
const char* emailSender = "YOUR_GMAIL_ADDRESS";
const char* passwordSender = "YOUR_GMAIL_PASSWORD";
const char* emailRecipient = "go8022@gmail.com";

void setup() {
  Serial.begin(115200);
  Serial.println("CAN Data Logger 시작...");

  // LCD 초기화
  lcd.init();
  lcd.backlight();
  lcd.print("CAN Logger");

  // SD 카드 초기화
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD 카드 초기화 실패!");
    lcd.clear();
    lcd.print("SD Card Error");
    while (1);
  }
  Serial.println("SD 카드 초기화 성공.");

  // CAN 초기화
  if (CAN.begin(CAN_500KBPS) == CAN_OK) {
    Serial.println("CAN 초기화 성공.");
    lcd.clear();
    lcd.print("CAN Ready");
  } else {
    Serial.println("CAN 초기화 실패!");
    lcd.clear();
    lcd.print("CAN Error");
    while (1);
  }

  // WiFi 연결 시도
  Serial.println("WiFi 연결 시도...");
  lcd.clear();
  lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi 연결 성공!");
  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);

  // 파일 이름 생성 (시동 시간 기반)
  currentFileName = generateFileName();
  openNewLogFile();
  isLogging = true;
  lastSaveTime = millis();
  lcd.clear();
  lcd.print("Logging Start");
}

void loop() {
  if (isLogging) {
    if (millis() - lastLogTime >= logInterval) {
      lastLogTime = millis();
      readAndLogCANData();
    }

    if (millis() - lastSaveTime >= saveInterval) {
      lastSaveTime = millis();
      closeLogFile();
      if (WiFi.status() == WL_CONNECTED) {
        sendEmail(currentFileName);
      }
      currentFileName = generateFileName();
      openNewLogFile();
    }
  }

  // LCD 버튼 처리 (필요한 경우)
  // ...
}

String generateFileName() {
  String fileName = "CAN_";
  // J1939 데이터에서 날짜와 시간 정보를 추출하여 파일 이름에 추가 (구현 필요)
  // 예시: fileName += getJ1939StartTime();
  // 임시로 현재 시간 사용
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
  fileName += String(buffer);
  fileName += ".asc";
  return fileName;
}

void openNewLogFile() {
  logFile = SD.open(currentFileName, FILE_WRITE);
  if (logFile) {
    Serial.print("파일 열기: ");
    Serial.println(currentFileName);
    // ASC 파일 헤더 작성
    logFile.println("date " + getFormattedDateTime());
    logFile.println("base hex  timestamps absolute");
    logFile.println("internal events logged");
    logFile.println("// version 8.0.0");
    logFile.print("Begin Triggerblock ");
    logFile.println(getFormattedDateTime());
    logFile.println("    0.000000 Start of measurement");
  } else {
    Serial.print("파일 열기 실패: ");
    Serial.println(currentFileName);
    lcd.clear();
    lcd.print("SD File Error");
    isLogging = false;
  }
}

void closeLogFile() {
  if (logFile) {
    logFile.print("End Triggerblock ");
    logFile.println(getFormattedDateTime());
    logFile.close();
    Serial.println("파일 저장 완료.");
  }
}

void readAndLogCANData() {
  unsigned long rxId;
  unsigned char len = 0;
  unsigned char rxBuf[8];

  if (CAN.readMsgBuf(&rxId, &len, rxBuf) == CAN_OK) {
    // PGN 추출 (J1939에 따라 다름)
    unsigned long pgn = (rxId & 0x1FFFF00) >> 8;

    // 필터링된 PGN인지 확인
    for (int i = 0; i < numTargetPGNs; i++) {
      if (pgn == targetPGNs[i]) {
        logCANMessage(rxId, len, rxBuf, false); // Rx 메시지
        break;
      }
    }
  }
  // 필요하다면 Tx 메시지 로깅 로직 추가
}

void logCANMessage(unsigned long id, unsigned char len, unsigned char* buf, bool isTx) {
  if (logFile) {
    unsigned long currentTimeMillis = millis();
    logFile.printf("    %.6f %d  %08lX %s  d %d", (float)currentTimeMillis / 1000.0, 1, id, isTx ? "Tx" : "Rx", len);
    for (int i = 0; i < len; i++) {
      logFile.printf(" %02X", buf[i]);
    }
    logFile.println();
  }
}

String getFormattedDateTime() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %p %Y", &timeinfo);
  return String(buffer);
}

void sendEmail(String filename) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("이메일 전송 시작...");
    lcd.clear();
    lcd.print("Sending Email");

    // 이메일 세션 설정
    smtp.setLogin(smtpServer, smtpPort, emailSender, passwordSender);
    smtp.setSender(emailSender, "CAN Data Logger");
    smtp.setPriority("High");
    smtp.setSubject("CAN Data Log - " + filename);
    smtp.setMessage("첨부된 파일은 CAN 데이터 로그입니다.", true); // HTML 형식 지원

    // 첨부 파일 추가
    String filePath = "/" + filename;
    if (SD.exists(filePath)) {
      smtp.addFile(SD.open(filePath, FILE_READ), filename);
    } else {
      Serial.println("첨부 파일 찾을 수 없음!");
      lcd.clear();
      lcd.print("File Not Found");
      delay(2000);
      return;
    }

    // 수신자 추가
    smtp.addRecipient(emailRecipient);

    // 이메일 전송
    if (!smtp.send()) {
      Serial.println("이메일 전송 실패");
      Serial.println("Error: " + smtp.errorReason());
      lcd.clear();
      lcd.print("Email Fail");
      lcd.setCursor(0, 1);
      lcd.print(smtp.errorReason().substring(0, 15));
      delay(3000);
    } else {
      Serial.println("이메일 전송 성공!");
      lcd.clear();
      lcd.print("Email Sent!");
      delay(2000);
    }
  } else {
    Serial.println("WiFi가 연결되지 않아 이메일을 보낼 수 없습니다.");
    lcd.clear();
    lcd.print("No WiFi");
    delay(2000);
  }
}

// J1939 데이터에서 시동 시간 정보를 추출하는 함수 (구현 필요)
// String getJ1939StartTime() {
//   // CAN 버스에서 특정 PGN 또는 SPN을 통해 시동 시간 정보 획득 및 파싱
//   // ...
//   return "20250418_093000"; // 임시 값
// }
