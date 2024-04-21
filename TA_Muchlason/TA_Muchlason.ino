#include <WiFi.h>
#include "mqtt_secrets.h"
#include "PubSubClient.h"                   // Install Library by Nick O'Leary version 2.8.0
#include "ADS1X15.h"                        // Install Library by Rob Tillaart version 0.3.9
// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include "RTClib.h"

RTC_DS3231 rtc;
// char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

char nama_wifi[] = "Valerie";
char password_wifi[] = "ve12345678";

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
// #define CHANNEL_ID      2506798
#define CHANNEL_ID      2516558

#define MQTT_LEN  100
String mqtt_payload;

#define SERIAL_LEN   1000
char text[SERIAL_LEN];

#define LED_BUILTIN         2

#define RELAY_ON            HIGH
#define RELAY_OFF           LOW

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
    bool pompa;
    bool chart;
}STATUS_TypeDef;
STATUS_TypeDef status;

#define LED_TIME_MQTT           200
#define LED_TIME_CONNECTED      1000
#define LED_TIME_DISCONNECT     2000

#define TIMEOUT_RECONNECT       60000
#define TIMEOUT_CHART           900000
#define TIMEOUT_UPDATE          1000
#define TIMEOUT_SENSOR          100
typedef struct{
    uint32_t led;
    uint32_t connection;
    uint32_t chart;
    uint32_t update;
    uint32_t update_sensor;
}TIMEOUT_TypeDef;
TIMEOUT_TypeDef timeout;

typedef struct{
    uint8_t sensor;
    uint8_t schedule;
}INDEX_TypeDef;
INDEX_TypeDef counter;

typedef struct{
    uint8_t hour;
    uint8_t minute;
}TIME_TypeDef;

#define EEPROM_ADDRESS			0x57 	//defines the base address of the EEPROM

typedef struct{
    uint8_t number;
    TIME_TypeDef on;
    TIME_TypeDef off;
}SCHEDULE_TypeDef;

#define BATTERY_ENERGY      87.5
#define BATTERY_FULL        13.65
typedef struct{
    float v_bat;
    float i_bat;
    float p_bat;
    float e_bat;
    float v_panel;
    float i_panel;
    float i_load;
    float p_load;
    float e_load;
    TIME_TypeDef time;
}DATA_TypeDef;
DATA_TypeDef data;

void setup(){
    delay(3000);
    Serial.begin(115200);
    Wire.begin();

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

    data.e_bat = BATTERY_ENERGY / 2;
    Serial.println("Init");

    client.setBufferSize(1024);
}

void loop(){
    if(WiFi.isConnected()){
        if(status.connection){  toggleLed(LED_TIME_MQTT);    }
        else {                  toggleLed(LED_TIME_CONNECTED);  }

        checkChart();

        status.connection = client.loop();
        if(status.connection){
            if(status.chart){
                publishChart();
            }
            else{
                processData();
            }            
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
        data.time.hour = now.hour();
        data.time.minute = now.minute();

        // Serial.print(now.year(), DEC);   Serial.print('/');
        // Serial.print(now.month(), DEC);  Serial.print('/');
        // Serial.print(now.day(), DEC);
        // Serial.print(" (");
        // Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
        // Serial.print(") ");
        // Serial.print(now.hour(), DEC);   Serial.print(':');
        // Serial.print(now.minute(), DEC); Serial.print(':');
        // Serial.print(now.second(), DEC); Serial.println();

        float energy = data.p_bat / 3600;
        data.e_bat += energy;
        if(data.e_bat > BATTERY_ENERGY) data.e_bat = BATTERY_ENERGY;

        energy = data.p_load / 3600;
        data.e_load += energy;

        if(data.time.hour == 0 && data.time.minute == 0){
            data.e_load = 0;
        }
    }
    else{
        if((millis() - timeout.update_sensor) > TIMEOUT_SENSOR){
            float load;
            ads1.begin(21,22);   ads1.setDataRate(7);

            counter.sensor++;
            switch(counter.sensor){
                case 1: data.v_bat = vBatt(); 
                        if(data.v_bat > BATTERY_FULL){
                            data.e_bat = BATTERY_ENERGY;
                        }
                break;
                case 2: data.v_panel = vPanel(); break;
                case 3: data.i_bat = iBatt(); break;
                case 4: data.i_panel = iPanel(); break;
                case 5: load = data.i_panel - data.i_bat;
                        if(load >= 0){
                            data.i_load = load; 
                            data.p_load = data.v_bat * data.i_load;
                        }

                        data.p_bat = data.v_bat * data.i_bat;
                break;
                default : counter.sensor = 0; break;
            }

            counter.schedule ++;
            if(counter.schedule > 24){
               counter.schedule = 1;
            }
            SCHEDULE_TypeDef schedule = readSchedule(counter.schedule);
            if(schedule.number > 0){
                if(schedule.on.hour == data.time.hour && schedule.on.minute == data.time.minute){
                    pompaOn();
                }
                else if(schedule.off.hour == data.time.hour && schedule.off.minute == data.time.minute){
                    pompaOff();
                }
            }

            timeout.update_sensor = millis();
        }
    }
}

void processData(){
    if(status.mqtt){
        Serial.println(mqtt_payload);
        uint8_t pos[100];
		    uint8_t pos_total;

        pos_total = findChar(mqtt_payload, "^", pos);

        if(pos_total){
            String split = mqtt_payload.substring(0, pos[0]);

            if(split == "GET"){
                split = mqtt_payload.substring(pos[0] + 1);

                if(split == "DATA"){
                    publishData();
                }
                else if(split == "SCHEDULE"){
                    publishSchedule();
                }
            }
            else if(split == "SET"){
                split = mqtt_payload.substring(pos[0] + 1, pos[1]);
                if(split == "RELAY"){
                    split = mqtt_payload.substring(pos[1] + 1);
                    if(split == "1"){
                        pompaOn();
                    }
                    else{
                        pompaOff();
                    }

                    publishData();
                }
                // SET^TIME^2024^04^20^08^30^00
                else if(split == "TIME"){
                    if(pos_total > 6){
                        long year = mqtt_payload.substring(pos[1] + 1, pos[2]).toInt();
                        long month = mqtt_payload.substring(pos[2] + 1, pos[3]).toInt();
                        long date = mqtt_payload.substring(pos[3] + 1, pos[4]).toInt();
                        long hour = mqtt_payload.substring(pos[4] + 1, pos[5]).toInt();
                        long minute = mqtt_payload.substring(pos[5] + 1, pos[6]).toInt();
                        long second = mqtt_payload.substring(pos[6] + 1).toInt();

                        rtc.adjust(DateTime(year, month, date, hour, minute, second));

                        DateTime now = rtc.now();
                        data.time.hour = now.hour();
                        data.time.minute = now.minute();

                        publishData();
                    }
                }
                // SET^SCHEDULE^10^04^20^08^30
                else if(split == "SCHEDULE"){
                    if(pos_total > 5){
                        long number = mqtt_payload.substring(pos[1] + 1, pos[2]).toInt();
                        long on_hour = mqtt_payload.substring(pos[2] + 1, pos[3]).toInt();
                        long on_minute = mqtt_payload.substring(pos[3] + 1, pos[4]).toInt();
                        long off_hour = mqtt_payload.substring(pos[4] + 1, pos[5]).toInt();
                        long off_minute = mqtt_payload.substring(pos[5] + 1).toInt();

                        SCHEDULE_TypeDef sch;
                        sch.number = number;
                        sch.on.hour = on_hour;
                        sch.on.minute = on_minute;
                        sch.off.hour = off_hour;
                        sch.off.minute = off_minute;

                        writeSchedule(sch);

                        publishSchedule();
                    }
                }
            }
        }

        clearDataMqtt();
        
        status.mqtt = false;
    }
}

void publishData(){
    /* DATA^hour^minute^vbat^ibat^vpanel^ipanel^iload^pload^pompa*/
    sprintf(text, "DATA^%d^%d^%0.2f^%0.2f^%0.2f^%0.2f^%0.2f^%0.2f^%0.2f^%d", 
                data.time.hour, data.time.minute, data.v_bat, data.i_bat, data.e_bat, data.v_panel, data.i_panel, data.p_load, data.e_load, status.pompa);

    mqttPublish(text);
}

void publishSchedule(){
    sprintf(text, "SCHEDULE");
    for(uint8_t i=1; i<= 24; i++){
        SCHEDULE_TypeDef sch = readSchedule(i);
        if(sch.number){
            sprintf(&text[strlen(text)], "^%d|%d|%d|%d|%d", sch.number, sch.on.hour, sch.on.minute, sch.off.hour, sch.off.minute);
        }
    }

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
    sprintf(text,"field1=%.2f&field2=%.2f&field3=%.2f&field4=%.2f&field5=%.2f&field6=%.2f&field7=%d&field8=%.2f&status=MQTTPUBLISH",  
            data.v_bat, data.i_bat, data.e_bat,
            data.v_panel, data.i_panel,
            data.e_load, status.pompa, data.p_load);
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

/***** Schedule Storage *****/
SCHEDULE_TypeDef readSchedule(uint8_t number){
    SCHEDULE_TypeDef sch;
    sch.number = number;
    if(sch.number == 0){
        return sch;
    }

    uint16_t addr = (number - 1) * 4;

    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write(highByte(addr));
    Wire.write(lowByte(addr));
    Wire.endTransmission();

    Wire.requestFrom(EEPROM_ADDRESS, 4);

    sch.on.hour = Wire.read();
    sch.on.minute = Wire.read();
    sch.off.hour = Wire.read();
    sch.off.minute = Wire.read();

    if( sch.on.hour > 23 || sch.on.minute > 59 || 
        sch.off.hour > 23 || sch.off.minute > 59
    ){
        sch.number = 0;
    }

    return sch;
}

void writeSchedule(SCHEDULE_TypeDef sch){
    if(sch.number == 0){
        return;
    }

    Serial.println(sch.number);
    uint16_t addr = (sch.number - 1) * 4;

    for(uint8_t i=0; i<4; i++){
        Wire.beginTransmission(EEPROM_ADDRESS);
        Wire.write(highByte(addr + i));
        Wire.write(lowByte(addr + i));
        
        Serial.println(addr + i);

        switch(i){
            case 0: Wire.write(sch.on.hour); break;
            case 1: Wire.write(sch.on.minute);break;
            case 2: Wire.write(sch.off.hour); break;
            case 3: Wire.write(sch.off.minute); break;
        }

        Wire.endTransmission();

        delay(10);
    }
}

/***** Voltage And Current Sensor ****/
float vBatt(){
    float vBat = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(2);     // GAIN 2.048

        int16_t raw = ads1.readADC(1);
        vBat = raw * ads1.toVoltage(1) * GAIN_V_BAT;

        if(vBat < 0){   vBat = 0;   }
    }
    else{
        Serial.println("Not Connect");
    }
    return vBat;
}

float iBatt(){
    float iLoad = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(16);    // GAIN 0.254

        int16_t raw = ads1.readADC(2);
        iLoad = (raw * ads1.toVoltage(1)) / GAIN_I_BATT;

        if(-0.01 < iLoad && iLoad < 0.01){
            iLoad = 0.0;
        }
    }
    else{
        Serial.println("Not Connect");
    }
    return iLoad;
}

float vPanel(){
    float vPanel = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(2);     // GAIN 2.048

        int16_t raw = ads1.readADC(0);
        vPanel = data.v_bat - (raw * ads1.toVoltage(1) * GAIN_V_PANEL);

        if(vPanel < 0){   vPanel = 0;   }
    }
    else{
        Serial.println("Not Connect");
    }
    return vPanel;
}

float iPanel(){
    float iPanel = 0.0;

    if(ads1.isConnected()){
        ads1.setGain(16);    // GAIN 0.254

        int16_t raw = ads1.readADC(3);
        iPanel = -1 * (raw * ads1.toVoltage(1)) / GAIN_I_PANEL;

        if(-0.01 < iPanel && iPanel < 0.01){
            iPanel = 0.0;
        }
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

uint8_t findChar(String data, char *c, uint8_t *pos){
	uint8_t index = 0;
	for(uint16_t i=0; i<data.length(); i++){
		for(uint16_t x=0; x<strlen(c); x++){
			if(data[i] == c[x]){
				pos[index] = i;
				index++;
			}
		}
	}

	return index;
}

String charToString(char * data, int start, int end){
    String buff = "";
    for(int n=start; n<=end; n++){
        buff += data[n];
    }

    return buff;
}