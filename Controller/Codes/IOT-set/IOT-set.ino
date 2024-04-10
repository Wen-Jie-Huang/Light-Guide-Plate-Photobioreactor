/* esp8266控制光强曲线
 * 浊度、温度记录与反馈
 */
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <FS.h>  
#include <NTPClient.h>

//在这里更改局域网账号密码
String AP_ssid = "PCSM-Lab";
String password = "algaetech";

bool lockIP=1;//是否固定IP
String titleString = "          LGP No.1";
String shortTitle = "LGP1";
IPAddress local_IP(192, 168, 1, 201);//固定IP地址
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
float turbidity=1023;
float realtime_turbidity;
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

int hourVal1,hourVal2,hourVal3,hourVal4,hourVal5;
int lightVal,lightPWM,lightVal1,lightVal2,lightVal3,lightVal4,lightVal5;

bool changeSetting=0;
int changeCount=0;

void setup() {
  analogWrite(LED_pin,0);
  // put your setup code here, to run once:
  Serial.begin(9600); 
  
  hourVal1=hourVal2=hourVal3=hourVal4=hourVal5=24;
  lightVal=lightPWM=lightVal1=lightVal2=lightVal3=lightVal4=lightVal5=10;
  
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
  sprintf(buf,"hVals:,%d,%d,%d,%d,%d,lVals:,%d,%d,%d,%d,%d,",
              hourVal1,hourVal2,hourVal3,hourVal4,hourVal5,
              lightVal1,lightVal2,lightVal3,lightVal4,lightVal5);
  logFile.println();
  logFile.println("System Reboot,");
  logFile.print(String(dayStamp)+","+String(timeStamp)+",turbidity,tempC,lightVal,lightPWM,countHour,countMin,setting: "+String(buf)+",,,,,,,"); 
  delay(10);
  logFile.close();  
  delay(10);


  if (SPIFFS.exists("timeSetting.txt")){
    File timeSetting = SPIFFS.open("timeSetting.txt", "r");
    String timeString="";
    for(int i=0; i<timeSetting.size(); i++){
      timeString += (char)timeSetting.read();       
    }
    char buf[30];
    strcpy( buf, timeString.c_str());
    sscanf( buf, "%d,%d,%d,%d,%d,",&hourVal1,&hourVal2,&hourVal3,&hourVal4,&hourVal5);
    ////Serial.print("Read timeString: ");
    ////Serial.println(timeString);
    ////Serial.print("Read hourVal: ");
    ////Serial.print(hourVal1);////Serial.print(", ");////Serial.print(hourVal2);////Serial.print(", ");////Serial.print(hourVal3);////Serial.print(", ");////Serial.print(hourVal4);////Serial.print(", ");////Serial.print(hourVal5);////Serial.println(", ");
    timeSetting.close(); 
  }
  else{
    File timeSetting = SPIFFS.open("timeSetting.txt", "w");
    timeSetting.print(String(hourVal1)+","+String(hourVal2)+","+String(hourVal3)+","+String(hourVal4)+","+String(hourVal5)+",");
    ////Serial.print("Write timeSetting.txt: ");
    ////Serial.print(hourVal1);////Serial.print(", ");////Serial.print(hourVal2);////Serial.print(", ");////Serial.print(hourVal3);////Serial.print(", ");////Serial.print(hourVal4);////Serial.print(", ");////Serial.print(hourVal5);////Serial.println(", ");
    timeSetting.close(); 
  }
  
  if (SPIFFS.exists("lightSetting.txt")){
    File lightSetting = SPIFFS.open("lightSetting.txt", "r");
    String lightString="";
    for(int i=0; i<lightSetting.size(); i++){
      lightString += (char)lightSetting.read();       
    }
    char buf[30];
    strcpy( buf, lightString.c_str());
    sscanf( buf, "%d,%d,%d,%d,%d,",&lightVal1,&lightVal2,&lightVal3,&lightVal4,&lightVal5);
    ////Serial.print("Read lightSetting: ");
    ////Serial.println(lightString);
    ////Serial.print("Read lightVal: ");
    ////Serial.print(lightVal1);////Serial.print(", ");////Serial.print(lightVal2);////Serial.print(", ");////Serial.print(lightVal3);////Serial.print(", ");////Serial.print(lightVal4);////Serial.print(", ");////Serial.print(lightVal5);////Serial.println(", ");
    lightSetting.close(); 
  }
  else{
    File lightSetting = SPIFFS.open("lightSetting.txt", "w");
    lightSetting.print(String(lightVal1)+","+String(lightVal2)+","+String(lightVal3)+","+String(lightVal4)+","+String(lightVal5)+",");
    ////Serial.print("Write lightSetting.txt: ");
    ////Serial.print(lightVal1);////Serial.print(", ");////Serial.print(lightVal2);////Serial.print(", ");////Serial.print(lightVal3);////Serial.print(", ");////Serial.print(lightVal4);////Serial.print(", ");////Serial.print(lightVal5);////Serial.println(", ");
    lightSetting.close(); 
  }
  
  esp8266_server.begin();
  ////Serial.println("HTTP server started");
  esp8266_server.on("/readADC", handleTurbidity); 
  esp8266_server.on("/readTemp", handleTemp);
  esp8266_server.on("/readRunTime", handleRunTime);
  esp8266_server.on("/downloadFile", handleReadLog);
  esp8266_server.on("/deleteFile", handleCleanLog);
  esp8266_server.on("/readTitle", handleTitle);
  esp8266_server.on("/setTime1", handleSetTime1);
  esp8266_server.on("/setTime2", handleSetTime2);
  esp8266_server.on("/setTime3", handleSetTime3);
  esp8266_server.on("/setTime4", handleSetTime4);
  esp8266_server.on("/setTime5", handleSetTime5);
  esp8266_server.on("/setLight1", handleSetLight1);
  esp8266_server.on("/setLight2", handleSetLight2);
  esp8266_server.on("/setLight3", handleSetLight3);
  esp8266_server.on("/setLight4", handleSetLight4);
  esp8266_server.on("/setLight5", handleSetLight5);
  esp8266_server.on("/saveSetting", handleSaveSetting);  
  esp8266_server.onNotFound(handleUserRequest); 

  pinMode(LED_pin,OUTPUT);
  
  controlLight();

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
    controlLight();
    timeSliceRun=0;
  }
  
  if(timeSliceRun){
    timeSliceRun=0;
    readValue();
    displayOLED();
  }


  if(changeSetting){
    changeCount++;
    if(changeCount>500000){
      changeSetting=changeCount=0;
      controlLight();
      ticker.attach(2, runTimeSlice);
    }
  }
}









              

void readValue(){
  realtime_turbidity = median_filtering(A0);
  turbidity += realtime_turbidity;
 
  sensors.requestTemperatures(); 
  realtime_tempC = sensors.getTempCByIndex(0);
  tempC += realtime_tempC;

  averageTCount++;
}

void displayOLED(){
  updateTimeMin();
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
  display.drawString(72, 13, "Light: " + String(lightVal)+"%");
  display.drawString(6, 23, "IP : " + ipString );
  display.drawString(6, 33, "Time: "  + dayStamp +", "+ timeStamp);
  display.drawString(6, 43, "Turbidity: " + String(realtime_turbidity) );
  display.drawString(6, 53, "Temperature: " + String(realtime_tempC) );

  display.display();
  //////Serial.println("Display Showed");
}




void writeFile(){
  ticker.detach();
  // Serial.println(">>GetVal");
  while(Serial.read() >= 0){}
  unsigned char data[] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x95, 0xCB}; // Hex data to be sent
  Serial.write(data, sizeof(data)); // Send the data over serial
  delay(50);

  unsigned char data2[9]; // Define an array to store the received bytes
  unsigned int V_u16;
  float V_float;
  unsigned int I_u16;
  float I_float;
  
  updateTime();
  turbidity = turbidity/averageTCount;
  tempC = tempC/averageTCount;
  averageTCount=0;
  String messageString = String(dayStamp)+","+String(timeStamp)+","+String(turbidity,2)+","+String(tempC,2)+","+String(lightVal,DEC)+","+String(lightPWM,DEC)+","+String(countHour,DEC)+","+String(countMin,DEC)+","; 
  turbidity=0;
  tempC=0;
  
  
  if (Serial.available()) { // Wait until there are 8 bytes (the size of the message) available in the serial buffer
    for (int i = 0; i < 9; i++) {
      data2[i] = Serial.read(); // Read each byte and store it in the array
    }
  }


  V_u16 =data2[3] * 256 + data2[4] ;
  V_float =(float)V_u16/100.0;

  I_u16 =data2[5] * 256 + data2[6] ;
  I_float =(float)I_u16/100.0;

  voltmeterString =  String(V_float)+","+String(I_float); 

  File logFile = SPIFFS.open(file_name, "a");
  logFile.println();
  logFile.print(messageString+voltmeterString+",,,,,,");
  delay(10);
  logFile.close();  
  delay(10);

  ////Serial.println(messageString+voltmeterString);
  
  ticker.attach(2, runTimeSlice);
}

void handleSaveSetting(){
  ticker.detach();
  char buf1[100];
  char buf2[100];
  sprintf(buf1,"%d,%d,%d,%d,%d,",hourVal1,hourVal2,hourVal3,hourVal4,hourVal5);
  sprintf(buf2,"%d,%d,%d,%d,%d,",lightVal1,lightVal2,lightVal3,lightVal4,lightVal5);
  File timeSetting = SPIFFS.open("timeSetting.txt", "w");
  timeSetting.print(String(buf1));
  ////Serial.print("Write timeSetting: ");
  ////Serial.println(String(buf1));
  delay(10);
  timeSetting.close(); 
  delay(10);
  File lightSetting = SPIFFS.open("lightSetting.txt", "w");
  lightSetting.print(String(buf2));
  ////Serial.print("Write lightSetting: ");
  ////Serial.println(String(buf2));
  delay(10);
  lightSetting.close(); 
  delay(10);
  countMin=countHour=0;
  updateTime();
  File logFile = SPIFFS.open(file_name, "a");
  char buf[100];
  sprintf(buf,"hVals:,%d,%d,%d,%d,%d,lVals:,%d,%d,%d,%d,%d,",
              hourVal1,hourVal2,hourVal3,hourVal4,hourVal5,
              lightVal1,lightVal2,lightVal3,lightVal4,lightVal5);
  logFile.println();
  logFile.println("System Reboot,");
  logFile.print(String(dayStamp)+","+String(timeStamp)+",turbidity,tempC,lightVal,lightPWM,countHour,countMin,V,I,setting: "+String(buf)+",,,,,,,"); 
  delay(10);
  logFile.close();  
  delay(10);
  //Send the Modbus command to clear the charge record
  for(int i=0;i<sizeof(clearElectric);i++){
    Serial.write(clearElectric[i]);
  }
  ticker.attach(2, runTimeSlice);
  esp8266_server.sendHeader("Location","/index.html"); 
  esp8266_server.send(303);
}






void handleTurbidity() {
  String adcValue = String(realtime_turbidity);
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
  char buf[100];
  sprintf(buf,"hVals:,%d,%d,%d,%d,%d,lVals:,%d,%d,%d,%d,%d,",
              hourVal1,hourVal2,hourVal3,hourVal4,hourVal5,
              lightVal1,lightVal2,lightVal3,lightVal4,lightVal5);
  logFile.println();
  logFile.println("System Reboot,");
  logFile.print(String(dayStamp)+","+String(timeStamp)+",turbidity,tempC,lightVal,lightPWM,countHour,countMin,setting: "+String(buf)+",,,,,,,"); 
  delay(10);
  logFile.close();  
  delay(10);
  esp8266_server.sendHeader("Location","/cleaned.html"); 
  esp8266_server.send(303);
  ticker.attach(2, runTimeSlice);
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
#define median_filtering_length 31 
float median_filtering(int sensorPin){
  int senseV[median_filtering_length];
  for(int i = 0; i < median_filtering_length; i++){
    senseV[i] = analogRead(sensorPin);
    delay(1);
  }
  for(int i = 0; i < median_filtering_length; i++ ){
    for(int k = i; k <median_filtering_length; k++ ){
      if(senseV[i] > senseV[k]){
          int tmp = senseV[i];
          senseV[i] = senseV[k];
          senseV[k] = tmp;
      }
    }  
  }
  float sv = 0;
  float num = 0;
  for(int i = 1; i < 15; i++){
    num = senseV[median_filtering_length/2+i]+senseV[median_filtering_length/2-i];
    sv = num + sv ; //*100.0/1024.0
  }
  sv = (sv+senseV[median_filtering_length/2])/29;
  //////Serial.println(senseV[median_filtering_length/2]);
  return sv;
  sv = num = 0;
}
void handleTitle() {
  int hval;
  char buf[100];
  sprintf(buf,"{\"title\":\"%s\",\"hVals\":[%d,%d,%d,%d,%d],\"lVals\":[%d,%d,%d,%d,%d]}",
                titleString.c_str(),hourVal1,hourVal2,hourVal3,hourVal4,hourVal5,
                lightVal1,lightVal2,lightVal3,lightVal4,lightVal5);
  esp8266_server.send(200, "text/plain", String(buf)); 

}
void handleSetTime1(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String timeStr = esp8266_server.arg("hour"); // 获取用户请求中的PWM数值
  hourVal1 = timeStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(hourVal1);
  analogWrite(LED_pin,lightVal1*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetTime2(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String timeStr = esp8266_server.arg("hour"); // 获取用户请求中的PWM数值
  hourVal2 = timeStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(hourVal2);
  analogWrite(LED_pin,lightVal2*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetTime3(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String timeStr = esp8266_server.arg("hour"); // 获取用户请求中的PWM数值
  hourVal3 = timeStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(hourVal3);
  analogWrite(LED_pin,lightVal3*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetTime4(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String timeStr = esp8266_server.arg("hour"); // 获取用户请求中的PWM数值
  hourVal4 = timeStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(hourVal4);
  analogWrite(LED_pin,lightVal4*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetTime5(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String timeStr = esp8266_server.arg("hour"); // 获取用户请求中的PWM数值
  hourVal5 = timeStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(hourVal5);
  analogWrite(LED_pin,lightVal5*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetLight1(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String lightStr = esp8266_server.arg("pwm"); // 获取用户请求中的PWM数值
  lightVal1 = lightStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(lightVal1);
  analogWrite(LED_pin,lightVal1*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetLight2(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String lightStr = esp8266_server.arg("pwm"); // 获取用户请求中的PWM数值
  lightVal2 = lightStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(lightVal2);
  analogWrite(LED_pin,lightVal2*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetLight3(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String lightStr = esp8266_server.arg("pwm"); // 获取用户请求中的PWM数值
  lightVal3 = lightStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(lightVal3);
  analogWrite(LED_pin,lightVal3*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetLight4(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String lightStr = esp8266_server.arg("pwm"); // 获取用户请求中的PWM数值
  lightVal4 = lightStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(lightVal4);
  analogWrite(LED_pin,lightVal4*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
}
void handleSetLight5(){
  ticker.detach();
  changeSetting=1;
  changeCount=0;
  String lightStr = esp8266_server.arg("pwm"); // 获取用户请求中的PWM数值
  lightVal5 = lightStr.toInt();              // 将用户请求中的PWM数值转换为整数
  //////Serial.println(lightVal5);
  analogWrite(LED_pin,lightVal5*255/100);

  esp8266_server.send(200, "text/plain");//向客户端发送200响应信息
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

void controlLight(){
  if(countHour<hourVal1){
    lightVal=lightVal1;
  }
  else if(countHour<(hourVal1+hourVal2)){
    lightVal=lightVal2;
  }
  else if(countHour<(hourVal1+hourVal2+hourVal3)){
    lightVal=lightVal3;
  }
  else if(countHour<(hourVal1+hourVal2+hourVal3+hourVal4)){
    lightVal=lightVal4;
  }
  else{
    lightVal=lightVal5;
  }
  lightPWM=lightVal*255/100;
  analogWrite(LED_pin,lightPWM);
}
