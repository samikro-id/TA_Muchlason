#include <WiFi.h>
#include "mqtt_secrets.h"
#include "PubSubClient.h"                   // Install Library by Nick O'Leary version 2.7.0
#include "ADS1X15.h"                        // Install Library by Rob Tillaart version 0.3.9
// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include "RTClib.h"

RTC_DS3231 rtc;
// char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

char nama_wifi[] = "HL_Antrian";
char password_wifi[] = "37123662";

WiFiClient espClient;
PubSubClient client(espClient);

#define MQTT_ID         "ddfdf6cd-f3eb-4936-a8cb-440ff3518b97"

#define MQTT_BROKER     "broker.emqx.io"            //
#define MQTT_PORT       1883                        //
#define MQTT_USERNAME   ""                          // Change to your Username from Broker
#define MQTT_PASSWORD   ""                          // Change to your password from Broker
#define MQTT_TIMEOUT    10
#define MQTT_QOS        0
#define MQTT_RETAIN     false

#define MQTT2_BROKER    "mqtt3.thingspeak.com"      
#define MQTT2_PORT      1883                       
#define MQTT2_TIMEOUT   50
#define MQTT2_QOS       0
#define MQTT2_RETAIN    false
#define CHANNEL_ID      2506798

#define MQTT_LEN  100
String mqtt_payload;

#define SERIAL_LEN   1000
char text[SERIAL_LEN];

#define LED_BUILTIN         2

#define RELAY_ON            LOW
#define RELAY_OFF           HIGH

#define RELAY_1_PIN         14

#define GAIN_V_BAT          16
#define GAIN_I_BATT         0.050
#define GAIN_V_PANEL        15.7
#define GAIN_I_PANEL        0.050

ADS1115 ads1(0x48);

typedef struct{
    bool connection;
    bool mqtt;
    bool led;
    bool charge;
    bool pompa;
    bool chart;
}STATUS_TypeDef;
STATUS_TypeDef status;

#define LED_TIME_MQTT       100
#define LED_TIME_CONNECTED  1000
#define LED_TIME_DISCONNECT 2000

#define TIMEOUT_RECONNECT       60000
#define TIMEOUT_CHART           900000
#define TIMEOUT_UPDATE          1000
#define TIMEOUT_UPDATE_CHARGE   180000
#define TIMEOUT_SENSOR          100
typedef struct{
    uint32_t led;
    uint32_t connection;
    uint32_t chart;
    uint32_t update;
    uint32_t update_charge;
    uint32_t update_sensor;
}TIMEOUT_TypeDef;
TIMEOUT_TypeDef timeout;

#define VOLT_PANEL_CHARGE       10.0
#define VOLT_PANEL_DISCHARGE    8.0
typedef struct{
    float v_bat;
    float i_bat;
    float v_panel;
    float i_panel;
}DATA_TypeDef;
DATA_TypeDef data;

uint8_t sensor_n = 0;

void setup(){
    delay(3000);
    Serial.begin(115200);

    if (! rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1) delay(10);
    }

    if (rtc.lostPower()) {
        Serial.println("RTC lost power, let's set the time!");
        // When time needs to be set on a new device, or after a power loss, the
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    // When time needs to be re-set on a previously configured device, the
    // following line sets the RTC to the date & time this sketch was compiled
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));

    initLed();
    initPompa();

    status.connection = false;
    status.mqtt = false;
    status.chart = false;

    timeout.connection = millis() - TIMEOUT_RECONNECT;
    timeout.update = millis();
    timeout.chart = millis() - TIMEOUT_CHART;

    Serial.println("Init");
}

void loop(){
    if(WiFi.isConnected()){
        if(status.connection){  toggleLed(LED_TIME_MQTT);    }
        else {                  toggleLed(LED_TIME_CONNECTED);  }

        checkChart();

        status.connection = client.loop();
        if(status.connection){
            processData();
            publishChart();
        }
        else{
            status.connection = mqttConnect();
        }
    }
    else{
        toggleLed(LED_TIME_DISCONNECT);
        
        wifiReconnect();
    }

    if((millis() - timeout.update) > TIMEOUT_UPDATE){
        uint32_t elapsed_time = millis() - timeout.update;

        // Serial.println("=========== update ==============");
        timeout.update = millis();

        DateTime now = rtc.now();

        // Serial.print(now.year(), DEC);
        // Serial.print('/');
        // Serial.print(now.month(), DEC);
        // Serial.print('/');
        // Serial.print(now.day(), DEC);
        // Serial.print(" (");
        // Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
        // Serial.print(") ");
        // Serial.print(now.hour(), DEC);
        // Serial.print(':');
        // Serial.print(now.minute(), DEC);
        // Serial.print(':');
        // Serial.print(now.second(), DEC);
        // Serial.println();
    }
    else{
        if((millis() - timeout.update_sensor) > TIMEOUT_SENSOR){

            ads1.begin(21,22);   ads1.setDataRate(7);

            sensor_n++;
            switch(sensor_n){
                case 1: data.v_bat = vBatt(); break;
                case 2: data.v_panel = vPanel(); break;
                case 3: data.i_bat = iBatt(); break;
                case 4: data.i_panel = iPanel(); break;
                default : sensor_n = 0; break;
            }

            timeout.update_sensor = millis();
        }
    }
}

void processData(){
    if(status.mqtt){
        Serial.println(mqtt_payload);

        int index = mqtt_payload.indexOf("|");

        if(mqtt_payload.substring(0, index) == "GET"){
            index++;
            if(mqtt_payload.substring(index) == "DATA"){
                publishData();
            }
        }

        clearDataMqtt();
        
        status.mqtt = false;
    }
}

void publishData(){
    /* DATA|pompa|vbat|ibat|vpanel|ipanel*/
    sprintf(text, "DATA|%d|%0.2f|%0.2f|%0.2f|%0.2f", 
                status.pompa, data.v_bat, data.i_bat, data.v_panel, data.i_panel);

    mqttPublish(text);
}

void checkChart(){
    if((millis() - timeout.chart) > TIMEOUT_CHART){
        timeout.chart = millis();
        client.disconnect();
        status.connection = false;
        status.chart = true;
    }
}

void publishChart(){
    if(status.chart){
        sprintf(text,"field1=%.3f&field2=%.3f&field3=%.3f&field4=%.3f&field5=%.3f&field6=%.3f&field7=%.3f&status=MQTTPUBLISH",  
                data.v_bat, data.i_bat, 0,
                data.v_panel, data.i_panel,
                0, 0);
        // Serial.println(text);

        char topic[50];
        sprintf(topic,"channels/%d/publish", CHANNEL_ID);
        
        if(!client.publish(topic,text,false)){
            Serial.println("fail");
        };

        client.loop();
        client.disconnect();
        status.chart = false;
    }
}

/***** MQTT Handle *******/
void callback(char* topic, byte* payload, unsigned int length) { //A new message has been received
    if(status.mqtt == false){
        for(uint16_t i=0; i < length; i++){
            mqtt_payload += (char) payload[i];
        }

        status.mqtt = true;
    }
}

bool mqttConnect(){
    if(status.chart){
        Serial.println("chart");
        
        client.disconnect();
        client.setServer(MQTT2_BROKER, MQTT2_PORT);

        if(client.connect(SECRET_MQTT_CLIENT_ID, SECRET_MQTT_USERNAME, SECRET_MQTT_PASSWORD)){
            delay(500);
            return true;
        }
    }
    else{
        Serial.println("mqtt");

        clearDataMqtt();
        status.mqtt = false;

        client.disconnect();
        client.setServer(MQTT_BROKER, MQTT_PORT);
        client.setCallback(callback);
        
        if( client.connect(MQTT_ID, MQTT_USERNAME, MQTT_PASSWORD) ){
            delay(500);
            if(client.subscribe("samikro/cmd/project/7", MQTT_QOS)){
                return true;
            }
        }
    }

    return false;
}

void mqttPublish(char *payload){
    client.publish("samikro/data/project/7",payload,false);
}

/***** WIFI Handle *****/
void wifiReconnect(){
    if((millis() - timeout.connection) > TIMEOUT_RECONNECT){
        timeout.connection = millis();

        Serial.println("wifi");

        status.connection = false;

        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(nama_wifi, password_wifi);
    }
};

/***** Voltage And Current Setting ****/
float vBatt(){
    // Serial.print("VBatt ");
    float vBat = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(2);     // GAIN 2.048

        int16_t raw = ads1.readADC(1);
        vBat = raw * ads1.toVoltage(1) * GAIN_V_BAT;

        if(vBat < 0){   vBat = 0;   }

        // Serial.println(vBat);
    }
    else{
        Serial.println("Not Connect");
    }
    return vBat;
}

float iBatt(){
    // Serial.print("IBatt ");
    float iLoad = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(16);    // GAIN 0.254

        int16_t raw = ads1.readADC(2);
        iLoad = (raw * ads1.toVoltage(1)) / GAIN_I_BATT;

        if(-0.01 < iLoad && iLoad < 0.01){
            iLoad = 0.0;
        }

        // Serial.println(iLoad);
    }
    else{
        Serial.println("Not Connect");
    }
    return iLoad;
}

float vPanel(){
    // Serial.print("VPanel ");
    float vPanel = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(2);     // GAIN 2.048

        int16_t raw = ads1.readADC(0);
        vPanel = data.v_bat - (raw * ads1.toVoltage(1) * GAIN_V_PANEL);

        // vPanel -= data.v_bat;

        if(vPanel < 0){   vPanel = 0;   }

        // Serial.println(vPanel);
    }
    else{
        Serial.println("Not Connect");
    }
    return vPanel;
}

float iPanel(){
    // Serial.print("IPanel ");
    float iPanel = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(16);    // GAIN 0.254

        int16_t raw = ads1.readADC(3);
        iPanel = -1 * (raw * ads1.toVoltage(1)) / GAIN_I_PANEL;

        if(-0.01 < iPanel && iPanel < 0.01){
            iPanel = 0.0;
        }

        // Serial.println(iPanel);
    }
    else{
        Serial.println("Not Connect");
    }
    return iPanel;
}

/***** Relay Handle ******/
void initPompa(){
    pinMode(RELAY_1_PIN, OUTPUT);

    pompaOff();
}

void pompaOn(){
    digitalWrite(RELAY_1_PIN, RELAY_ON);
    status.pompa = true;
}

void pompaOff(){
    digitalWrite(RELAY_1_PIN, RELAY_OFF);
    status.pompa = false;
}

/***** LED Setting *****/
void initLed(){
    pinMode(LED_BUILTIN, OUTPUT);
    setLed(true);
}

void toggleLed(uint32_t timer){
    if((millis() - timeout.led) > timer){
        timeout.led = millis();

        setLed(!status.led);
    }
}

void setLed(bool state){
    if(state){  digitalWrite(LED_BUILTIN, LOW);     }// nyalakan LED
    else{       digitalWrite(LED_BUILTIN, HIGH);    }// matikan LED

    status.led = state;
}

/***** Tambahan *******/
void clearDataMqtt(){
    mqtt_payload = "";
}

int findChar(char * data, char character, int start_index){
    int n;
    for(n=start_index; n < strlen(data); n++){
        if(data[n] == character){
            break;
        }
    }

    return n;
}

String charToString(char * data, int start, int end){
    String buff = "";
    for(int n=start; n<=end; n++){
        buff += data[n];
    }

    return buff;
}