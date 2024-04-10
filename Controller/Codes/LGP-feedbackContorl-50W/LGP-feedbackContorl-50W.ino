/* esp8266控制光强曲线
 * 控制间歇性通气
 * 浊度、温度记录与反馈
 */

 /*
  * 反馈控制，根据浊度计读数控制pwm
  */
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <FS.h>  
#include <NTPClient.h>


String AP_ssid = "PCSM-Lab";
String password = "algaetech";

bool lockIP=1;//是否固定IP
String titleString = "    Feedback LGP No.4";
String shortTitle = "50WLGP4";
IPAddress local_IP(192, 168, 1, 184);//固定IP地址
IPAddress gateway( 192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress DNS1(114, 114, 114, 114);
IPAddress DNS2(8, 8, 8, 8);

ESP8266WebServer esp8266_server(80);

#include <OneWire.h>
#include <DallasTemperature.h>
const int oneWireBus = 0;//D3
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

#include <Ticker.h>
Ticker ticker;

#include "SSD1306Wire.h" 
SSD1306Wire display(0x3c, SDA, SCL); 

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com");

#define LED_pin D4 
String file_name = shortTitle+"_log.csv"; //Light Guided Plate
char clearElectric[]= {0x01,0x06,0x0C,0x26,0x12,0x34,0x66,0x26};//The Modbus command to clear the charge record
float tempC=0;
float realtime_tempC;
int averageTCount=0;
int timeSlice=0;
bool timeSliceRun=0;
int countMin=0;
int countHour=0;
String ipString;
String voltmeterString="";

String formattedDate;
String dayStamp;
String timeStamp;
int splitT;

int lightPWM;

//电压电流表反馈
unsigned char data2[9]; // Define an array to store the received bytes
unsigned int V_u16;
float V_float;
unsigned int I_u16;
float I_float;
float realPower,wantPower;

void setup() {
  analogWrite(LED_pin,0);
  // put your setup code here, to run once:
  Serial.begin(9600); 
  
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, titleString);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 20,"  Welcome!");
  display.display();
  
  if(SPIFFS.begin()){                       // 启动闪存文件系统
    ////Serial.println("SPIFFS Started.");
  } else {
    ////Serial.println("SPIFFS Failed to Start.");
  } 
  
  if(lockIP){
    WiFi.config(local_IP, gateway, subnet, DNS1, DNS2);//设置静态IP
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);//设置断开连接后重连
  ////Serial.println("已设置自动重新连接");
  ////Serial.println("开始连接");
  WiFi.begin(AP_ssid, password);
  ////Serial.print("正在连接到");
  ////Serial.print(AP_ssid);
  //这里的循环用来判断是否连接成功的。连接过程中每隔500毫秒会检查一次是否连接成功，，并打一个点表示正在连接中
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    i++;
    delay(500);
    ////Serial.print(".");
    if (i > 20) { 
      ////Serial.print("连接超时！请检查网络环境");
      break;
    }
  }
  ////Serial.println("网络连接成功");
  ipString = WiFi.localIP().toString().c_str();
  ////Serial.println(ipString);
  
  sensors.begin();
  
  timeClient.begin();
  timeClient.setTimeOffset(28800); //GMT +1 = 3600 GMT +8 = 28800 GMT -1 = -3600 GMT 0 = 0
  updateTime();
  File logFile = SPIFFS.open(file_name, "a");
  char buf[100];
  logFile.println();
  logFile.println("System Reboot,");
  logFile.print(String(dayStamp)+","+String(timeStamp)); 
  delay(10);
  logFile.close();  
  delay(10);
  
  esp8266_server.begin();
  esp8266_server.on("/readTitle", handleTitle);
  esp8266_server.on("/readADC", handlePower); 
  esp8266_server.on("/readTemp", handleTemp);
  esp8266_server.on("/readRunTime", handleRunTime);
  esp8266_server.on("/downloadFile", handleReadLog);
  esp8266_server.on("/deleteFile", handleCleanLog);
  esp8266_server.onNotFound(handleUserRequest); 

  pinMode(LED_pin,OUTPUT);

  ticker.attach(2, runTimeSlice);
}

void runTimeSlice(){
  
  if(timeSlice<60){
    timeSlice+=2;
  }
  else{
    timeSlice=2;
  }
  timeSliceRun=1;
  ////Serial.print(".");
}


void loop(){
  esp8266_server.handleClient();  //处理网络请求
  
  if(timeSlice==6&&timeSliceRun==1){
    ////Serial.println();
    countMin++;
    if(countMin%5==0){//记录的时间间隔
      writeFile();
      display.init();
      display.flipScreenVertically();
      displayOLED();
    }
    if(countMin%60==0){
      countHour++;
      countMin=0;
    }
    timeSliceRun=0;
  }
  
  if(timeSliceRun){
    timeSliceRun=0;

    while(Serial.read() >= 0){}
    unsigned char data[] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x95, 0xCB}; // Hex data to be sent
    Serial.write(data, sizeof(data)); // Send the data over serial
    delay(50);

    if (Serial.available()) { // Wait until there are 8 bytes (the size of the message) available in the serial buffer
    for (int i = 0; i < 9; i++) {
      data2[i] = Serial.read(); // Read each byte and store it in the array
      }
    }
    V_u16 =data2[3] * 256 + data2[4] ;
    V_float =(float)V_u16/100.0;
    I_u16 =data2[5] * 256 + data2[6] ;
    I_float =(float)I_u16/100.0;
    realPower = I_float * V_float;//0-100

    if(realPower>wantPower&&lightPWM>=0){
      lightPWM--;
    }
    else if(realPower<wantPower&&lightPWM<=255){
      lightPWM++;
    }
    
    analogWrite(LED_pin,lightPWM);

    if(countHour<24){
      wantPower = 2.5;
    }
    else if(countHour>=24&&countHour<48){
      wantPower = 14.375;
    }
    else if(countHour>=48&&countHour<72){
      wantPower = 26.25;
    }
    else if(countHour>=72&&countHour<96){
      wantPower = 38.125;
    }
    else if(countHour>=96&&countHour<120){
      wantPower = 50;
    }

    displayOLED();
  }
}


void displayOLED(){
  updateTimeMin();
  sensors.requestTemperatures(); 
  realtime_tempC = sensors.getTempCByIndex(0);
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, titleString);

  if (WiFi.status() == WL_CONNECTED){
    display.drawString(28, 13, "WiFi");
  }
  if(SPIFFS.begin()) {
    display.drawString(6, 13, "SD");
  }
  display.drawString(72, 13, "PWM: " + String(lightPWM)+"%");
  display.drawString(6, 23, "IP : " + ipString );
  display.drawString(6, 33, "Time: "  + dayStamp +", "+ timeStamp);
  display.drawString(6, 43, "Power: " + String(realPower) );
  display.drawString(6, 53, "Temperature: " + String(realtime_tempC) );

  display.display();
  //////Serial.println("Display Showed");
}

void writeFile(){
  ticker.detach();

  updateTime();
  String messageString = String(dayStamp)+","+String(timeStamp)+","+String(realPower,4)+","+String(realtime_tempC,2)+","+String(lightPWM,DEC)+","+String(countHour,DEC)+","+String(countMin,DEC)+","; 
  voltmeterString =  String(V_float)+","+String(I_float); 

  File logFile = SPIFFS.open(file_name, "a");
  logFile.println();
  logFile.print(messageString+voltmeterString+",,,,,,");
  delay(10);
  logFile.close();  
  delay(10);

  ticker.attach(2, runTimeSlice);
}

void handleReadLog() {
  ticker.detach();
  File readFile; 
  File download = SPIFFS.open(file_name, "r");
  if (download) {
    esp8266_server.sendHeader("Content-Type", "text/text");
    esp8266_server.sendHeader("Content-Disposition", "attachment; filename="+file_name);
    esp8266_server.sendHeader("Connection", "close");
    esp8266_server.streamFile(download, "application/octet-stream");
    download.close();
  } 
  ticker.attach(2, runTimeSlice);
}
void handleCleanLog(){
  updateTime();
  ticker.detach();
  File logFile = SPIFFS.open(file_name, "w");
  logFile.println();
  logFile.println("System Reboot,");
  logFile.print(String(dayStamp)+","+String(timeStamp)+",,,,,,,"); 
  delay(10);
  logFile.close();  
  delay(10);
  esp8266_server.sendHeader("Location","/cleaned.html"); 
  esp8266_server.send(303);
  ticker.attach(2, runTimeSlice);
}

void handleTitle() {
  esp8266_server.send(200, "text/plain", shortTitle); //发送模拟输入引脚到客户端ajax请求
}
void handlePower() {
  String adcValue = String(realPower);
  esp8266_server.send(200, "text/plain", adcValue); //发送模拟输入引脚到客户端ajax请求
}
void handleTemp() {
  String tempValue = String(realtime_tempC);
  esp8266_server.send(200, "text/plain", tempValue); //发送模拟输入引脚到客户端ajax请求
}
void handleRunTime() {
  String timeValue = String(countHour)+"h "+String(countMin)+"min";
  esp8266_server.send(200, "text/plain", timeValue); //发送模拟输入引脚到客户端ajax请求
}

void handleUserRequest() {             
  String reqResource = esp8266_server.uri();
  ////Serial.print("reqResource: ");
  ////Serial.println(reqResource);
  bool fileReadOK = handleFileRead(reqResource);
  if (!fileReadOK){                                                 
    esp8266_server.send(404, "text/plain", "404 Not Found"); 
  }
}
bool handleFileRead(String resource) {
  if (resource.endsWith("/")) {
    resource = "/index.html";
  } 
  String contentType = getContentType(resource);
  if (SPIFFS.exists(resource)) {
    File file = SPIFFS.open(resource, "r");
    esp8266_server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}
String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
void updateTime(){
  timeClient.update();
  formattedDate = timeClient.getFormattedDate();
  //////Serial.println(formattedDate);
  splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
}
void updateTimeMin(){
  timeClient.update();
  formattedDate = timeClient.getFormattedDate();
  //////Serial.println(formattedDate);
  splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  timeStamp = formattedDate.substring(splitT+1, formattedDate.indexOf(":",formattedDate.length()-5));
}
