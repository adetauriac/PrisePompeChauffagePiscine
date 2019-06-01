/***********************************************************************************************

  Pilotage d'une prise pour pompe chauffe haut piscine de type ROOS Solor Control PROFI
       Remplacement de l'electronique par un Arduino Nano



************************************************************************************************
*/
/*LIBRAIRIE OneWire
https://github.com/PaulStoffregen/OneWire.git

LIBRAIRIE DallasTemperature
https://github.com/milesburton/Arduino-Temperature-Control-Library

Librairie Adafruit 
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


CONNEXIONS 1wire
fil rouge    +5V
fil noir     GND
fil jaune    données OneWire 
Résistance pull-up de 4.7 kΩ entre le signal (fil jaune) et +5V (fil rouge)
Sonde Temperature en mode parasite pour garder le cable 2fils d'origine de la prise

Connexion Ecran OLED I2C
SCL ==> A4
SDA ==> A5 


MICROCONTRÔLEUR
Clone Arduino Nano

REMARQUES
- Le protocole OneWire est lent, il faut environ 780 ms pour une lecture.
- Le capteur DS18B20 n’est pas très réactif, il faut environ 5 min
  pour qu’il se stabilise.

*/

//1wire
#include <OneWire.h>
#include <DallasTemperature.h>
//OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Le fil des données est connectés à la broche 2 (D2)
#define ONE_WIRE_BUS 9

// Initialisation d’une instance pour communiquer avec le protocole OneWire
OneWire oneWire( ONE_WIRE_BUS );

// Initialise DallasTemperature avec la référence à OneWire.
DallasTemperature sensors( &oneWire );

//Initialisation relais signal sur le pin 12
int relais  = 12;

//Initialisation LEDs
const int Lrouge = 8;  // D8 Indication Temperature Exterieur (Te) consigne atteint ou pas
const int Lorange = 7; // D7 indication si process en mode forcé activé ou pas (par interrupteur ou logiciel)
const int Lverte = 6;  // D6 Indication Temperature Pompe (Tp) consigne atteint ou pas

//Initi OLED
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

//Init Selecteur
//Encoder rotatif Pin definition Attention D3 est necessaire pour l'intteruption 
const int PinA = 3;  // Used for generating interrupts using CLK signal
const int PinB = 4;  //Used for reading DT signal
const int PinSW = 10;  // Used for the push button switch

// Updated by the ISR (Interrupt Service Routine)
volatile int virtualPosition = 1000;
volatile int lastPosition = 1000;



//Initialisation des temps de consigne
float Tec = 26; //Temperature externe minimum pour chauffer la piscine
float Tecm = 30; //Temperature externe maximum pour chauffer le piscine (a utilisé avec la température de la piscine ==> Non present)
float Tpc = 24; // Temperature minimume a envoyer dans la piscine

//Definir une hysteresis
int hysteresis = 1;

// Variable diverse
bool bTec = false; // Consigne Temp externe validée : True/False
bool bTpc = false; // Consigne Temp Pompre validée : True/False
bool bDemo = true; // Activer le defilement des informations à l'écran 

float Te; // Variable pour recuperer temperature Exterieur
float Tp; // Variable pour recuperer temperature Pompe
float TePrev; // Variable pour stocker la dernière valeur
float TpPrev; // Variable pour stocker la dernière valeur

int Screen =1;
int posMenu = 1;


#define timeswitchScreen 2000  //Change Screen after x seconde en auto 
#define timeswitchOffScreen 20000  //SwitchOff Screen after x seconde en auto 
#define timeGetTemp 5000  //Prendre temperature toute les X seconde
#define NBSCREEN 2  //Nombre d'écran 
#define NumberMenu 2 // Nombre de Menu 1er niveau


unsigned long  currentTime;
unsigned long  StarTimeScreen = 0;
unsigned long  TimeScreen = 0;
unsigned long  StarTimeLight = 0;
unsigned long LastTimeGetTemp = 0;
//unsigned long lastmillis = 0;


char* myStrings[]={"Pompe Consigne", "Exterieur Consigne"};


// ------------------------------------------------------------------
// INTERRUPT     INTERRUPT     INTERRUPT     INTERRUPT     INTERRUPT
// ------------------------------------------------------------------
void isr ()  {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  // If interrupts come faster than 5ms, assume it's a bounce and ignore
  if (interruptTime - lastInterruptTime > 5) {
    if (digitalRead(PinB) == LOW)
    {
      virtualPosition-- ; // Could be -5 or -10
    }
    else {
      virtualPosition++ ; // Could be +5 or +10
    }

    // Restrict value from 0 to +2000
    virtualPosition = min(2000, max(0, virtualPosition));

    // Keep track of when we were here last (no more than every 5ms)
    lastInterruptTime = interruptTime;
  }
}



//*************************************************************************************************
void setup() {
  Serial.begin( 115200 );
  Serial.print( "Demo prise pompe v1\n" );

  // Démarre le processus de lecture.
  // IC Default 9 bit. If you have troubles consider upping it 12.
  // Ups the delay giving the IC more time to process the temperature
  // measurement
  sensors.begin();
  
  //Config du RelaisP
  digitalWrite(relais, OUTPUT); // module relais .signal sur la broche 12
  
 //Config LEDs
  pinMode(Lrouge, OUTPUT); //Lrouge est une broche de sortie
  pinMode(Lorange, OUTPUT); //Lorange est une broche de sortie
  pinMode(Lverte, OUTPUT); //Lverte est une broche de sortie

    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  //Utiliser le scanner I2C pour trouver le port série sur lequel se trouve votre écran 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64) 
  // init done
  display.clearDisplay();   // clears the screen and buffer   // Efface l'écran 

  StarTimeScreen = millis(); //Variable to change screen after x Seconde
  LastTimeGetTemp = millis();
  

 // Rotary pulses are INPUTs
  pinMode(PinA, INPUT_PULLUP);
  pinMode(PinB, INPUT_PULLUP);
  // Switch is floating so use the in-built PULLUP so we don't need a resistor
  pinMode(PinSW, INPUT_PULLUP);
  // Attach the routine to service the interrupts
  attachInterrupt(digitalPinToInterrupt(PinA), isr, LOW);


  
}

void loop() {


if ( millis() -  LastTimeGetTemp > timeGetTemp ){
  GetTemperature();
  LastTimeGetTemp = millis ();
  
  }

  
  // Allumer les led verte et rouge en fonction des températures de concigne et de température des sondes, en tenant compte d'une hyteresis de 1°)

//Led Rouge (Temp Externe Te)
 if ( Te >= Tec){
  digitalWrite(Lrouge, HIGH);
  bTec=true;
 }
 if (Te < Tec-hysteresis) {
  digitalWrite(Lrouge, LOW);
  bTec=false;
 }

 
//Led Verte (Temp Pompe Tp)
 if ( Tp > Tpc){
  digitalWrite(Lverte, HIGH);
  bTpc=true;
 }
 if (Tp<Tpc-hysteresis) {
  digitalWrite(Lverte, LOW);
  bTpc=false;
 }
  


  



  //Recuperer le mode de fonctionnement 
  //==> To do avec interrupteur et consigne autre ?
  // Mod = 0 Off
  // Mod = 1 Auto
  // Mod = 2 Forcé
  int Mod = 1;
  pinMode(relais, OUTPUT);

  //Condition pour connaitre le mode de fonctionnement 
  if (Mod == 2){ //Mode forcé, on allume le relais ==> Prevoir un delais de xx Minute ou heure
      digitalWrite(relais,HIGH);
      digitalWrite(Lorange, HIGH); //allumer LED Orange pour indiquer mode forcé
  }

  if (Mod == 1){ //Mode auromatique

    if (bTec == true){
      digitalWrite(relais,HIGH);
       Serial.println( "Start Pompe" );
    }
    else {
      digitalWrite(relais,LOW);
      Serial.println( "Stop Pompe" );

      
    }
  }


  navigation() ;
  if (bDemo == true ){ //Lance le defilement des informations
    Demo();
  }
  else { // Passage dans menu config
    Menu();
  
  }




}


void Demo(){
  currentTime = millis();  
  if ( millis() -  StarTimeScreen > timeswitchScreen ){
    Screen=Screen+1 ;
    if (Screen > NBSCREEN )
    {
      Screen = 1;
    }
    StarTimeScreen = millis();
    Serial.print("Screen number :");
    Serial.println (Screen);
  }
  
  switch (Screen) {
    case 1:
      DisplayTempPompe();
    break;
    case 2:
      DisplayTempExterne();
    break; 
  }
}
void GetTemperature(){
    // Requête de toutes les températures disponibles sur le bus
  sensors.requestTemperatures();
  // On ne garde que les deux première température (index = 0 et 1)
  // Requete de lecture des 2 sondes 1wire
  Te = sensors.getTempCByIndex( 0 ); //Temperature Externe
  Tp = sensors.getTempCByIndex( 1 ); //Temperature Pompe

//  //Check bug avec Intteruption lorsque que le selecteur est actionné. Ca fausee le relevé de température. Dans ce cas, on detecte la valeur > 85 au -127 et on remplace la valeur par la valeur precedente
  if (Te >=85 or Te < -120 ) {
    Te=TePrev;
    Serial.println("Bug Temp Te");
  }
  if (Tp >= 84 or Te < -120) {
    Tp=TpPrev; 
    Serial.println("Bug Temp Tp");
  }
  TpPrev=Tp;
  TePrev=Te;
  
  // Afficher pour debbug 
  Serial.print( "T Externe= " );
  Serial.print( Te, 1 );
  Serial.print( " degC" );
   Serial.print( "  " );
  Serial.print( "T Externe Consigne  = " );
  Serial.print( Tec, 1 );
  Serial.print( " degC" );
  Serial.print( "  " );
  Serial.print( "T Pompe = " );
  Serial.print( Tp, 1 );
  Serial.print( " degC" );
  Serial.print( "  " );
  Serial.print( "T Pompe Consigne  = " );
  Serial.print( Tpc, 1 );
  Serial.println( " degC" );
  
}

void DisplayTempPompe(){
  // text display tests
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Temp Pompe");
    //display.setTextColor(BLACK, WHITE); // 'inverted' text
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print("Live     : ");
    display.print( Tp );
    display.println(" C");
    display.print("Consigne : ");
    display.print( Tpc );
    display.println(" C");
    display.display();

}

void DisplayTempExterne(){
    // text display tests
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Temp Externe");
    //display.setTextColor(BLACK, WHITE); // 'inverted' text
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print("Live     : ");
    display.print( Te );
    display.println(" C");
    display.print("Consigne : ");
    display.print( Tec );
    display.println(" C");
    display.display();

}


void navigation() { 
  if ( virtualPosition != lastPosition) {
    StarTimeLight = millis();
    if  (virtualPosition > lastPosition ) {  //menu suivant
      posMenu = (posMenu + 1 ) % NumberMenu ;
    } else if (virtualPosition < lastPosition ) { //menu precedent
      if (posMenu == 0) {
        posMenu = (NumberMenu);
      }
      posMenu = (posMenu - 1) % NumberMenu;
    }
    lastPosition = virtualPosition;
    Serial.print(F("Position menu : "));
    Serial.println(posMenu);
    bDemo=false; //Desactiver le mode defilement pour passer aux modifs de config

  }

}

void Menu(){
    
    switch (posMenu) {
    case 0:
      DisplayTempPompe(); 
      if ((!digitalRead(PinSW))) {
        lastPosition = virtualPosition;
        Tpc=PutConsigne(Tpc, 0);  //0 pour la pompe
       }
  
    break;
    case 1:
      DisplayTempExterne();
      if ((!digitalRead(PinSW))) {
        lastPosition = virtualPosition;
        Tec=PutConsigne(Tec, 1);  //0 pour la temp exterieur
       }
    break; 
  }
  
}

float PutConsigne ( float Consigne, int sonde){
    bool exit_loop = false;
     while (!exit_loop) {
     display.clearDisplay(); //clean display
     display.setTextSize(1);
     display.setCursor(0,0);
     display.println(myStrings[sonde]);
     if (virtualPosition != lastPosition ) {
          if  (virtualPosition < lastPosition ) {  //Augmente la temperature
            Consigne = Consigne+1 ;
          } else if (virtualPosition > lastPosition ) { //Baisse la temperature
            if (Consigne < 11) {
              Consigne = 10;
            }
            Consigne = Consigne -1;
          }
          lastPosition = virtualPosition;
       }
       display.setCursor(10,15);
       display.setTextSize(2);
       display.print(Consigne);
       display.display(); 
       if ((!digitalRead(PinSW))) { //Sortire de la boucle 
        //delay(250);
        exit_loop = !exit_loop;
       }
     
      }
  return Consigne;
}
