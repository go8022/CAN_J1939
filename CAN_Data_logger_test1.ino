/* Arduino Data logger by Perplexity
아래 ASC 포맷으로 CAN J1939 Data logger를 "아두이노"와 "CAN shield"를 이용하여 코딩을 하려고해. 파일의 확장자를 asc로 만들어주세요. 차량의 데이터는 하루 종일 주행을 기준으로 약 8시간 이상의 주행 기준이며, 매 200ms 마다 주요 CAN log를 저장하려고 한다. 저장하려는 CAN message의 PGN은 0cf00300, 18ff335a, 18ff345a, 18ff355a, 18ff3b03, 18fef100, 18f0010b, 0cf00203, 0cf00503, 18fe4a03, 0c010305 이다. 
이 포맷에 맞게 아두이노 코딩을 해주고, 데이터의 저장 시간은 매 30분 마다 저장을 하고, 파일의 이름은 차량의 시동이 걸리면 이때의 날짜와 시간으로 표기하면 될 것 같음. 날짜와 시간 정보는 J1939 기본 CAN data를 참고하면 됨. 그리고, 몇 기가의 SD 카드가 필요한지도 추천바람. 

date Tue Dec 17 15:44:44 PM 2024
base hex  timestamps absolute
internal events logged
// version 8.0.0
Begin Triggerblock Tue Dec 17 15:44:44 PM 2024
   0.000000 Start of measurement
   0.000000 1  18FEF100x             Rx   d 8 C3 00 00 40 00 00 00 C0 
*/

#include <SPI.h>
#include <mcp_can.h>
#include <SD.h>

// 핀 설정
#define CAN_CS_PIN 10
#define SD_CS_PIN 4

MCP_CAN CAN(CAN_CS_PIN); // MCP2515 초기화
File logFile;

// PGN 리스트
const unsigned long PGN_LIST[] = {0x0CF00300, 0x18FF335A, 0x18FF345A, 0x18FF355A, 
                                  0x18FF3B03, 0x18FEF100, 0x18F0010B, 
                                  0x0CF00203, 0x0CF00503, 0x18FE4A03, 
                                  0x0C010305, 0x18FECA03};
const int PGN_COUNT = sizeof(PGN_LIST) / sizeof(PGN_LIST[0]);

unsigned long previousMillis = 0;
const unsigned long interval = 200; // 메시지 저장 간격

unsigned long startTimeMillis = millis();
const unsigned long fileInterval = 30 * 60 * 1000; // 파일 저장 주기 (30분)

void setup() {
    Serial.begin(115200);

    // MCP2515 초기화
    if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
        Serial.println("CAN 초기화 실패");
        while (1);
    }
    Serial.println("CAN 초기화 성공");

    // SD 카드 초기화
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD 카드 초기화 실패");
        while (1);
    }
    Serial.println("SD 카드 초기화 성공");

    createNewLogFile();
}

void loop() {
    unsigned long currentMillis = millis();

    // 메시지 읽기 및 저장
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        unsigned long id;
        byte len;
        byte buf[8];

        if (CAN.checkReceive() == CAN_MSGAVAIL) {
            CAN.readMsgBuf(&id, &len, buf);

            // PGN 필터링
            for (int i = 0; i < PGN_COUNT; i++) {
                if ((id & 0x1FFFF000) == PGN_LIST[i]) { // PGN 매칭
                    logMessage(id, len, buf);
                    break;
                }
            }
        }
    }

    // 파일 저장 주기 확인
    if (currentMillis - startTimeMillis >= fileInterval) {
        startTimeMillis = currentMillis;
        createNewLogFile();
    }
}

void createNewLogFile() {
    char filename[20];
    snprintf(filename, sizeof(filename), "log_%lu.asc", millis());
    
    logFile = SD.open(filename, FILE_WRITE);
    if (logFile) {
        Serial.print("새로운 로그 파일 생성: ");
        Serial.println(filename);
        
        logFile.println("date Thu Apr 10 16:07:00 PM");
        logFile.println("base hex timestamps absolute");
        logFile.println("internal events logged");
        logFile.println("// version 8.2.0");
        logFile.println("Begin Triggerblock Thu Apr 10");
        logFile.close();
    } else {
        Serial.println("로그 파일 생성 실패!");
    }
}

void logMessage(unsigned long id, byte len, byte *buf) {
    logFile = SD.open("log.asc", FILE_WRITE);
    if (logFile) {
        unsigned long timestamp = millis();
        
        logFile.print(timestamp / 1000.0); // 상대 시간 기록
        logFile.print(" ");
        logFile.print(1); // 채널 번호
        logFile.print(" ");
        logFile.print(id & 0x1FFFFFFF, HEX); // 메시지 ID
        logFile.print(" Rx d ");
        
        logFile.print(len); // 데이터 길이
        for (int i = 0; i < len; i++) {
            logFile.print(" ");
            logFile.print(buf[i], HEX); // 데이터 바이트 기록
        }
        
        logFile.println();
        logFile.close();
    } else {
        Serial.println("로그 파일 열기 실패!");
    }
}
