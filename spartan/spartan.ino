#include <SoftwareSerial.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <time.h>
#include <EEPROM.h>
#define IS_TEMP_FCM_FREE_ADDRESS 0 // flag to prevent temperature FCM spam
#define RAIN_SENSOR_STATE 1 // store state of rain sensor
#define FCM_BODY_ADDRESS 10 // FCM request body

void(* resetFunc) (void) = 0;

#pragma region SSID
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
const char* ssid = "Brawijaya";
const char* password = "ujungberung";
// const char* ssid = "Sandy Asmara";
// const char* password = "sandydimas17";
#pragma endregion

#pragma region DHT11
#include <DHT.h>
#define DHTPIN D7     //D7 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
#pragma endregion

#pragma region Cloud Firestore
#include <Firebase_ESP_Client.h>
#define API_KEY "AIzaSyBbOZqg19S67nkfzis-7SXGvaQzN_GPGks"
#define FIREBASE_PROJECT_ID "apps-2ee38"
#define USER_EMAIL "ghesa@gmail.com"
#define USER_PASSWORD "123qwe"
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
#pragma endregion

#define RAIN_PIN_ANALOG A0 // A0

#pragma region DS18B20 Temperature Sensor
#include <OneWire.h>
#include <DallasTemperature.h>
// Data wire is plugged into digital pin 2 on the Arduino
#define ONE_WIRE_BUS D1 //D1
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
#pragma endregion

#pragma region Item Document
#define ITEM_0 "ZrM6BzaBriaNSliSGlXF"
#define ITEM_1 "XQBI3RT7LEPIDcVh6XWR"
#define ITEM_2 "BnF9PZs02yHXBTBdwszT"
#define ITEM_3 "XE584ON3H6cEE2Nn9btP"
#define ITEM_4 "lNYFTm6MssOC1ZtFSnZD"
#define ITEM_5 "TqMR7gF6MC5i9IeFdTfB"
#define ITEM_0_QC "ZQtO1TTwNsO2ilsLkQAx"
#define ITEM_1_QC "oriYYaxGGzZJof8TrZER"
#pragma endregion

#pragma region Global Variable
bool qcMode = true;
int currentTime = 0; // can be minute or hour or day
//"2023-02-26T17:00:00.00000Z"; // ISO 8601/RFC3339 UTC "Zulu" format
int itemCode = 0;
String currentDate = "2023-03-01T00:00:00.00000Z";
int errorCount = 0;
#pragma endregion

void setup() {
  Serial.println("[info] booting");
  
  #pragma region init Serial, EEPROM, Sensor, WiFi, and Cloud Firestore
  Serial.begin(115200);

  /*
  * for SensorType please see table CODEMASTER.CodeType = SensorType
  */
  sensors.begin();
  WiFi.begin(ssid, password);
  startWifiConnection();

  EEPROM.begin(1536);
  #pragma region send pending FCM
  Serial.println("[info] reading EEPROM");
  String recivedData = read_String(FCM_BODY_ADDRESS);
  if (recivedData != "0") {
    Serial.println("[info] pending FCM detected");
    sendFCM(recivedData);
  } else {
    Serial.println("[info] no FCM pending");
  }
  #pragma endregion

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  fbdo.setBSSLBufferSize(2048, 2048);
  fbdo.setResponseSize(2048);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  #pragma endregion

  #pragma region init OTA
  ArduinoOTA.onStart([]() {
    Serial.println("[info] init OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\[info] init OTA finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[info] Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[error] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  #pragma endregion

  // run main() one time test
  // main();
}

void loop() {
  ArduinoOTA.handle();
  // run main() loop mode
  main();
}

int main() {
  delay(30000); // 30 seconds
  // delay(60000); // 1 minute

  String _currentDate = getTimeStampNow();

  #pragma region DHT11 reading
  float currentReadHumidity = dht.readHumidity();
  float currentReadTemperature = dht.readTemperature();
  if (!isnan(currentReadHumidity) && !isnan(currentReadTemperature)) {
    while (!updateSensorHeader(currentReadHumidity, 2, currentDate)) {
      delay(2000);
    }
    while (!updateSensorHeader(currentReadTemperature, 0, currentDate)) {
      delay(2000);
    }
  }
  #pragma endregion

  #pragma region Rain reading
  float rainSensorValue = analogRead(RAIN_PIN_ANALOG);
  if (WiFi.status() == WL_CONNECTED) {
    if (Firebase.ready()) {
      // save captured sensor
      if (_currentDate != "") {
        while (!updateSensorHeader(rainSensorValue, 1, currentDate)) {
          delay(2000);
        }
        Serial.println("[success] update sensor header success!");
      }

      // send FCM
      int prevRainState = EEPROM.read(RAIN_SENSOR_STATE);
      String title = "";
      String message = "";
      if (rainSensorValue >= 600 && rainSensorValue < 900) {
        if (prevRainState != 1) {
          EEPROM.write(RAIN_SENSOR_STATE, 1);
          EEPROM.commit();
        }
        if (prevRainState < 1) {
          title = "Info";
          message = "Current weather is drizzle";
          fetchFCM(title, message);
        }
      } else if (rainSensorValue >= 400 && rainSensorValue < 600) {
        if (prevRainState != 2) {
          EEPROM.write(RAIN_SENSOR_STATE, 2);
          EEPROM.commit();
        }
        if (prevRainState < 2) {
          title = "Info";
          message = "Current weather is rain";
          fetchFCM(title, message);
        }
      } else if (rainSensorValue < 400) {
        if (prevRainState != 3) {
          EEPROM.write(RAIN_SENSOR_STATE, 3);
          EEPROM.commit();
        }
        if (prevRainState < 3) {
          title = "Warning";
          message = "Current weather is heavy rain";
          fetchFCM(title, message);
        }
      } else {
        if (prevRainState != 0) {
          EEPROM.write(RAIN_SENSOR_STATE, 0);
          EEPROM.commit();
        }
      }
    }
  }
  #pragma endregion

  Serial.printf("[info] proccessing ItemCode [%d]\n", itemCode);
  #pragma region Item Temperature reading
  sensors.requestTemperatures();
  float currentReadTemperatureItem = sensors.getTempCByIndex(itemCode);
  if (currentReadTemperatureItem != -127) {
    if (WiFi.status() == WL_CONNECTED) {
      if (Firebase.ready()) {
        // save captured sensor
        if (_currentDate != "") {
          currentDate = _currentDate;
          currentDate += currentDate.indexOf("Z") <= 0 ? "Z" : "";
          while (!updateItemHeader(itemCode, currentReadTemperatureItem, currentDate)) {
            delay(2000);
          }
          Serial.println("[success] update item header success!");
          // int timeRead = getCurrentMinute(currentDate.c_str()); // test only
          int timeRead = getCurrentHour(currentDate.c_str());
          if (currentTime != timeRead) {
            while (!insertLog(itemCode, currentReadTemperatureItem, currentDate)) {
              delay(2000);
            }
            EEPROM.write(IS_TEMP_FCM_FREE_ADDRESS, 1);
            EEPROM.commit();
            Serial.println("[success] logging success!");
            currentTime = timeRead; 
          }

          // send FCM
          if (currentReadTemperatureItem > 34 && EEPROM.read(IS_TEMP_FCM_FREE_ADDRESS) == 1) {
            String title = "Warning";
            String message = "Item " + String(itemCode) + " temperature is warm: " + String(currentReadTemperatureItem);
            fetchFCM(title, message);
          }
        }
      } else {
        Serial.println("[error] firestore connection not ready");
        resetIfOverfailed();
      }
    } else {
      startWifiConnection();
    }
  } else {
    Serial.printf("[error] DS18B20 sensor [%d] disconnected\n", itemCode);
    itemCode++;
    resetIfOverfailed();
  }
  #pragma endregion
  
  increaseItemCode();
}

bool updateItemHeader(int itemCode, float currentReadTemperatureItem, String dateNow) {
  String itemDocument = getDocumentCode(itemCode);
  String documentPath = qcMode ? "ITEMHEADERQC" : "ITEMHEADER";
  documentPath += "/" + itemDocument;
  FirebaseJson content;
  content.set("fields/TemperatureValue/doubleValue", String(currentReadTemperatureItem).c_str());
  content.set("fields/LastSensorUpdateDate/timestampValue", String(dateNow).c_str());
  if (Firebase.Firestore.patchDocument(
      &fbdo, 
      FIREBASE_PROJECT_ID, 
      "", 
      documentPath.c_str(),
      content.raw(), 
      "TemperatureValue,LastSensorUpdateDate")) {
    // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    return true;
  } else {
    Serial.println("[error] update item header failed!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return false;
  }
}

bool insertLog(int itemCode, float currentReadTemperatureItem, String dateNow) {
  String documentPath = qcMode ? "ITEMSENSORLOGQC" : "ITEMSENSORLOG";
  FirebaseJson content;
  content.set("fields/ItemCode/doubleValue", String(itemCode).c_str());
  content.set("fields/SensorValue/doubleValue", String(currentReadTemperatureItem).c_str());
  content.set("fields/SensorType/doubleValue", String(0).c_str());
  content.set("fields/CreateDate/timestampValue", String(dateNow).c_str());
  content.set("fields/CreateBy/stringValue", "system");
  if(Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
    // Serial.printf("ok\n%s\n", fbdo.payload().c_str());
    return true;
  } else {
    Serial.println("[error] logging failed!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return false;
  }
}

bool updateSensorHeader(float currentReadSensor, int sensorType, String dateNow) {
  String itemDocument = "";
  switch (sensorType) {
    case 0:
      itemDocument = qcMode ? "DgIZ9j9sHkaTIhZwN65x" : "IkQlE4jLWI83PMGHOvPy";
      break;
    case 1:
      itemDocument = qcMode ? "OwP1AiH2V9qFmUmFaxVj" : "kySLPPBr2ktANhdoCEU2";
      break;
    case 2:
      itemDocument = qcMode ? "hWcLWTHQQvA8ad0LRcwh" : "DdJuS08b3OektdixtvAc";
      break;
  }
  String documentPath = qcMode ? "SENSORHEADERQC" : "SENSORHEADER";
  documentPath += "/" + itemDocument;
  FirebaseJson content;
  content.set("fields/SensorValue/doubleValue", String(currentReadSensor).c_str());
  content.set("fields/LastUpdateDate/timestampValue", String(dateNow).c_str());
  if (Firebase.Firestore.patchDocument(
      &fbdo, 
      FIREBASE_PROJECT_ID, 
      "", 
      documentPath.c_str(),
      content.raw(), 
      "SensorValue,LastUpdateDate")) {
    // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    return true;
  } else {
    Serial.println("[error] update sensor header failed!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return false;
  }
}

String getTimeStampNow() {
  unsigned long randTime = millis();
  String serverTimePath = qcMode ? "SERVERTIMEQC/ServerTime" : "SERVERTIME/ServerTime";
  FirebaseJson content;
  content.set("fields/CreateBy/stringValue", String(randTime).c_str());
  content.set("fields/LastGet/timestampValue", String(currentDate).c_str());
  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str(), content.raw(), "CreateBy,LastGet")) {
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str())) {
      // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      DynamicJsonDocument doc(500);
      DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());
      if (error) {
        Serial.println("[error] deserializeJson() failed: ");
        Serial.println(error.f_str());
        resetIfOverfailed();
        return "";
      }
      const char* timeResult = doc["updateTime"];
      String date = convertDateTime(timeResult);
      return date;
    } else {
      Serial.println("[error] get server time failed!");
      Serial.println(fbdo.errorReason());
      resetIfOverfailed();
      return "";
    }
  } else {
    Serial.println("[error] set server time failed!");
    Serial.println(fbdo.errorReason());
    Serial.printf("[error] input used: ");
    Serial.printf(String(randTime).c_str());
    Serial.printf("\n");
    Serial.printf(String(currentDate).c_str());
    Serial.printf("\n");
    resetIfOverfailed();
    return "";
  }
}

void startWifiConnection() {
  #pragma region Reconnect WiFi
  Serial.printf("[info] connecting ");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[info] connected with IP: ");
  Serial.println(WiFi.localIP());
  #pragma endregion
}

String getDocumentCode(int itemCode) {
  if (!qcMode) {
    switch (itemCode) {
      default:
      case 0:
        return ITEM_0;
      case 1:
        return ITEM_1;
      case 2:
        return ITEM_2;
      case 3:
        return ITEM_3;
      case 4:
        return ITEM_4;
      case 5:
        return ITEM_5;
    }
  } else {
    switch (itemCode) {
      default:
      case 0:
        return ITEM_0_QC;
      case 1:
        return ITEM_1_QC;
    }
  }
}

void increaseItemCode() {
  if (qcMode) {
    // itemCode = (itemCode < 1) ? itemCode + 1 : 0;
    itemCode = 0;
  } else {
    // itemCode = (itemCode < 5) ? itemCode + 1 : 0;
    itemCode = 0;
  }
  
}

String convertDateTime(const char* date) {
  //https://arduino.stackexchange.com/questions/83860/esp8266-iso-8601-string-to-tm-struct
  struct tm tm = {0};
  char buf[100];
  // Convert to tm struct
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  // Can convert to any other format
  // strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &tm); //original
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  // Serial.printf("%s", buf);
  return String(buf);
}

int getCurrentHour(const char* date) {
  struct tm tm = {0};
  char buf[100];
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  return tm.tm_hour;
}

int getCurrentMinute(const char* date) {
  struct tm tm = {0};
  char buf[100];
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  return tm.tm_min;
}

void resetIfOverfailed() {
  errorCount++;
  if (errorCount >= 4) {
    Serial.println("[info] resetting device ...");
    resetFunc();
  }
}

void fetchFCM(String title, String message) {
  Serial.println("[info] get all user token for FCM");
  String tokens = "";
  String userPath = qcMode ? "USERQC" : "USER";
  if (Firebase.Firestore.listDocuments(&fbdo, FIREBASE_PROJECT_ID, "", userPath.c_str(), 3, "", "", "FcmToken", false)) {
    // Serial.printf("[info] user token result: \n%s\n\n", fbdo.payload().c_str());
    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());
    if (error) {
      Serial.printf("[error] deserializeJson() failed: ");
      Serial.println(error.f_str());
      resetIfOverfailed();
    } else {
      int i = 0;
      bool continueLoop = true;
      while (continueLoop) {
        String token = doc["documents"][i]["fields"]["FcmToken"]["stringValue"];
        tokens += (tokens == "") ? "\"" + token + "\"" : ",\"" + token + "\"";

        i++;
        String check = doc["documents"][i];
        continueLoop = (check != "null") ? true : false;
      }
    }
  } else {
    Serial.printf("[error] get user failed! ");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
  }

  String httpRequestData = "";
  if (tokens != "") {
    httpRequestData += "{\"registration_ids\": [" + tokens + "]";
    httpRequestData += ", \"notification\": { \"body\": \"" + message + "\"";
    httpRequestData += ", \"title\": \"" + title + "\" }}";
    writeString(FCM_BODY_ADDRESS, httpRequestData);
    // reset immediately
    errorCount = 4;
    resetIfOverfailed();
  }
}

void sendFCM(String httpRequestData) {
  if (WiFi.status() == WL_CONNECTED) {
    String auth = "key=AAAAfvFyFkM:APA91bHCvoVe9wXdtD7PM6on0qebHHkin2Cd28psimpNtS3jtthSBYOi4lBDC2lQNzeD_p2hMmvRhdI-STVbm-4TjfNQQ8a_BRjnBhJPdyRMQhIiCqtXwqJrwqz8rvEgrEw8F7I02Dqg";
    // https://randomnerdtutorials.com/esp8266-nodemcu-https-requests/#:~:text=ESP8266%20NodeMCU%20HTTPS%20Requests%20%E2%80%93%20No%20Certificate
    // //CONNECT: connect https
    std::unique_ptr<BearSSL::WiFiClientSecure>clients(new BearSSL::WiFiClientSecure);
    
    // // Ignore SSL certificate validation
    clients->setInsecure();
    
    // //create an HTTPClient instance
    HTTPClient https;
    WiFiClient client;
    
    //Initializing an HTTPS communication using the secure client
    Serial.print("[https] begin POST...\n");
    if (https.begin(*clients, "https://fcm.googleapis.com/fcm/send")) {  // HTTPS
      https.addHeader("Content-Type", "application/json");
      https.addHeader("Authorization", auth);
      // start connection and send HTTP header
      resend:
      int httpCode = https.POST(httpRequestData);
      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[https] POST... result code: %d\n", httpCode);
        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          // String payload = https.getString();
          // Serial.println(payload);
          clients = NULL;
          writeString(FCM_BODY_ADDRESS, "0");
          // reset immediately
          errorCount = 4;
          resetIfOverfailed();
        }
      } else {
        Serial.printf("[https] POST... failed, result code: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        resetIfOverfailed();
        goto resend;
      }
      https.end();
    } else {
      Serial.printf("[https] Unable to connect\n");
      resetIfOverfailed();
    }
  }
}

// https://circuits4you.com/2018/10/16/arduino-reading-and-writing-string-to-eeprom/
void writeString(char add,String data) {
  int _size = data.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(add+i,data[i]);
  }
  EEPROM.write(add+_size,'\0');   //Add termination null character for String Data
  EEPROM.commit();
}

// https://circuits4you.com/2018/10/16/arduino-reading-and-writing-string-to-eeprom/
String read_String(char add) {
  int i;
  char data[1536]; //Max 100 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(add);
  while(k != '\0' && len<1536)   //Read until null character
  {    
    k=EEPROM.read(add+len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  return String(data);
}
