// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
#include <Wire.h>
#include "RTClib.h"

//Set actuation pins
const int WaterPumpPin = 2; //LOW pump On
const int WaterValvePin = 3;
const int FoodPumpPin = 4;
const int WaterFlowPin = 5; //Pin connected to main water flow meter, located upstraem of the main valve, counts water going from the main water supply line
const int FoodFlowPin = 6;  //Pin connected to food flow meter, located downstream from the food pump
const int FlowToPotPin = 7; //Pin connected to flow meter upstream from the main pump, counts water going to the pots
const int MuxS0pin = 10;     //Control pin for the Mux controlling the pot valves
const int MuxS1pin = 9;     //Control pin for the Mux controlling the pot valves
const int MuxS2pin = 8;    //Control pin for the Mux controlling the pot valves
const int MuxIOpin = 13;    //Pin setting positions of the valves, this pin is multiplexed throught the mux
const int AirPin = 12;      //Pin controlling air pump
const int BallValveControlPin = 13;

//Set control pins
const int FoodLevelPin = A1; //Pin connected to level switch in the food tank.
const int WaterLevelPin = A0; //Pin connected to level switches in the main water tank. 


int Frequency = 3000; //How often the descision is made, in ms

//Set up Control Timer
RTC_DS1307 ControlTimer;
DateTime Time_Last_Water[8];
DateTime Time_Last_Food[8];
DateTime dtNow;
String strCurrentTime;

//Logical variables for descision making

bool TankFlooded;
bool EnoughWater;       //Is there any water in the food tank?
bool EnoughFood;        //Is there any liquid in the food tank?
bool WaterIsFlowing;    //Is the water flowing through the main line?
bool Emergency = false; //Once this variable is true, the main ball valve closes. 

bool PlantsWantWater[] = {true, true, true, true, true, true, true, true};
bool PlantsWantFood[] = {true, true, true, true, true, true, true, true};
bool TimeToWater[] = {true, true, true, true, true, true, true, true};
bool TimeToFeed[] = {true, true, true, true, true, true, true, true};
long int WateringInterval[] = {5, 5, 5, 5, 5, 5, 5, 5};
long int FeedingInterval[] = {5, 5, 5, 5, 5, 5, 5, 5};
float WaterNeededforPot[] = {5, 5, 5, 5, 5, 5, 5, 5};
float FoodNeededforPot[] = {5, 5, 5, 5, 5, 5, 5, 5};


//Analog variables
int WaterFlow;

//Calibration constants
const int MainWaterClicksPerLiter = 1000;
const int FoodClicksPerLiter = 1000;
const int PotWaterClicksPerLItre = 1000;

void setup () {
  Serial.begin(57600);
  
  //Assign actuation pins and set them high: relay board takes high control to have relay off
  pinMode(WaterPumpPin,OUTPUT);
    digitalWrite(WaterPumpPin, HIGH);   //keep it off by default
  pinMode(WaterValvePin,OUTPUT);
    digitalWrite(WaterValvePin, HIGH);
  pinMode(FoodPumpPin,OUTPUT);
    digitalWrite(FoodPumpPin, HIGH);  
  pinMode(AirPin, OUTPUT);    
    digitalWrite(AirPin, HIGH);  
  pinMode(BallValveControlPin, OUTPUT);    
    digitalWrite(BallValveControlPin, HIGH);  
       
  pinMode(MuxS0pin,OUTPUT); 
  pinMode(MuxS1pin,OUTPUT);
  pinMode(MuxS2pin,OUTPUT);
  pinMode(MuxIOpin,OUTPUT);
  pinMode(AirPin, OUTPUT);
  
  //Assign Control pins
  pinMode(FoodLevelPin,INPUT);
  pinMode(WaterLevelPin,INPUT);
  pinMode(WaterFlowPin,INPUT);
  pinMode(FoodFlowPin, INPUT);
  pinMode(FlowToPotPin, INPUT);

    if (! ControlTimer.begin()) {             //check if ControlTimer is connected                  
    Serial.println("Couldn't find RTC");
    while (1);
  }
    if (! ControlTimer.isrunning()) {         //check if ControlTimer is up                       
    Serial.println("RTC is NOT running!");
   // ControlTimer.adjust(DateTime(2014, 1, 21, 3, 0, 0));   // sets Control Timer to user defiend time
  }
 ControlTimer.adjust(DateTime(F(__DATE__), F(__TIME__)));
 
  dtNow = ControlTimer.now();
  for(int i = 0; i<8; i++)
  {
    Time_Last_Water[i] = dtNow;
    Time_Last_Food[i] = dtNow;
  }
}

void loop () {
  dtNow = ControlTimer.now();                                                        //Get current time from RTC timer board
  Serial.print("Time: ");  DisplayTimeStamp(dtNow); Serial.println();
  
   ReadDataLogger();                                                                //Read Data Logger, set plants want food and water arrays
   CheckWaterLevel();
   CheckFoodLevel();

  //Read and figure out all the variables 
  for (int i = 0; i<8; i++){
   Serial.print(i);
   if (PlantsWantWater[i]){                                                         
     Serial.print(" wants water ");
   }
   else{Serial.print(" does not want water ");
   }

   if (PlantsWantFood[i]){                                                         
     Serial.print(" wants food ");
   }
   else{Serial.print(" does not want food ");
   }

   if (dtNow.unixtime()>=(Time_Last_Water[i].unixtime()+ WateringInterval[i])) {         //Check if already watered today, modify the bool Time to water array         
    TimeToWater[i] = true; 
    Serial.print(" Time to water ");
   }
   else { TimeToWater[i] = false; 
    Serial.print(" was watered today");
   } 
   
   if (dtNow.unixtime()>=(Time_Last_Food[i].unixtime()+ FeedingInterval[i])) {          //Check if already fed today, modify the bool Time to feed array         
    TimeToFeed[i] = true; 
    Serial.println(" Time to feed ");
   }
   else { TimeToFeed[i] = false; 
    Serial.println(" was fed already");
   } 
   
  }

  //For each pot, add individual amount of water and food 
  for (int i = 0; i<8; i++){

    Serial.print("Taking care of pot "); Serial.println(i);
     
    bool Decided_To_Water = TimeToWater[i] && PlantsWantWater[i];
    bool Decided_To_Feed = PlantsWantFood[i] && TimeToFeed[i];

    //Feed if appropriate. Make sure the fertilizer will get pumped to the pot.
    if(  Decided_To_Water  &&  Decided_To_Feed  ){ 
      CheckFoodLevel();                                       
      Serial.print("Adding food to the tank");
      if(AddFoodToTank(FoodNeededforPot[i])){
        Time_Last_Food[i] = dtNow;                                                    //Reset food waiting timer 
        Serial.println("Added food to tank succesfully.");
      }
    }
   
   //Add water to the tank.
   if(Decided_To_Water){
    CheckWaterLevel();
                                      
    Serial.print("Adding water to the tank ");
    if(AddWaterToTank(WaterNeededforPot[i])){
      Serial.println("Added water succesfully.");
    }
    else{Serial.println("Adding water took too much time.");}

    //Mix water with fertilizer if it was added
    if(Decided_To_Feed){
    Serial.println("Mixing the tank");
      digitalWrite(AirPin, LOW);
      delay(1000);
      digitalWrite(AirPin, HIGH);
    }

    //push water (fertilizer) to the pot
    Serial.println("Pushing water to the pot");
    TurnValve(i, "On");
    if(Push_Water_Out(WaterNeededforPot[i]+FoodNeededforPot[i])){
      Time_Last_Water[i] = dtNow;                                                      //Reset water waiting timer        
    }
    TurnValve(i, "Off");
    
    Serial.println("Took care of this pot.");
    Serial.println(" ");
   }
  }

  if (FlowOverPeriod(100, WaterFlowPin) > 2){                                       //Check if the main water valve is closed
    WaterIsFlowing = true;
    Serial.println("ALERT! Main water valve is open!");
  }
  else{WaterIsFlowing = false;
  Serial.println("Main water valve is closed");
  }
  
  if(Emergency){                                                                    //In case of emergency, shut off the ball valve, turn 12V power off. 
    digitalWrite(BallValveControlPin, LOW);
  }
  else{digitalWrite(BallValveControlPin, HIGH);}
  
  delay(Frequency);

}

//Displays dat and Time in a conveninet format
void DisplayTimeStamp(DateTime dt) {
  Serial.print(dt.hour(), DEC);
  Serial.print(":");
  Serial.print(dt.minute(), DEC);
  Serial.print(":");
  Serial.print(dt.second(), DEC);
  return;
}

//Pumps water out of main tank
bool TryToPumpWaterOut() {
  Serial.println("Trying to pump water out...");
  digitalWrite(WaterPumpPin, LOW);
  for (int i = 10; i>0; i--) {Serial.print(i); delay (500);}
  Serial.println();

  if (analogRead(WaterLevelPin)>50) {                                                   //Check if there is still too much water in the tank
    TankFlooded = false;
    return true;
  }
  else { TankFlooded = true;
  Serial.println("EMERGENCY! Can not pump water out of overfilled main tank!");
  return false;
  }
}

int FlowOverPeriod(int period, int SensorPin){
  bool CurrentState = digitalRead(SensorPin);
  bool NewState = CurrentState;
  int StateCounter = 0;
  for(int i = 0; i<period/5; i++){
    
    NewState = digitalRead(SensorPin);
    
    if (NewState != CurrentState) {
       NewState = CurrentState;
       StateCounter++;
    } 
    delay(5);
  }
return StateCounter;
}

//Takes amount of water to pump in liters. Opens the valve to add 
bool Push_Water_Out(float Water) {
  int WaterToUse =  Water*PotWaterClicksPerLItre;
  int WaterUsedSoFar = 0;
  DateTime StartTime = ControlTimer.now();
  DateTime Now = StartTime;
  digitalWrite(WaterPumpPin, LOW);
  while( 
    ((WaterToUse - WaterUsedSoFar)>0)&&
    (Now.unixtime()-StartTime.unixtime() < 4)
       )
  {
    Now = ControlTimer.now();
    if (fmod( Now.unixtime()-StartTime.unixtime(),1)==0 ){
    Serial.print(" ");
    Serial.print(WaterUsedSoFar);
    }
    WaterUsedSoFar = WaterUsedSoFar + FlowOverPeriod(1000, FlowToPotPin);
  }
  
  digitalWrite(WaterPumpPin, HIGH);
  Serial.println();
  
  if(Now.unixtime()-StartTime.unixtime() >= 4){
    Serial.print("Main flow meter error. ");
    return true;
  }
  else{ return true;}
}

//Takes amount of water to pump in liters. Opens the valve to add water to the tank, closes it once the needed amount of water is added.
bool AddWaterToTank(float Water) {
  int WaterToUse =  Water*MainWaterClicksPerLiter;
  int WaterUsedSoFar = 0;
  DateTime StartTime = ControlTimer.now();
  DateTime Now = StartTime;
  digitalWrite(WaterValvePin, LOW);
  while( 
    ((WaterToUse - WaterUsedSoFar)>0)&&
    (Now.unixtime()-StartTime.unixtime() < 4)
       )
  {
    Now = ControlTimer.now();
    if (fmod( Now.unixtime()-StartTime.unixtime(),1)==0 ){
    Serial.print(" ");
    Serial.print(WaterUsedSoFar);
    }
    WaterUsedSoFar = WaterUsedSoFar + FlowOverPeriod(1000, WaterFlowPin);
  }
  
  digitalWrite(WaterValvePin, HIGH);
  Serial.println();
  
  if(Now.unixtime()-StartTime.unixtime() >= 4){
    Serial.print("Main flow meter error. ");
    return true;
  }
  else{ return true;}
}

bool AddFoodToTank(float Food) {
  int FoodToUse =  Food*FoodClicksPerLiter;
  int FoodUsedSoFar = 0;
  DateTime StartTime = ControlTimer.now();
  DateTime Now = StartTime;
  digitalWrite(FoodPumpPin, LOW);
  
  while( 
    ((FoodToUse - FoodUsedSoFar)>0)&&
    (Now.unixtime()-StartTime.unixtime() < 4)
       )
  {
    Now = ControlTimer.now();
    if (fmod( Now.unixtime()-StartTime.unixtime(),1)==0 ){
    Serial.print(" ");
    Serial.print(FoodUsedSoFar);
    }
    FoodUsedSoFar = FoodUsedSoFar + FlowOverPeriod(1000, FoodFlowPin);
  }
  
  digitalWrite(FoodPumpPin, HIGH);
  Serial.println();
  
  if(Now.unixtime()-StartTime.unixtime() >= 4){
    Serial.print("Food flow meter error. ");
    return true;
  }
  else{ return true;}
}

void ReadDataLogger(){
    
}

void TurnValve(int Valve, String State){
  switch(Valve) {
  case 0:
    digitalWrite(MuxS0pin, LOW);
    digitalWrite(MuxS1pin, LOW);
    digitalWrite(MuxS2pin, LOW);
    break;
  case 1:
    digitalWrite(MuxS0pin, HIGH);
    digitalWrite(MuxS1pin, LOW);
    digitalWrite(MuxS2pin, LOW);
    break;
  case 2:
    digitalWrite(MuxS0pin, LOW);
    digitalWrite(MuxS1pin, HIGH);
    digitalWrite(MuxS2pin, LOW);
    break;
  case 3:
    digitalWrite(MuxS0pin, HIGH);
    digitalWrite(MuxS1pin, HIGH);
    digitalWrite(MuxS2pin, LOW);
    break;
  case 4:
    digitalWrite(MuxS0pin, LOW);
    digitalWrite(MuxS1pin, LOW);
    digitalWrite(MuxS2pin, HIGH);
    break;
  case 5:
    digitalWrite(MuxS0pin, HIGH);
    digitalWrite(MuxS1pin, LOW);
    digitalWrite(MuxS2pin, HIGH);
    break; 
  case 6:
    digitalWrite(MuxS0pin, LOW);
    digitalWrite(MuxS1pin, HIGH);
    digitalWrite(MuxS2pin, HIGH);
    break;
  case 7:
    digitalWrite(MuxS0pin, HIGH);
    digitalWrite(MuxS1pin, HIGH);
    digitalWrite(MuxS2pin, HIGH);
    break;        
  default:
    break;
  }
  if(State == "On") {digitalWrite(MuxIOpin, LOW);}
  if(State == "Off"){digitalWrite(MuxIOpin, HIGH);}
  Serial.print("Valve ");
  Serial.print(Valve);
  Serial.print(" is ");
  Serial.println(State);
}

void CheckWaterLevel(){
  if (analogRead(WaterLevelPin)<50) {                                                   //Check if there is too much water in the tank
     TankFlooded = true;
     Serial.println("WATER TANK FLOODED!");
     TankFlooded = !TryToPumpWaterOut();
   }
  if (    (analogRead(WaterLevelPin)>50)   &&    (analogRead(WaterLevelPin)<150)    ){
    TankFlooded = false;
    EnoughWater = true;
    Serial.println("Some water in the tank");
   }
  if (analogRead(WaterLevelPin)>150){
    TankFlooded = false;
    EnoughWater = false;
    Serial.println("Water tank is dry");
   }
  
}

void CheckFoodLevel(){ 
if  (analogRead(FoodLevelPin)>150){
    EnoughFood = false;
    Serial.println("Food tank is dry");
   }
}
