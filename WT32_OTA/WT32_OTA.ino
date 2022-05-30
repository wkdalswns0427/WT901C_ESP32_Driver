#include <HardwareSerial.h> //for rs485 comm
#define RXD2 5
#define TXD2 17
HardwareSerial rs485(2); // rxtx mode 2 of 0,1,2

#include "deviceConfig.h"
#include <Arduino.h> //for ethernet
#include <ETH.h>     //for ethernet
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <string.h>
#include <math.h>
StaticJsonDocument<1024> sensor;

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

const char* host = "esp32";
WebServer server(80);

unsigned long long int uS_TO_S_FACTOR = 1000000ULL;
unsigned long long int TIME_TO_SLEEP = 0;
RTC_DATA_ATTR int bootCount = 0;

byte unlockMaster1[]={0x50, 0x06, 0x00, 0x69, 0xB5, 0x88, 0x22, 0xA1};
byte changeADDR1[] = {0x50, 0x06, 0x00, 0x1A, 0x00, 0x51, 0x64, 0x70};
byte accCalmode1[]={0x50, 0x06, 0x00, 0x01, 0x00, 0x01, 0x14, 0x4B};
byte magCalmode1[]={0x50, 0x06, 0x00, 0x01, 0x00, 0x07, 0x94, 0x49};
byte setNormal1[]={0x50, 0x06, 0x00, 0x01, 0x00, 0x00, 0xD5, 0x8B};
byte saveConfig1[]={0x50, 0x06, 0x00, 0x00, 0x00, 0x00, 0x84, 0x4B};
byte readAngle1[] = {0x50, 0x03, 0x00, 0x3d, 0x00, 0x03, 0x99, 0x86};
byte readAcc1[] = {0x50, 0x03, 0x00, 0x34, 0x00, 0x03, 0x49, 0x84};
byte readAngVel1[] = {0x50, 0x03, 0x00, 0x37, 0x00, 0x03, 0xB9, 0x84};

byte unlockMaster2[] = {0x51, 0x06, 0x00, 0x69, 0xB5, 0x88, 0x23, 0x70};
byte saveConfig2[]={0x51, 0x06, 0x00, 0x00, 0x00, 0x00, 0x85, 0x9A};
byte accCalmode2[]={0x51, 0x06, 0x00, 0x01, 0x00, 0x01, 0x15, 0x9A};
byte magCalmode2[]={0x51, 0x06, 0x00, 0x01, 0x00, 0x07, 0x95, 0x98};
byte setNormal2[]={0x51, 0x06, 0x00, 0x01, 0x00, 0x00, 0xD4, 0x5A};
byte readAngle2[] = {0x51, 0x03, 0x00, 0x3D, 0x00, 0x03, 0x98, 0x57};
byte readAcc2[] = {0x51, 0x03, 0x00, 0x34, 0x00, 0x03, 0x48, 0x55};
byte readAngVel2[] = {0x51, 0x03, 0x00, 0x37, 0x00, 0x03, 0xB8, 0x55};

short recData1[12];
short recData2[12];
short trashBuffer[180];

short prevBuffer[3][6];
short newBuffer[3][6];
short diffBuffer[3][6];

/*
 * buffer structure
 **********************************************************************
 *           dev1[x] | dev1[y] | dev1[z] | dev2[x] | dev2[y] | dev2[z] |
 *           -----------------------------------------------------------
 *accel      |       |         |         |         |         |         |
 *           -----------------------------------------------------------
 *angle      |       |         |         |         |         |         |
 *           -----------------------------------------------------------
 *angularvel |       |         |         |         |         |         |
 *           -----------------------------------------------------------
 ***********************************************************************
 */

static float accDiff[6];
static float angDiff[6];
static float angvelDiff[6];

int static1=32768*16*9.81;
int static2=32768*180;
int static3=32768*2000;

int flag = 0;

struct errcnt {
    unsigned short success = 0;
    unsigned short fail = 0;
};
struct errcnt err_api;
struct errcnt err_wt901_1;
struct errcnt err_wt901_2;

const char* loginIndex =
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
             "<td>Username:</td>"
             "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

/*
 * Server Index Page
 */

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";


void reboot()
{
    Serial.println("system reboot");
    ESP.restart();
}

void errcnt_countup(struct errcnt* count, int fail)
{
    if (fail) {
        count->success = 0;
        count->fail++;
        if (count->fail == ERRCNT_FAIL_THRESHOLD)

            reboot();
    } else {
        count->success++;
        if (count->success >= ERRCNT_SUCCESS_THRESHOLD) {
            count->success--;
            count->fail = 0;
        }
    }
}


void WiFiEvent(WiFiEvent_t event)
{

  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}


void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break; //should always be this
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void config_sleep_mode() {
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.print("Setup ESP32 to sleep for every ");
  Serial.print(TIME_TO_SLEEP);
  Serial.println(" Seconds");
  print_wakeup_reason();
}


void sendCommand(byte command[8], int prt){
  byte data[10];
  for(int i=0;i<8;i++){
    data[i]=command[i];
    }
  if(prt==1){
    for(int i = 0;i<8;i++){
      Serial.print(command[i],HEX);
      if(i != 7){
        Serial.print(",");
        }
      }
    }
  rs485.write(data, 8);
  Serial.println();
  rs485.flush();
  }

void calibrateAcc(int type){
    if(type==1){
      Serial.println("---------- Acceleration Calibration Init ----------");
      sendCommand(unlockMaster1,1);
      delay(500);
      sendCommand(accCalmode1,1);
      delay(6000);
      sendCommand(setNormal1,1);
      delay(1000);
      sendCommand(saveConfig1,1);
      delay(1000);
    }
    else if(type==2){
      Serial.println("---------- Acceleration Calibration Init ----------");
      sendCommand(unlockMaster2,1);
      delay(500);
      sendCommand(accCalmode2,1);
      delay(6000);
      sendCommand(setNormal2,1);
      delay(1000);
      sendCommand(saveConfig2,1);
      delay(1000);
    }
  }
  
void calibrateMag(int type){
    if(type==1){
      Serial.println("---------- Magnetic Calibration Init ----------");
      sendCommand(unlockMaster1,1);
      delay(500);
      sendCommand(magCalmode1,1);
      Serial.println("---------- Slowly rotate in 3 axis ----------");
      delay(5000);
      Serial.println("---------- May stop now :) ----------");
      sendCommand(setNormal1,1);
      delay(1000);
      sendCommand(saveConfig1,1);
      delay(1000);  
    }
    else if(type==2){
      Serial.println("---------- Magnetic Calibration Init ----------");
      sendCommand(unlockMaster2,1);
      delay(500);
      sendCommand(magCalmode2,1);
      Serial.println("---------- Slowly rotate in 3 axis ----------");
      delay(5000);
      Serial.println("---------- May stop now :) ----------");
      sendCommand(setNormal2,1);
      delay(1000);
      sendCommand(saveConfig2,1);
      delay(1000);  
      }
  }
  

int rs485_receive(short recv[], int num){
    unsigned long t = millis(); 
    while(1){
      if(millis() - t > 10000){
        return -1;
        break;
      }
      for (int i = 0; (rs485.available() > 0) && (i < num); i++) {
        recv[i] = rs485.read();
      }
      return 0;
      break;
    }
  }
  
void readAcceleration(int type){
  if(type==1){
    sendCommand(readAcc1,0);
    Serial.println("Acceleration");
    
    if(rs485_receive(recData1, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData1[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAccel(recData1);
    rs485.flush();
    delay(10);
    }

  else if(type==2){
    sendCommand(readAcc2,0);
    Serial.println("Acceleration");
    
    if(rs485_receive(recData2, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData2[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAccel(recData2);
    rs485.flush();
    delay(10);
    }
  }

void readSensorAngle(int type){
  if(type==1){
    sendCommand(readAngle1,0);
    Serial.println("Angle");
    
    if(rs485_receive(recData1, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData1[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAngle(recData1);
    rs485.flush();
    delay(10);
    }
    
  else if(type==2){
    sendCommand(readAngle2,0);
    Serial.println("Angle");
    
    if(rs485_receive(recData2, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData2[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAngle(recData2);
    rs485.flush();
    delay(10);
    }
  }

void readAngularVelocity(int type){
  if(type==1){
    sendCommand(readAngVel1,0);
    Serial.println("Angular Velocity");
    
    if(rs485_receive(recData1, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData1[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAngVel(recData1);
    rs485.flush();
    delay(10);
    }

  else if(type==2){
    sendCommand(readAngVel2,0);
    Serial.println("Angular Velocity");
    
    if(rs485_receive(recData2, 11) != -1){
      for(int i = 0;i<11;i++){
        Serial.print(recData2[i],HEX);
        Serial.print(",");
        }
        Serial.println();
      }
     else{
      Serial.println("no resp");
      Serial.println();    
      } 
    printAngVel(recData2);
    rs485.flush();
    delay(10);
    }
  }

// device 1,2 // type 0,1,2
void savebuffer(short tarbuf[3][6], int device, int type){
  if(device==1){
    tarbuf[type][0]=((recData1[3]<<8)|recData1[4]);
    tarbuf[type][1]=((recData1[5]<<8)|recData1[6]);
    tarbuf[type][2]=((recData1[7]<<8)|recData1[8]);
    }
  else if(device==2){
    tarbuf[type][3]=((recData2[3]<<8)|recData2[4]);
    tarbuf[type][4]=((recData2[5]<<8)|recData2[6]);
    tarbuf[type][5]=((recData2[7]<<8)|recData2[8]);
    }
  }

void printAccel(short rec[]){
  short data_x = ((rec[3]<<8)|rec[4])/static1;
  short data_y = ((rec[5]<<8)|rec[6])/static1;
  short data_z = ((rec[7]<<8)|rec[8])/static1;
  Serial.print(data_x);Serial.print("   "); Serial.print(data_y);Serial.print("   "); Serial.println(data_z);
  }

void printAngle(short rec[]){
  short data_x = ((rec[3]<<8)|rec[4])/static2;
  short data_y = ((rec[5]<<8)|rec[6])/static2;
  short data_z = ((rec[7]<<8)|rec[8])/static2;
  Serial.print(data_x);Serial.print("   "); Serial.print(data_y);Serial.print("   "); Serial.println(data_z);
  }

void printAngVel(short rec[]){
  short data_x = ((rec[3]<<8)|rec[4])/static3;
  short data_y = ((rec[5]<<8)|rec[6])/static3;
  short data_z = ((rec[7]<<8)|rec[8])/static3;
  Serial.print(data_x);Serial.print("   "); Serial.print(data_y);Serial.print("   "); Serial.println(data_z);
  }
  
// device 1,2 // type 0,1,2
void calculateDiff(float arr[], int device, int type){
  for(int i=3*(device-1); i<3*(device-1)+3; i++){
    if(type==0){
        arr[i] = (newBuffer[type][i]/static1);
      }
    else if(type==1){
        arr[i] = (newBuffer[type][i]/static2);
      }
    else if(type==2){
        arr[i] = (newBuffer[type][i]/static3);
      }
    diffBuffer[type][i]=arr[i];
    }
  if(device==1){
    Serial.print(arr[0]);Serial.print("   "); Serial.print(arr[1]);Serial.print("   "); Serial.println(arr[2]);
    }
  else{
    Serial.print(arr[3]);Serial.print("   "); Serial.print(arr[4]);Serial.print("   "); Serial.println(arr[5]);
    }
  
  }

float calcAbsVal(int type, int device){
  float val;
  if(device == 1){
    val = sqrt(pow(diffBuffer[type][0],2.0) + pow(diffBuffer[type][1],2.0) + pow(diffBuffer[type][2],2.0));  
    }
  else if(device == 2){
    val = sqrt(pow(diffBuffer[type][3],2.0) + pow(diffBuffer[type][4],2.0) + pow(diffBuffer[type][5],2.0));    
    }
  Serial.println(val);        
  return val;
  }

void readSensor(short buf[3][6]){
  readAcceleration(1);
  savebuffer(buf, 1, 0);
  readAcceleration(2);
  savebuffer(buf, 2, 0);
  
  readSensorAngle(1);
  savebuffer(buf, 1, 1);
  readSensorAngle(2);
  savebuffer(buf, 2, 1);
    
  readAngularVelocity(1);
  savebuffer(buf, 1, 2);
  readAngularVelocity(2);
  savebuffer(buf, 2, 2);
  Serial.println();
}

void calcSensor(){
  calculateDiff(accDiff, 1, 0);
  calculateDiff(accDiff, 2, 0);
  calculateDiff(angDiff, 1, 1);
  calculateDiff(angDiff, 2, 1);
  calculateDiff(angvelDiff, 1, 2);
  calculateDiff(angvelDiff, 2, 2);
}

void clearBuffer(){
  for(int i=0; i<3;i++){
    for(int j=0; j<6;j++){
      prevBuffer[i][j] = newBuffer[i][j];
      newBuffer[i][j] = 0;
      }
  }
}
void sensorPOST(int sen){
  sensor["mac"]= device_mac;
  if(sen==1){
    sensor["type"]="SENSOR1";
    sensor["data"][0]["sensortype"] = SENSOR_ACC1_X;
    sensor["data"][0]["value"] = diffBuffer[0][0];
    sensor["data"][1]["sensortype"] = SENSOR_ACC1_Y;
    sensor["data"][1]["value"] = diffBuffer[0][1];
    sensor["data"][2]["sensortype"] = SENSOR_ACC1_Z;
    sensor["data"][2]["value"] = diffBuffer[0][2];
    sensor["data"][3]["sensortype"] = SENSOR_ANG1_X;
    sensor["data"][3]["value"] = diffBuffer[1][0];
    sensor["data"][4]["sensortype"] = SENSOR_ANG1_Y;
    sensor["data"][4]["value"] = diffBuffer[1][1];
    sensor["data"][5]["sensortype"] = SENSOR_ANG1_Z;
    sensor["data"][5]["value"] = diffBuffer[1][2];
    sensor["data"][6]["sensortype"] = SENSOR_ANGVEL1_X;
    sensor["data"][6]["value"] = diffBuffer[2][0];
    sensor["data"][7]["sensortype"] = SENSOR_ANGVEL1_Y;
    sensor["data"][7]["value"] = diffBuffer[2][1];
    sensor["data"][8]["sensortype"] = SENSOR_ANGVEL1_Z;
    sensor["data"][8]["value"] = diffBuffer[2][2];
    }
  else if(sen==2){
    sensor["type"]="SENSOR2";
    sensor["data"][0]["sensortype"] = SENSOR_ACC1_X;
    sensor["data"][0]["value"] = diffBuffer[0][3];
    sensor["data"][1]["sensortype"] = SENSOR_ACC1_Y;
    sensor["data"][1]["value"] = diffBuffer[0][4];
    sensor["data"][2]["sensortype"] = SENSOR_ACC1_Z;
    sensor["data"][2]["value"] = diffBuffer[0][5];
    sensor["data"][3]["sensortype"] = SENSOR_ANG1_X;
    sensor["data"][3]["value"] = diffBuffer[1][3];
    sensor["data"][4]["sensortype"] = SENSOR_ANG1_Y;
    sensor["data"][4]["value"] = diffBuffer[1][4];
    sensor["data"][5]["sensortype"] = SENSOR_ANG1_Z;
    sensor["data"][5]["value"] = diffBuffer[1][5];
    sensor["data"][6]["sensortype"] = SENSOR_ANGVEL1_X;
    sensor["data"][6]["value"] = diffBuffer[2][3];
    sensor["data"][7]["sensortype"] = SENSOR_ANGVEL1_Y;
    sensor["data"][7]["value"] = diffBuffer[2][4];
    sensor["data"][8]["sensortype"] = SENSOR_ANGVEL1_Z;
    sensor["data"][8]["value"] = diffBuffer[2][5];
    }
  }

void postHTTP(int sen){
  HTTPClient http;
  sensorPOST(sen);
  String requestBody;
  serializeJson(sensor, requestBody);

  http.begin("http://sacheonchallenge.toysmythiot.com:5000/sensor"); 
  http.addHeader("Content-Type", "application/json", "Content-Length", requestBody.length());

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode > 0) {
      errcnt_countup(&err_api, 0);
      Serial.print("sensor data post result: ");
      Serial.println(httpResponseCode);
      Serial.println(http.getString());
  } else {
      errcnt_countup(&err_api, 1);
      Serial.print("error with httpResponseCode: ");
      Serial.println(httpResponseCode);
  }

  sensor.clear();
  requestBody.clear();
  http.end();

}


void setup() 
{ 
  Serial.begin(9600);
  rs485.begin(9600, SERIAL_8N1, RXD2, TXD2);
  rs485.flush();

  // ethernet
  int cnt = 0;
  WiFi.onEvent(WiFiEvent);
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);
  do {
    if (++cnt == 10) {
      TIME_TO_SLEEP = (50 * 60);
      config_sleep_mode();
      Serial.println("Can not connect ethernet -> ESP sleep now");
      Serial.flush();
      esp_deep_sleep_start();
    }
    Serial.println("Waiting for ethernet connection");
    delay(10000);
  } while (!eth_connected);

  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();  
  
  Serial.println("--------------- Serial Initiated ---------------");
  calibrateAcc(1);
  calibrateAcc(2);
//  calibrateMag(1);
//  calibrateMag(2);
  Serial.println("--------------- Calibration Done ---------------");
  rs485.flush();
  
  if(rs485_receive(trashBuffer, 180) != -1){
    for(int i = 0;i<180;i++){
      Serial.print(trashBuffer[i],HEX);
      Serial.print(",");
      }
      Serial.println();
    }
}


void loop() 
{ 
  server.handleClient();
  rs485.flush();
  if(++flag==1){
    Serial.println("WT901C485 read");
    readSensor(prevBuffer);
    delay(500);
  }
  readSensor(newBuffer);
  calcSensor();
  postHTTP(1);
  postHTTP(2);
  calcAbsVal(2,1);
  calcAbsVal(2,2);
  clearBuffer();
}
