#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <string.h>

// Magic Knob OS 2.0-alpha2 | UI, quadrature encoder, gesture support
// Experimental firmware, NOT factory-complete. Do NOT flash to ESP32-U4WDH.
// Patched ST77916 V2 library required, see GitHub Actions workflow.
constexpr int PIN_CS=14, PIN_CLK=13, PIN_D0=15, PIN_D1=16, PIN_D2=17, PIN_D3=18;
constexpr int PIN_RST=21, PIN_BL=47, PIN_SDA=11, PIN_SCL=12, PIN_TPRST=10;
constexpr int PIN_A=8, PIN_B=7, PIN_BAT=1, TOUCH_ADDR=0x15;
Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_CS,PIN_CLK,PIN_D0,PIN_D1,PIN_D2,PIN_D3);
Arduino_GFX *lcd = new Arduino_ST77916(bus,PIN_RST,0,true,360,360);
Preferences store;

constexpr uint16_t MK_DARK=0x0862, MK_PANEL=0x18e4, MK_PANEL2=0x2165, MK_GOLD=0xd587;
constexpr uint16_t MK_FAINT=0x732d, MK_WHITE=0xf79e, MK_BLUE=0x3d9d, MK_AMBER=0xfbc5;
constexpr uint16_t MK_RED=0xf2c5, MK_GREEN=0x5e0c;
enum Page : uint8_t {HOME, SETUP, GAME, COUNTERS, TOOLS, ROLL, TIMER, SETTINGS, DIAG};
enum RollType : uint8_t {ROLL_D6, ROLL_D20, ROLL_COIN};
RollType rollType=ROLL_D20;
Page page=HOME;
enum CounterType : uint8_t {POISON, ENERGY, TAX, COMMANDER};
CounterType selectedCounter=POISON;
uint8_t players=2, startLife=40, selected=0, brightness=210;
uint8_t knobDivider=4;
bool invertKnob=false, playing=false, fresh=true, lifeDirty=false, selectionDirty=false, counterDirty=false;
int16_t life[2]={40,40}, poison[2]={0,0}, energy[2]={0,0}, tax[2]={0,0}, cmdDamage[2]={0,0};
uint32_t lastChange=0, lastTouch=0, lastDiagnostics=0;
bool needsSave=false;
volatile int32_t knobDelta=0;
volatile uint32_t rawA=0, rawB=0;
volatile uint8_t encoderPrev=0;
int32_t pulseAccumulator=0;
bool touchActive=false, swipeAlreadyHandled=false;
int touchStartX=0, touchStartY=0, touchEndX=0, touchEndY=0;
uint32_t touchStartMs=0, lastReleaseMs=0;
uint8_t absentTouchSamples=0;
bool timerRunning=false;
uint32_t mkTimerStartedAt=0, timerElapsed=0;
uint32_t randomValue=0;
char toolResult[24]="Pronto";

// Quadrature: count valid transitions of both phases, reject bounce/reversal pairs.
// Previous rising-edge-only ISR could cancel clockwise movements.
void IRAM_ATTR knobEdge() {
  const uint8_t current=(gpio_get_level((gpio_num_t)PIN_A)<<1) | gpio_get_level((gpio_num_t)PIN_B);
  const uint8_t previous=encoderPrev;
  if(current==previous)return;
  if((current ^ previous) == 2) rawA++;
  else if((current ^ previous) == 1) rawB++;
  const uint8_t transition=(previous<<2)|current;
  if(transition==1 || transition==7 || transition==14 || transition==8) knobDelta++;
  else if(transition==2 || transition==11 || transition==13 || transition==4) knobDelta--;
  encoderPrev=current;
}
void markSaved() {needsSave=true; lastChange=millis();}
void printAt(const char *s,int x,int y,int scale,uint16_t c) {
  lcd->setTextWrap(false);lcd->setTextColor(c);lcd->setTextSize(scale);
  lcd->setCursor(x,y);lcd->print(s);
}
void centered(const char *s,int y,int scale,uint16_t c) {
  printAt(s,180-(int)strlen(s)*3*scale,y,scale,c);
}
void uiButton(int x,int y,int w,int h,const char *s,int scale=2,uint16_t color=MK_PANEL2) {
  lcd->fillRoundRect(x,y,w,h,12,color);
  lcd->drawRoundRect(x,y,w,h,12,MK_GOLD);
  printAt(s,x+(w-(int)strlen(s)*6*scale)/2,y+(h-8*scale)/2,scale,MK_WHITE);
}
void base(const char *title) {
  lcd->fillScreen(MK_DARK);
  lcd->drawCircle(180,180,176,MK_GOLD);
  lcd->drawCircle(180,180,172,MK_PANEL2);
  lcd->drawCircle(180,180,164,MK_PANEL);
  lcd->drawFastHLine(100,26,160,MK_PANEL2);
  lcd->drawFastHLine(100,330,160,MK_PANEL2);
  centered(title,37,2,MK_GOLD);
  lcd->drawFastHLine(93,64,174,MK_PANEL2);
  if(page!=HOME)centered("SWIPE > HOME",342,1,MK_FAINT);
}
void changePage(Page next) {page=next;fresh=true;lifeDirty=true;selectionDirty=true;counterDirty=true;}
void persist() {
  store.putUChar("players",players);store.putUChar("initial",startLife);
  store.putUChar("bright",brightness);store.putUChar("edges",knobDivider);
  store.putBool("reverse",invertKnob);store.putBool("playing",playing);
  for(int p=0;p<2;p++){
    String suffix=String(p);
    store.putShort(("life"+suffix).c_str(),life[p]);
    store.putShort(("poison"+suffix).c_str(),poison[p]);
    store.putShort(("energy"+suffix).c_str(),energy[p]);
    store.putShort(("tax"+suffix).c_str(),tax[p]);
    store.putShort(("cmd"+suffix).c_str(),cmdDamage[p]);
  }
  needsSave=false;
}
void load() {
  store.begin("magic-os",false);
  players=store.getUChar("players",2)==1?1:2;
  startLife=store.getUChar("initial",40)==20?20:40;
  brightness=constrain(store.getUChar("bright",210),40,255);
  knobDivider=constrain((int)store.getUChar("edges",4),2,8);
  invertKnob=store.getBool("reverse",false);
  playing=store.getBool("playing",false);
  for(int p=0;p<2;p++){
    String suffix=String(p);
    life[p]=constrain(store.getShort(("life"+suffix).c_str(),startLife),-99,999);
    poison[p]=constrain(store.getShort(("poison"+suffix).c_str(),0),0,99);
    energy[p]=constrain(store.getShort(("energy"+suffix).c_str(),0),0,999);
    tax[p]=constrain(store.getShort(("tax"+suffix).c_str(),0),0,999);
    cmdDamage[p]=constrain(store.getShort(("cmd"+suffix).c_str(),0),0,999);
  }
}
void setBrightness(){ledcWrite(0,brightness);}
void newGame() {
  for(int i=0;i<2;i++){life[i]=startLife;poison[i]=0;energy[i]=0;tax[i]=0;cmdDamage[i]=0;}
  selected=0;playing=true;markSaved();changePage(GAME);
}
void drawHome(){
  base("MAGIC KNOB OS");centered("2.0 ALPHA 2",77,2,MK_FAINT);
  uiButton(90,111,180,54,"MAGIC",3,0x10c4);
  uiButton(90,174,180,42,"NUOVA",2);
  uiButton(90,222,180,42,"STRUMENTI",2);
  uiButton(90,271,180,42,"IMPOSTAZ.",2);
}
void drawSetup(){
  base("NUOVA PARTITA");
  centered("Giocatori",94,2,MK_FAINT);
  uiButton(72,120,100,47,"1",3,players==1?MK_BLUE:MK_PANEL);
  uiButton(188,120,100,47,"2",3,players==2?MK_AMBER:MK_PANEL);
  centered("Punti vita",183,2,MK_FAINT);
  uiButton(72,208,100,47,"20",2,startLife==20?MK_BLUE:MK_PANEL);
  uiButton(188,208,100,47,"40",2,startLife==40?MK_AMBER:MK_PANEL);
  uiButton(105,276,150,46,"INIZIA",2,MK_GREEN);
  centered("<<",325,1,MK_FAINT);
}
void plusMinus(int x,int y,bool plus,uint16_t color) {
  lcd->fillCircle(x,y,26,MK_PANEL);lcd->drawCircle(x,y,26,color);
  lcd->fillRect(x-11,y-2,22,4,MK_WHITE);
  if(plus) lcd->fillRect(x-2,y-11,4,22,MK_WHITE);
}
void lifeArea(int p){
  const int y=players==1?133:(p==0?93:251);
  const int size=players==1?8:5;
  int h=players==1?82:55;
  lcd->fillRect(95,y,170,h,MK_DARK);
  char buf[16];snprintf(buf,sizeof(buf),"%d",life[p]);
  centered(buf,y+3,size,life[p]<=0?MK_RED:(p==0?MK_BLUE:MK_AMBER));
}
void paintSelection(){
  if(players==1)return;
  lcd->fillRect(127,76,110,18,MK_DARK);
  lcd->fillRect(127,232,110,18,MK_DARK);
  centered(selected==0?"[P1]":" P1 ",78,2,selected==0?MK_GOLD:MK_BLUE);
  centered(selected==1?"[P2]":" P2 ",234,2,selected==1?MK_GOLD:MK_AMBER);
}
void drawGame(){
  base(players==1?"MAGIC LIFE":"2 GIOCATORI");
  if(players==1){
    centered("GIOCATORE 1",96,2,MK_BLUE);
    plusMinus(58,184,false,MK_RED);plusMinus(302,184,true,MK_GREEN);
    uiButton(65,278,95,38,"CONT.",2);
    uiButton(199,278,95,38,"MENU",2);
  }else{
    lcd->drawFastHLine(81,179,198,MK_GOLD);
    plusMinus(58,116,false,MK_BLUE);plusMinus(302,116,true,MK_BLUE);
    plusMinus(58,288,false,MK_AMBER);plusMinus(302,288,true,MK_AMBER);
    uiButton(104,160,70,36,"CONT",1);
    uiButton(185,160,70,36,"MENU",1);
  }
  lifeArea(0);if(players==2)lifeArea(1);paintSelection();
}
const char* counterName(){switch(selectedCounter){
  case POISON:return "VELENO";case ENERGY:return "ENERGIA";
  case TAX:return "TASSA CMD";default:return "DANNI CMD";
}}
int16_t &currentCounter() {
  int p=players==1?0:selected;
  switch(selectedCounter){case POISON:return poison[p];case ENERGY:return energy[p];
    case TAX:return tax[p];default:return cmdDamage[p];}
}
void drawCounters(){
  base("CONTATORI");
  centered("P1/P2: tocchi per scegliere",75,1,MK_FAINT);
  uiButton(65,98,110,39,selected==0?"[P1]":" P1 ",2,selected==0?MK_BLUE:MK_PANEL);
  if(players==2)uiButton(185,98,110,39,selected==1?"[P2]":" P2 ",2,selected==1?MK_AMBER:MK_PANEL);
  centered(counterName(),150,2,MK_GOLD);
  uiButton(91,190,47,39,"<",2);
  uiButton(222,190,47,39,">",2);
  uiButton(107,289,146,37,"PARTITA",2);
  redrawCounter();
}
void redrawCounter(){
  if(page!=COUNTERS)return;
  lcd->fillRect(135,185,90,94,MK_DARK);
  char buf[16];snprintf(buf,sizeof(buf),"%d",currentCounter());
  centered(buf,239,4,currentCounter()>=10&&selectedCounter==POISON?MK_RED:MK_WHITE);
}
void drawTools() {
  base("STRUMENTI MTG");
  uiButton(65,103,106,54,"D6",3);
  uiButton(189,103,106,54,"D20",3);
  uiButton(81,173,198,44,"MONETA",2);
  uiButton(81,227,198,44,"TIMER",2);
  uiButton(107,291,146,38,"HOME",2);
}
// Big dedicated result page: tap the main area to reroll, swipe to Home.
void throwTool() {
  if(rollType==ROLL_D6)randomValue=(esp_random()%6)+1;
  else if(rollType==ROLL_D20)randomValue=(esp_random()%20)+1;
  else randomValue=esp_random()&1;
  fresh=true;
}
void drawRoll() {
  base(rollType==ROLL_D6?"DADO D6":rollType==ROLL_D20?"DADO D20":"MONETA");
  lcd->drawCircle(180,172,104,MK_GOLD);
  lcd->drawCircle(180,172,98,MK_PANEL2);
  char result[20];
  if(rollType==ROLL_COIN){
    strcpy(result,randomValue?"TESTA":"CROCE");
    centered(result,146,5,MK_WHITE);
  } else {
    snprintf(result,sizeof(result),"%lu",(unsigned long)randomValue);
    centered(result,115,12,MK_WHITE);
  }
  uiButton(100,278,160,44,"RILANCIA",2);
}
void drawTimer(){
  base("CRONOMETRO");
  uiButton(79,204,202,43,timerRunning?"PAUSA":"AVVIA",2);
  uiButton(92,262,80,38,"AZZ",2);
  uiButton(188,262,80,38,"MENU",2);
}
void repaintTimer(){
  if(page!=TIMER)return;
  uint32_t val=timerElapsed+(timerRunning?millis()-mkTimerStartedAt:0);
  char s[20];snprintf(s,sizeof(s),"%02lu:%02lu",(unsigned long)((val/60000)%100),(unsigned long)((val/1000)%60));
  lcd->fillRect(93,116,174,62,MK_DARK);centered(s,126,4,MK_GOLD);
}
void drawSettings(){
  base("IMPOSTAZIONI");
  centered("Luminosita",92,2,MK_FAINT);
  uiButton(65,115,52,37,"-",2);
  uiButton(243,115,52,37,"+",2);
  centered("Direzione ghiera",169,2,MK_FAINT);
  uiButton(78,192,204,37,invertKnob?"INVERTITA":"NORMALE",2);
  centered("Fasi per vita (4 tip.)",239,1,MK_FAINT);
  uiButton(68,267,56,37,"-",2);
  uiButton(238,267,56,37,"+",2);
  centered("<< MENU",316,1,MK_GOLD);
}
void settingsValues(){
  if(page!=SETTINGS)return;
  char s[12];lcd->fillRect(138,114,84,42,MK_DARK);
  snprintf(s,sizeof(s),"%u",brightness);centered(s,127,2,MK_WHITE);
  lcd->fillRect(142,264,76,44,MK_DARK);
  snprintf(s,sizeof(s),"%u",knobDivider);centered(s,275,2,MK_WHITE);
}
void drawDiagnostics(){
  base("DIAGNOSTICA");
  centered("Ruota nei due versi",82,2,MK_FAINT);
  centered("Impulsi rilevati",112,2,MK_FAINT);
  uiButton(101,277,158,44,"INDIETRO",2);
}
void redrawDiagnostics(){
  if(page!=DIAG)return;
  uint32_t a,b;noInterrupts();a=rawA;b=rawB;interrupts();
  lcd->fillRect(93,145,175,118,MK_DARK);
  char s[40];snprintf(s,sizeof(s),"A: %lu",(unsigned long)a);centered(s,153,2,MK_BLUE);
  snprintf(s,sizeof(s),"B: %lu",(unsigned long)b);centered(s,185,2,MK_AMBER);
  analogReadResolution(12);
  float v=analogReadMilliVolts(PIN_BAT)*0.002f;
  if(v>4.4f)snprintf(s,sizeof(s),"USB/ADC: %.1fV",v);
  else snprintf(s,sizeof(s),"VBAT: %.2fV",v);
  centered(s,225,1,MK_FAINT);
}
void render(){
  switch(page) {
    case HOME:drawHome();break;case SETUP:drawSetup();break;
    case GAME:drawGame();break;case COUNTERS:drawCounters();break;
    case TOOLS:drawTools();break;case ROLL:drawRoll();break;case TIMER:drawTimer();break;
    case SETTINGS:drawSettings();settingsValues();break;
    case DIAG:drawDiagnostics();redrawDiagnostics();break;
  }
  fresh=false;lifeDirty=false;selectionDirty=false;counterDirty=false;
}
void adjustLife(int p,int change) {
  if(!playing||p>=players)return;
  life[p]=constrain((int)life[p]+change,-99,999);selected=p;
  markSaved();lifeDirty=true;selectionDirty=true;
}
void adjustCounter(int amount) {
  int16_t &c=currentCounter();
  c=constrain((int)c+amount,0,999);
  markSaved();counterDirty=true;
}
bool readTouch(int &x,int &y) {
  Wire.beginTransmission(TOUCH_ADDR);Wire.write((uint8_t)0x01);
  if(Wire.endTransmission(true)!=0)return false;
  if(Wire.requestFrom((uint8_t)TOUCH_ADDR,(uint8_t)6)!=6)return false;
  uint8_t d[6];for(int i=0;i<6;i++)d[i]=Wire.read();
  if(!(d[1]&0x0f))return false;
  x=((d[2]&15)<<8)|d[3];y=((d[4]&15)<<8)|d[5];
  return x>=0&&x<360&&y>=0&&y<360;
}
bool within(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;}
bool near(int x,int y,int a,int b){long dx=x-a,dy=y-b;return dx*dx+dy*dy<=30L*30L;}
void touchAction(int x,int y){
  if(page==HOME){
    if(within(x,y,90,111,270,165))changePage(playing?GAME:SETUP);
    else if(within(x,y,90,174,270,216))changePage(SETUP);
    else if(within(x,y,90,222,270,264))changePage(TOOLS);
    else if(within(x,y,90,271,270,313))changePage(SETTINGS);
  } else if(page==SETUP){
    if(within(x,y,72,120,172,167)){players=1;fresh=true;markSaved();}
    else if(within(x,y,188,120,288,167)){players=2;fresh=true;markSaved();}
    else if(within(x,y,72,208,172,255)){startLife=20;fresh=true;markSaved();}
    else if(within(x,y,188,208,288,255)){startLife=40;fresh=true;markSaved();}
    else if(within(x,y,105,276,255,322))newGame();
    else if(y>320)changePage(HOME);
  } else if(page==GAME){
    if(players==1){
      if(near(x,y,58,184))adjustLife(0,-1);
      else if(near(x,y,302,184))adjustLife(0,1);
      else if(within(x,y,65,278,160,316))changePage(COUNTERS);
      else if(within(x,y,199,278,294,316))changePage(HOME);
    }else{
      if(near(x,y,58,116))adjustLife(0,-1);
      else if(near(x,y,302,116))adjustLife(0,1);
      else if(near(x,y,58,288))adjustLife(1,-1);
      else if(near(x,y,302,288))adjustLife(1,1);
      else if(within(x,y,104,160,174,196))changePage(COUNTERS);
      else if(within(x,y,185,160,255,196))changePage(HOME);
      else if(y>65&&y<150){selected=0;selectionDirty=true;}
      else if(y>225&&y<325){selected=1;selectionDirty=true;}
    }
  } else if(page==COUNTERS){
    if(within(x,y,65,98,175,137)){selected=0;fresh=true;}
    else if(players==2&&within(x,y,185,98,295,137)){selected=1;fresh=true;}
    else if(within(x,y,91,190,138,229)){selectedCounter=(CounterType)((selectedCounter+3)%4);fresh=true;}
    else if(within(x,y,222,190,269,229)){selectedCounter=(CounterType)((selectedCounter+1)%4);fresh=true;}
    else if(within(x,y,107,289,253,326))changePage(GAME);
  } else if(page==TOOLS){
    if(within(x,y,65,103,171,157)){rollType=ROLL_D6;throwTool();changePage(ROLL);}
    else if(within(x,y,189,103,295,157)){rollType=ROLL_D20;throwTool();changePage(ROLL);}
    else if(within(x,y,81,173,279,217)){rollType=ROLL_COIN;throwTool();changePage(ROLL);}
    else if(within(x,y,81,227,279,271))changePage(TIMER);
    else if(within(x,y,107,291,253,329))changePage(HOME);
  } else if(page==ROLL){
    if(within(x,y,80,275,280,325)||within(x,y,75,78,285,267))throwTool();
    else if(y>325)changePage(TOOLS);
  } else if(page==TIMER){
    if(within(x,y,79,204,281,247)){
      if(timerRunning){timerElapsed+=millis()-mkTimerStartedAt;timerRunning=false;}
      else{mkTimerStartedAt=millis();timerRunning=true;}
      fresh=true;
    }else if(within(x,y,92,262,172,300)){timerRunning=false;timerElapsed=0;fresh=true;}
    else if(within(x,y,188,262,268,300))changePage(TOOLS);
  } else if(page==SETTINGS){
    if(within(x,y,65,115,117,152)){brightness=max(40,(int)brightness-15);setBrightness();markSaved();settingsValues();}
    else if(within(x,y,243,115,295,152)){brightness=min(255,(int)brightness+15);setBrightness();markSaved();settingsValues();}
    else if(within(x,y,78,192,282,229)){invertKnob=!invertKnob;markSaved();fresh=true;}
    else if(within(x,y,68,267,124,304)){knobDivider=max(2,(int)knobDivider-1);markSaved();settingsValues();}
    else if(within(x,y,238,267,294,304)){knobDivider=min(8,(int)knobDivider+1);markSaved();settingsValues();}
    else if(y>312){changePage(HOME);}
    else if(y>75&&y<110){changePage(DIAG);}
  } else if(page==DIAG){
    if(within(x,y,101,277,259,321))changePage(SETTINGS);
  }
}
void setup(){
  Serial.begin(115200);delay(260);load();
  pinMode(PIN_BL,OUTPUT);digitalWrite(PIN_BL,LOW);
  pinMode(PIN_A,INPUT_PULLUP);pinMode(PIN_B,INPUT_PULLUP);
  encoderPrev=(digitalRead(PIN_A)<<1)|digitalRead(PIN_B);
  attachInterrupt(digitalPinToInterrupt(PIN_A),knobEdge,CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B),knobEdge,CHANGE);
  Wire.begin(PIN_SDA,PIN_SCL);Wire.setClock(400000);
  pinMode(PIN_TPRST,OUTPUT);digitalWrite(PIN_TPRST,LOW);delay(35);
  digitalWrite(PIN_TPRST,HIGH);delay(150);
  // Force a hardware LCD reset on warm USB/RTS boot; not only on power cycle.
  pinMode(PIN_RST,OUTPUT);digitalWrite(PIN_RST,LOW);delay(60);
  digitalWrite(PIN_RST,HIGH);delay(150);
  bool ok=lcd->begin(40000000);
  if(!ok){Serial.println("LCD init failed");return;}
  lcd->setRotation(0);
  randomSeed(esp_random());fresh=true;
  render(); // draw first frame while backlight remains off
  ledcSetup(0,50000,8);ledcAttachPin(PIN_BL,0);
  setBrightness();
}
void loop(){
  noInterrupts();int32_t amount=knobDelta;knobDelta=0;interrupts();
  if(amount!=0){
    if(invertKnob)amount=-amount;
    pulseAccumulator+=amount;
    if(abs(pulseAccumulator)>=knobDivider){
      int step=pulseAccumulator/knobDivider;
      pulseAccumulator-=(step*knobDivider);
      step=constrain(step,-30,30);
      if(page==GAME)adjustLife(players==1?0:selected,step);
      else if(page==COUNTERS)adjustCounter(step);
      else if(page==SETTINGS){
        brightness=constrain((int)brightness+step*5,40,255);
        setBrightness();markSaved();settingsValues();
      }
    }
  }
  static uint32_t touchPoll=0;
  if(millis()-touchPoll>16){
    touchPoll=millis();int x,y;bool down=readTouch(x,y);
    if(down){
      absentTouchSamples=0;
      if(!touchActive){
        touchActive=true;swipeAlreadyHandled=false;
        touchStartX=touchEndX=x;touchStartY=touchEndY=y;
        touchStartMs=millis();
      }else{
        touchEndX=x;touchEndY=y;
        int dx=touchEndX-touchStartX,dy=touchEndY-touchStartY;
        if(page!=HOME && !swipeAlreadyHandled && touchStartX<=115 &&
           dx>=100 && abs(dy)<=75 && millis()-touchStartMs<=1200) {
          swipeAlreadyHandled=true;
          changePage(HOME);
        }
      }
    }else if(touchActive && ++absentTouchSamples>=2){
      touchActive=false;absentTouchSamples=0;
      int dx=touchEndX-touchStartX,dy=touchEndY-touchStartY;
      if(!swipeAlreadyHandled && abs(dx)<28 && abs(dy)<28 &&
         millis()-touchStartMs<1000 && millis()-lastReleaseMs>100){
        touchAction(touchStartX,touchStartY);
      }
      lastReleaseMs=millis();
    }
  }
  if(fresh)render();
  else {
    if(page==GAME){if(lifeDirty){lifeArea(0);if(players==2)lifeArea(1);lifeDirty=false;}
      if(selectionDirty){paintSelection();selectionDirty=false;}}
    else if(page==COUNTERS&&counterDirty){redrawCounter();counterDirty=false;}
    if(page==TIMER&&millis()-lastDiagnostics>300){lastDiagnostics=millis();repaintTimer();}
    if(page==DIAG&&millis()-lastDiagnostics>250){lastDiagnostics=millis();redrawDiagnostics();}
  }
  if(needsSave&&millis()-lastChange>1800)persist();
  delay(2);
}
