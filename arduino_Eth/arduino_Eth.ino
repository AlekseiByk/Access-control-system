#include <SPI.h>
#include <Ethernet3.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <utility/w5500.h>
//--------------------------------------------------assign pins
#define BUTTON      (3)
#define BLUE        (4)
#define GREEN       (5)
#define RED         (6)
#define PN532_IRQ   (7)
#define DOOR        (8)
#define FIRE        (9)
//--------------------------------------------------

#define LOGGING_SERIAL

#ifdef LOGGING_SERIAL
#define LOG_SERIAL(x) Serial.println(x)
#else
#define LOG_SERIAL(x)
#endif


static byte MAC[] = {0xe0,0xd0,0xa0,0xed,0xa0,0x03};     // ethernet interface mac address

IPAddress server(192, 168, 0, 101);  // IP for server
#define   port 8008                  // Port of the server
IPAddress ip(192, 168, 0, 100);      // IP for static config

EthernetClient client;


Adafruit_PN532 nfc(PN532_IRQ, 1);                   // assign name to the module

uint32_t admin_keys[10] = {};
int admin_number = 0;

static uint32_t available_time   = 0;
static uint32_t timer            = 0;
static uint32_t timer_blink      = 0;
static uint32_t timer_blink_x    = 0;
static uint32_t timer_door_open  = 0;
static uint32_t timer_connection = 0;
static uint32_t timer_pr         = 0;

static uint32_t door_open_timer  = 10000;
static uint32_t open_admin_card  = (uint32_t) 4*60*60*1000;

static boolean green             = false;
static boolean red               = false;
static boolean light             = false;
static boolean connection        = false;    
static boolean open_av           = false;                                                                                                                                             

void        (* resetFunc)       (void) = 0;                         //reset function
static void check_callback      (byte status, word off, word len);  //callback to card id messege
static void button_pressed      ();                                 //callback to button press event    
void        door_open           (boolean access);                   //open door function    
static void door_check          ();                                 //cehcking door status
static void blinking_function   ();                                 //control light diods
void        sendLogMsg          (char* text);                       //Send Log messeges function
void        initializeNick      ();
static void ack_callback        (byte status, word off, word len);  //callback to ack from logging msgs

    
void setup() {
    #ifdef LOGGING_SERIAL
    Serial.begin(9600);
    #endif
    
  
//--------------------------------------------------------------инициализация пинов
    pinMode(GREEN, OUTPUT);                                                                 //set pin for diods
    pinMode(BLUE, OUTPUT);  
    pinMode(RED, OUTPUT);
    pinMode(BUTTON, INPUT);
    pinMode(DOOR, OUTPUT);
    pinMode(FIRE, INPUT);

    digitalWrite(RED, HIGH);


//--------------------------------------------------------------инициализация Ethernet
    if (Ethernet.begin(MAC) == 0){
        LOG_SERIAL(F("Failed to access Ethernet controller"));
        Ethernet.begin(MAC, ip);
    }

    Ethernet.softreset();
    delay(200);

    if (Ethernet.begin(MAC) == 0){
        LOG_SERIAL(F("Failed to access Ethernet controller"));
        Ethernet.begin(MAC, ip);
    }
    
    LOG_SERIAL("connecting...");
    delay(200);
    if (client.connect(server, port)) {
        LOG_SERIAL("connected");
    }
    else {
        // kf you didn't get a connection to the server:
        delay(1000);
        resetFunc();
    }
  
//--------------------------------------------------------------инициализация считывателя
  
    
    nfc.begin();                                                                                  // инициируем работу с модулем
    static uint32_t versiondata = nfc.getFirmwareVersion();                                              // считываем версию прошивки модуля в переменную
    if (! versiondata) {
        LOG_SERIAL("Didn't find PN53x board");
        client.stop();
        delay(1000);
        resetFunc();
    }
    LOG_SERIAL("Found chip PN5");              // если версия прочитана, то выводим текст и версию чипа в монитор портa
    nfc.setPassiveActivationRetries(0xFF);                                                        // указываем количество попыток на считывание карты
    nfc.SAMConfig();                                                                              // настраиваем модуль на чтение RFID-меток

  
//---------------------------------------------------------------

    digitalWrite(DOOR, HIGH);
    //attachInterrupt(digitalPinToInterrupt(BUTTON), button_pressed, RISING);

    connection = true;

    //***********************************
    sendLogMsg("initialized");
    //***********************************
    
}


void loop() {

    static boolean success = false;                                                                              // задаём переменную для считывания номера карты
    static uint8_t uid_temp[] = { 0, 0, 0, 0, 0, 0, 0, 0};                                                           // задаём переменную для хранения номера считанной карты
    static uint8_t uidLength;
    static char str[48] = "";
    static boolean door = false;
    if (millis() > timer && !green)
        success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid_temp[0], &uidLength, 100);
    if ((millis() > timer) && success && uidLength == 4) {
        success = false;
        timer = millis()  + 2000;

        for (int i = 0; i < admin_number; i++)
            if (*((uint32_t*)&uid_temp[0]) == admin_keys[i]){
                door_open(true, door_open_timer);
                available_time = millis() + (uint32_t)open_admin_card; 
                door = true;
                //*****************************************************************************************
                sprintf(str, "open/%02x%02x%02x%02x", uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
                sendLogMsg(str);
                //*****************************************************************************************
                continue; 
            }
      
        if ((door == false) && client.connected())
        {
            sprintf(str, "/request/%02x%02x%02x%02x%02x%02x/%02x%02x%02x%02x", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5], uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
            LOG_SERIAL(str);
            connection = false;
            timer_connection = millis() + 1000;
            client.print(str);
            while (client.connected() && !client.available() && (millis() < timer_connection)){}
        }
        door = false;
    }

    if (client.available()) {        
        static char answer[128] = "";
         
        int i = 0;
        for (; client.available(); i++){
            answer[i] = client.read();
        }
        answer[i] = 0;
        
        
        static char* index;
        LOG_SERIAL( answer );
        
        if (index = strstr( answer, "reply")){
            door_open((char)(*(index + 6)) == '1', door_open_timer);
            available_time = millis() + (uint32_t)atol(index + 8);  
        }
        else if (index = strstr( answer, "open")){
            door_open((char)(*(index + 5)) == '1', atol(index + 7));
            open_av = false;
        }
        else if (index = strstr( answer, "scan")){
            LOG_SERIAL ("scan");
            digitalWrite(BLUE, HIGH);
            digitalWrite(RED, HIGH);
            digitalWrite(GREEN, LOW);
            success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid_temp[0], &uidLength, atol(index + 5));
            if (success){
                success = false;
                sprintf(str, "/scanned/%02x%02x%02x%02x%02x%02x/%02x%02x%02x%02x", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5], uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
                LOG_SERIAL(str);
                connection = false;
                timer_connection = millis() + 1000;
                timer = millis()  + 1000;
                client.print(str);  
            }
            else{
                sprintf(str, "/scanned/%02x%02x%02x%02x%02x%02x/00000000", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);
                LOG_SERIAL(str);
                client.print(str);
            }
        }
        else if (index = strstr( answer, "update")){          //load new uid cards
            index += 6;
            
            for (int j = 0; j < 10 && *index; j++){
                index++;
                if (*index == 'a'){
                    index += 2;
                    uint8_t temp;
                    for (int i = 0; i < 4; i++){
                        char cur_char = *(index + i*2);
                        if (cur_char >= '0' && cur_char <= '9'){
                            temp = cur_char - '0';
                        }
                        else{
                            temp = cur_char - 'a' + 10;
                        }
                    
                        temp = temp << 4;
                        cur_char = *(index + i*2 + 1);
                    
                        if (cur_char >= '0' && cur_char <= '9'){
                            temp += cur_char - '0';
                        }
                        else{
                            temp += cur_char - 'a' + 10;
                        }
                        uid_temp[abs(i - 3)] = temp;
                    }
                    index += 8;
        
                    admin_keys[j] = *((uint32_t*)&uid_temp[0]);
                    admin_number = j + 1;
                }
                else if (*index == 'b'){
                    index += 2;
                    door_open_timer  = atol(index);
                    LOG_SERIAL(door_open_timer);
                    while (isDigit(*index)) index++;
                    
                }
                else if (*index == 'c'){
                    index += 2;
                    open_admin_card = atol(index);
                    LOG_SERIAL(open_admin_card);
                    while (isDigit(*index)) index++;
                }
            }
        }    
    }

    if (!client.connected()) {
        LOG_SERIAL("disconnecting.");
        client.stop();

        delay(100);
        
        //Ethernet.softreset();
        delay(500);
        //Ethernet.begin(MAC);

        if (client.connect(server, port)) {
            LOG_SERIAL("connected");
            sendLogMsg("initialized");
        }
    }
    
    blinking_function ();
    door_check ();

    if (millis()> (uint32_t)1000*60*60*24*7*3)                                  //reset 1 time per week
        resetFunc();

    if (digitalRead(FIRE)){
        //***********************************
        sendLogMsg("open/fire");
        //***********************************
    }
    //------------------------------------------------------------------------------------------------------------------button processing
    
    if (digitalRead(BUTTON) && ((millis() < timer_door_open - 600 || millis() > timer_door_open + 600) || open_av)){
        LOG_SERIAL("The sensor is touched");
        timer = millis()  + 2000;
        
        if (open_av)
            door_open(true, 2000);
        else
            door_open(true, door_open_timer);
            
        digitalWrite(GREEN, HIGH);
        digitalWrite(BLUE, LOW);
        digitalWrite(RED, LOW);
        
        //***********************************
        sendLogMsg("open/button");
        //***********************************
        timer_pr = millis() + 2000;
        
        while (millis() < timer_pr && digitalRead(BUTTON)){}
        if (!digitalRead(BUTTON))
            open_av = false;
        else{
            open_av = true;
            
            digitalWrite(GREEN, HIGH);
            digitalWrite(BLUE, HIGH);
            digitalWrite(RED, HIGH);
            
            //***********************************
            sendLogMsg("open/available");
            //***********************************
            timer_door_open = available_time;
        }
        while (digitalRead(BUTTON)){}
    }
    //************************************************************************************************************************************
}

void door_open (boolean access, uint32_t open_time) {
    if (access){
        green = true;
        digitalWrite(DOOR, LOW);
        timer_door_open = millis() + open_time;
        
    }
    else{
        red = true;
        timer_door_open = millis() + 2000;
    }
}


void door_check () {
    if ((millis() > timer_door_open) && green){
        green = false;
        digitalWrite(DOOR, HIGH);
        //***********************************
        sendLogMsg("close/timer");
        //***********************************
    }
    if ((millis() > timer_door_open) && red){
        red = false;
        open_av = false;
    }
}
  

void blinking_function () {
    if ((millis() > timer_blink_x) && green && open_av){
        timer_blink_x = millis() + 1000;
        light = !light;

        if (light){
            digitalWrite(GREEN, LOW);
            digitalWrite(RED, LOW);
            digitalWrite(BLUE, LOW);
        }
        else{
            digitalWrite(GREEN, HIGH);
            digitalWrite(BLUE, HIGH);
            digitalWrite(RED, HIGH);
        }
    }
    else if ((millis() > timer_blink_x) && green){
        timer_blink_x = millis() + 200;
        light = !light;

        digitalWrite(BLUE, LOW);
        digitalWrite(RED, LOW);

        if (light)
            digitalWrite(GREEN, LOW);
        else
            digitalWrite(GREEN, HIGH);
    }
    else if ((millis() > timer_blink_x) && red){
        timer_blink_x = millis() + 300;
        light = !light;

        digitalWrite(GREEN, LOW);
        digitalWrite(BLUE, LOW);

        if (light)
            digitalWrite(RED, LOW);
        else
            digitalWrite(RED, HIGH);
    }
    else if ((millis() > timer_blink) && !green && !red){
        timer_blink = millis() + 1000;
        light = !light;

        digitalWrite(GREEN, LOW);
        digitalWrite(RED, LOW);

        if (light)
            digitalWrite(BLUE, LOW);
        else
            digitalWrite(BLUE, HIGH);
    }
}

void sendLogMsg (char* text){                   //logging format: /log/Nickname/info
    static char str[64] = "";
    int bytes = sprintf(str, "/log/%02x%02x%02x%02x%02x%02x/%s", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5], text);
    LOG_SERIAL(str);
    client.print(str);
}
