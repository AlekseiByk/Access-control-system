#include <SPI.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
//--------------------------------------------------assign pins
#define BUTTON      (3)
#define BLUE        (4)
#define GREEN       (5)
#define RED         (6)
#define PN532_IRQ   (7)
#define DOOR        (8)
#define FIRE        (9)
//--------------------------------------------------
#define DOOR_OPEN_TIMER 15000

#define LOGGING_SERIAL

#ifdef LOGGING_SERIAL
#define LOG_SERIAL(x) Serial.println(x)
#else
#define LOG_SERIAL(x)
#endif

Adafruit_PN532 nfc(PN532_IRQ, 1);

uint32_t program_card = 0x1ddea07a;                 //!! serial number of master card
//--------------------------------------
static boolean admin_keys[24] = {};
static int admin_number_max = 24;
//--------------------------------------
static uint32_t available_time   = 0;
static uint32_t timer            = 0;
static uint32_t timer_blink      = 0;
static uint32_t timer_blink_x    = 0;
static uint32_t timer_door_open  = 0;
static uint32_t timer_pr         = 0;

static boolean green             = false;
static boolean red               = false;
static boolean light             = false;
static boolean connection        = false;    
static boolean open_av           = false;                                                                                                                                             

void        (* resetFunc)       (void) = 0;                         //reset function
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


//--------------------------------------------------------------инициализация считывателя
  
    
    nfc.begin();                                                                                  // инициируем работу с модулем
    static uint32_t versiondata = nfc.getFirmwareVersion();                                              // считываем версию прошивки модуля в переменную
    if (! versiondata) {
        LOG_SERIAL("Didn't find PN53x board");
        delay(1000);
        resetFunc();
    }
    LOG_SERIAL("Found chip PN5");              // если версия прочитана, то выводим текст и версию чипа в монитор портa
    nfc.setPassiveActivationRetries(0xFF);                                                        // указываем количество попыток на считывание карты
    nfc.SAMConfig();                                                                              // настраиваем модуль на чтение RFID-меток

  
//---------------------------------------------------------------

    digitalWrite(DOOR, HIGH);
    //attachInterrupt(digitalPinToInterrupt(BUTTON), button_pressed, RISING);
    EEPROM.get(0, admin_keys);
    
    #ifdef LOGGING_SERIAL
    char str[48] = "";
    uint8_t uid_temp[] = { 0, 0, 0, 0, 0, 0, 0, 0};      
    for (int i = 0; i < admin_number_max - 1; i++){
      Serial.print(admin_keys[i]);
    }
    Serial.println(admin_keys[admin_number_max - 1]);
    for (int i = 0; i < admin_number_max - 1; i++){
        if (admin_keys[i]==true){
            sprintf(str, "saved %d: %02x%02x%02x%02x", i, uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
            LOG_SERIAL(str);
        }
    }
    #endif
}


void loop() {

    static boolean success = false;                                                                              // задаём переменную для считывания номера карты
    static uint8_t uid_temp[] = { 0, 0, 0, 0, 0, 0, 0, 0};                                                           // задаём переменную для хранения номера считанной карты
    static uint8_t uidLength;
    static char str[48] = "";
    static boolean door = false;
    static uint32_t temp = 0;
    if (millis() > timer && !green)
        success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid_temp[0], &uidLength, 100);
    if ((millis() > timer) && success && uidLength == 4) {
        success = false;
        
        sprintf(str, "scanned: %02x%02x%02x%02x", uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
        LOG_SERIAL(str);

        if (*((uint32_t*)&uid_temp[0]) == program_card){
          
            digitalWrite(GREEN, LOW);
            digitalWrite(BLUE, HIGH);
            digitalWrite(RED, HIGH);
            do {
                success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid_temp[0], &uidLength, 4000);
            }while (*((uint32_t*)&uid_temp[0]) == program_card && success);
            do {                
                if (success && *((uint32_t*)&uid_temp[0]) != program_card && temp != *((uint32_t*)&uid_temp[0])){
                    sprintf(str, "scanned: %02x%02x%02x%02x", uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
                    LOG_SERIAL(str);
                    for (int i = 0; i < admin_number_max; i++){
                        EEPROM.get(admin_number_max + 32*i, temp);
                        if (true == admin_keys[i] && (temp == *((uint32_t*)&uid_temp[0]))){
                            sprintf(str, "Errase: %02x%02x%02x%02x", uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
                            LOG_SERIAL(str);
                            admin_keys[i] = false;
                            EEPROM.put(0, admin_keys);
                            door = true;
                            
                            digitalWrite(GREEN, LOW);
                            digitalWrite(BLUE, LOW);
                            digitalWrite(RED, HIGH);
                            delay(500);
                            
                            break;
                        }
                    }
                    if (door == false)
                        for (int i = 0; i < admin_number_max; i++)
                            if (false == admin_keys[i]){
                                sprintf(str, "Add: %02x%02x%02x%02x", uid_temp[3], uid_temp[2], uid_temp[1], uid_temp[0]);
                                LOG_SERIAL(str);
                                admin_keys[i] = true;
                                EEPROM.put(0, admin_keys);
                                EEPROM.put(admin_number_max + i*32, *((uint32_t*)&uid_temp[0]));

                                digitalWrite(GREEN, HIGH);
                                digitalWrite(BLUE, LOW);
                                digitalWrite(RED, LOW);
                                delay(500);
                                
                                break; 
                            }
                    door = false;
                } 
                
                digitalWrite(GREEN, LOW);
                digitalWrite(BLUE, HIGH);
                digitalWrite(RED, HIGH);
                
                temp = *((uint32_t*)&uid_temp[0]);
                success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid_temp[0], &uidLength, 20000);
            }while (*((uint32_t*)&uid_temp[0]) != program_card && success);
        }
        else{ 
            for (int i = 0; i < admin_number_max; i++){
                EEPROM.get(admin_number_max + 32*i, temp);
                if (true == admin_keys[i] && (temp == *((uint32_t*)&uid_temp[0]))){
                    door_open(true, DOOR_OPEN_TIMER);
                    available_time = millis() + (uint32_t)60 * 60 * 1000; 
                    door = true;
                    continue; 
                }
            }
      
            if (door == false)
                door_open(false, DOOR_OPEN_TIMER);
            door = false;
        }
        timer = millis()  + 2000;
    }

    blinking_function ();
    door_check ();

    if (millis()> (uint32_t)1000*60*60*24*7*3)                                  //reset 1 time per week
        resetFunc();

    if (digitalRead(FIRE)){}
    //------------------------------------------------------------------------------------------------------------------button processing
    
    if (digitalRead(BUTTON) && ((millis() < timer_door_open - 600 || millis() > timer_door_open + 600) || open_av)){
        LOG_SERIAL("The sensor is touched");
        timer = millis()  + 2000;
        
        if (open_av)
            door_open(true, 2000);
        else
            door_open(true, DOOR_OPEN_TIMER);
            
        digitalWrite(GREEN, HIGH);
        digitalWrite(BLUE, LOW);
        digitalWrite(RED, LOW);
        
        timer_pr = millis() + 2000;
        
        while (millis() < timer_pr && digitalRead(BUTTON)){}
        if (!digitalRead(BUTTON))
            open_av = false;
        else{
            open_av = true;
            
            digitalWrite(GREEN, HIGH);
            digitalWrite(BLUE, HIGH);
            digitalWrite(RED, HIGH);
            
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
    }
    if ((millis() > timer_door_open) && red){
        red = false;
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
