#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <string.h>

// Magic Knob OS 2.0-alpha4.2 | CW rising-edge option and independent pulse diagnostics
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
enum Page : uint8_t {HOME, SETUP, GAME, RESET_CONFIRM, COUNTERS, TOOLS, ROLL, TIMER, SETTINGS, DISPLAY_SETTINGS, KNOB_SETTINGS, BATTERY, DIAG};
enum RollType : uint8_t {ROLL_D6, ROLL_D20, ROLL_COIN};
RollType rollType=ROLL_D20;
Page page=HOME;
enum CounterType : uint8_t {POISON, ENERGY, TAX, COMMANDER};
CounterType selectedCounter=POISON;
uint8_t players=2, startLife=40, selected=0, brightness=210;
uint8_t knobDivider=1;
bool invertKnob=false, playing=false, fresh=true, lifeDirty=false, selectionDirty=false, counterDirty=false;
uint8_t lifeDirtyMask=0x03;
struct KnockoutEffect {
  bool active=false;
  uint8_t frame=255;
  uint32_t started=0;
};
KnockoutEffect knockout[2];
int16_t life[2]={40,40}, poison[2]={0,0}, energy[2]={0,0}, tax[2]={0,0}, cmdDamage[2]={0,0};
uint32_t lastChange=0, lastTouch=0, lastDiagnostics=0;
uint32_t lastBatteryRead=0;
float batteryVolts=0.0f;
bool batterySampleValid=false;
uint32_t lastLifePaintAt=0;
uint32_t lastSelectionPaintAt=0;
bool needsSave=false;
volatile int32_t knobDelta=0;
volatile uint32_t rawA=0, rawB=0;
volatile uint32_t rawAFalling=0, rawARising=0, rawBFalling=0, rawBRising=0;
volatile uint32_t lastPulseA=0, lastPulseB=0;
volatile bool clockwiseRising=true;
int32_t pulseAccumulator=0;
bool touchActive=false, swipeAlreadyHandled=false;
int touchStartX=0, touchStartY=0, touchEndX=0, touchEndY=0;
uint32_t touchStartMs=0, lastReleaseMs=0;
uint8_t absentTouchSamples=0;
bool timerRunning=false;
uint32_t mkTimerStartedAt=0, timerElapsed=0;
uint32_t randomValue=0;
char toolResult[24]="Pronto";

// Two independent active-low direction pulses, NOT quadrature.
// GPIO8 = clockwise, GPIO7 = anticlockwise. Count both physical edges for
// diagnostics but apply exactly ONE selected edge per detent to the game.
// The right-hand direction defaults to RISING, as in another field-tested
// Waveshare implementation; LEFT remains the previously reliable FALLING.
// The CW mode can be changed in Settings > Knob without reflashing.
constexpr uint32_t PULSE_DEBOUNCE_US=2400;
void IRAM_ATTR countClockwiseEdge(uint32_t t) {
  if((uint32_t)(t-lastPulseA)<PULSE_DEBOUNCE_US)return;
  lastPulseA=t;rawA++;knobDelta++;
}
void IRAM_ATTR countCounterclockwiseEdge(uint32_t t) {
  if((uint32_t)(t-lastPulseB)<PULSE_DEBOUNCE_US)return;
  lastPulseB=t;rawB++;knobDelta--;
}
void IRAM_ATTR knobClockwise() {
  const uint32_t t=micros();
  const bool high=gpio_get_level((gpio_num_t)PIN_A)!=0;
  if(high)rawARising++;
  else rawAFalling++;
  if(high==clockwiseRising)countClockwiseEdge(t);
}
void IRAM_ATTR knobCounterclockwise() {
  const uint32_t t=micros();
  const bool high=gpio_get_level((gpio_num_t)PIN_B)!=0;
  if(high)rawBRising++;
  else rawBFalling++;
  if(!high)countCounterclockwiseEdge(t);
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
void changePage(Page next) {
  if(next!=GAME)for(auto &fx:knockout)fx.active=false;
  page=next;fresh=true;lifeDirty=true;lifeDirtyMask=0x03;
  selectionDirty=true;counterDirty=true;
}
void persist() {
  store.putUChar("players",players);store.putUChar("initial",startLife);
  store.putUChar("bright",brightness);store.putUChar("pulseDiv",knobDivider);
  store.putBool("reverse",invertKnob);store.putBool("cwRise",clockwiseRising);store.putBool("playing",playing);
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
  knobDivider=constrain((int)store.getUChar("pulseDiv",1),1,4);
  invertKnob=store.getBool("reverse",false);
  clockwiseRising=store.getBool("cwRise",true);
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
// GPIO1 is wired through an approximately 2:1 voltage divider.
// This measures the monitored rail, which may be USB-biased; never
// present USB-biased voltage as a precise battery state of charge.
void sampleBattery(bool force=false){
  if(!force && (uint32_t)(millis()-lastBatteryRead)<2200)return;
  lastBatteryRead=millis();
  uint32_t total=0;
  for(int i=0;i<10;i++){total+=(uint32_t)analogReadMilliVolts(PIN_BAT);delayMicroseconds(140);}
  float measured=0.002f*(float(total)/10.0f);
  if(!isfinite(measured) || measured<0.0f || measured>6.5f){
    batterySampleValid=false;return;
  }
  batterySampleValid=true;
  if(batteryVolts<=0.05f)batteryVolts=measured;
  else batteryVolts=batteryVolts*0.65f+measured*0.35f;
}
int batteryPercentApprox(float voltage){
  // Very approximate LiPo open-circuit state of charge: not a fuel gauge.
  const float v[]={3.35f,3.50f,3.60f,3.70f,3.80f,3.90f,4.00f,4.10f,4.20f};
  const int p[]={0,8,15,25,45,60,75,90,100};
  if(voltage<=v[0])return 0;
  if(voltage>=v[8])return 100;
  for(int i=1;i<9;i++){
    if(voltage<=v[i])
      return (int)(p[i-1]+(p[i]-p[i-1])*(voltage-v[i-1])/(v[i]-v[i-1]));
  }
  return 100;
}
void newGame() {
  for(int i=0;i<2;i++){
    life[i]=startLife;poison[i]=0;energy[i]=0;tax[i]=0;cmdDamage[i]=0;
    knockout[i].active=false;
  }
  selected=0;playing=true;markSaved();changePage(GAME);
}
void drawHome(){
  base("MAGIC KNOB OS");centered("2.0 ALPHA 4.2",77,2,MK_FAINT);
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
// A fixed, symmetrical two-player layout. Only numbers, badges and focus
// strokes are updated during play; the whole screen is never redrawn per life.
void duelButton(int x,int y,bool plus,uint16_t accent){
  lcd->fillCircle(x,y,22,MK_DARK);
  lcd->drawCircle(x,y,21,accent);
  lcd->drawCircle(x,y,18,MK_PANEL2);
  lcd->fillRect(x-10,y-2,20,4,MK_WHITE);
  if(plus)lcd->fillRect(x-2,y-10,4,20,MK_WHITE);
}
void duelPanel(int p){
  const int y=(p==0)?75:215;
  const uint16_t accent=p==0?MK_BLUE:MK_AMBER;
  lcd->fillRoundRect(51,y,258,96,18,MK_PANEL);
  lcd->drawRoundRect(51,y,258,96,18,accent);
  lcd->drawFastHLine(124,y+30,112,MK_PANEL2);
  lcd->drawLine(180,y+32,180,y+34,MK_GOLD);
  duelButton(78,y+67,false,accent);
  duelButton(282,y+67,true,accent);
}
void knockoutBadge(int p,uint16_t color){
  if(players!=2)return;
  const int y=(p==0)?84:224;
  lcd->fillRoundRect(251,y,39,21,7,MK_PANEL);
  if(life[p]>0)return;
  lcd->fillRoundRect(251,y,39,21,7,color);
  printAt("KO",263,y+7,1,MK_DARK);
}
void lifeArea(int p){
  char buf[16];snprintf(buf,sizeof(buf),"%d",life[p]);
  if(players==1){
    lcd->fillRect(95,133,170,82,MK_DARK);
    centered(buf,136,8,life[p]<=0?MK_RED:MK_BLUE);
    return;
  }
  const int y=p==0?109:249;
  // Only refresh the rectangular text area, not the rounded panel every pulse.
  // This avoids expensive full-widget fills while turning the knob quickly.
  lcd->fillRect(115,y+4,130,50,MK_PANEL);
  int digits=strlen(buf);
  int size=digits<=2?6:(digits==3?5:4);
  int x=180-(digits*6*size)/2;
  int baseline=y+(58-8*size)/2;
  uint16_t color=life[p]<=0?MK_RED:
                 life[p]<=5?MK_GOLD:(p==0?MK_BLUE:MK_AMBER);
  printAt(buf,x,baseline,size,color);
  lcd->drawRoundRect(106,y,148,58,12,life[p]<=0?MK_RED:MK_PANEL);
  // Always refresh the small badge area: this remains correct even when
  // returning from menus or starting a new game.
  knockoutBadge(p,MK_RED);
}
void paintSelection(){
  if(players!=2)return;
  for(int p=0;p<2;p++){
    const int y=p==0?75:215;
    uint16_t accent=p==0?MK_BLUE:MK_AMBER;
    lcd->drawRoundRect(50,y-1,260,98,19,
                       selected==p?MK_GOLD:accent);
    lcd->fillRoundRect(146,y+7,68,25,10,
                       selected==p?MK_PANEL2:MK_PANEL);
    printAt(p==0?"P1":"P2",168,y+12,2,
            selected==p?MK_GOLD:accent);
  }
}
void drawKOFrame(int p,uint8_t frame){
  if(players!=2||page!=GAME)return;
  const int y=p==0?109:249;
  const uint16_t pulse[8]={
    MK_RED,MK_GOLD,MK_RED,MK_PANEL2,
    MK_RED,MK_GOLD,MK_RED,MK_RED
  };
  uint16_t shade=pulse[frame<8?frame:7];
  lcd->drawRoundRect(106,y,148,58,12,shade);
  knockoutBadge(p,shade);
}
void updateKOAnimations(){
  if(page!=GAME||players!=2)return;
  uint32_t now=millis();
  for(int p=0;p<2;p++){
    KnockoutEffect &fx=knockout[p];
    if(!fx.active)continue;
    if(life[p]>0){fx.active=false;continue;}
    uint8_t next=(uint8_t)min((uint32_t)7,
                   (uint32_t)((now-fx.started)/100));
    if(next!=fx.frame){fx.frame=next;drawKOFrame(p,next);}
    if((uint32_t)(now-fx.started)>=800){
      fx.active=false;drawKOFrame(p,7);
    }
  }
}
void drawGame(){
  base("MAGIC LIFE");
  if(players==1){
    centered("GIOCATORE 1",96,2,MK_BLUE);
    plusMinus(58,184,false,MK_RED);plusMinus(302,184,true,MK_GREEN);
    uiButton(65,278,95,38,"CONT.",2);
    uiButton(199,278,95,38,"MENU",2);
  }else{
    // Balanced panels for one screen viewed by two players.
    duelPanel(0);duelPanel(1);
    lcd->drawFastHLine(89,194,182,MK_GOLD);
    uiButton(58,178,79,32,"SEGN.",2,MK_PANEL);
    uiButton(142,178,78,32,"RESET",2,MK_PANEL);
    uiButton(225,178,77,32,"MENU",2,MK_PANEL);
  }
  lifeArea(0);if(players==2)lifeArea(1);
  paintSelection();
}
void drawResetConfirm(){
  base("RESET PARTITA");
  centered("RICOMINCIARE?",107,2,MK_GOLD);
  centered("Vite e segnalini",148,2,MK_WHITE);
  centered("saranno azzerati.",173,2,MK_FAINT);
  uiButton(65,224,108,51,"ANNULLA",1,MK_PANEL2);
  uiButton(187,224,108,51,"CONFERMA",1,MK_RED);
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
  base("SEGNALINI");
  centered("SCEGLI GIOCATORE",75,1,MK_FAINT);
  uiButton(65,94,110,39,selected==0?"[P1]":" P1 ",2,selected==0?MK_BLUE:MK_PANEL);
  if(players==2)uiButton(185,94,110,39,selected==1?"[P2]":" P2 ",2,selected==1?MK_AMBER:MK_PANEL);
  uiButton(68,157,42,34,"<",2);
  uiButton(250,157,42,34,">",2);
  centered(counterName(),145,2,MK_GOLD);
  lcd->drawCircle(180,234,61,MK_GOLD);
  lcd->drawCircle(180,234,58,MK_PANEL2);
  plusMinus(78,234,false,MK_RED);
  plusMinus(282,234,true,MK_GREEN);
  uiButton(111,290,138,36,"PARTITA",2);
  redrawCounter();
}
void redrawCounter(){
  if(page!=COUNTERS)return;
  // Clear only the digit box; don't touch the gold ring or +/- controls.
  lcd->fillRect(138,198,84,73,MK_DARK);
  char buf[16];snprintf(buf,sizeof(buf),"%d",currentCounter());
  const int digits=(int)strlen(buf);
  const int size=digits==1?8:(digits==2?6:4);
  const int x=180-(digits*6*size)/2;
  const int y=234-(8*size)/2;
  printAt(buf,x,y,size,currentCounter()>=10&&selectedCounter==POISON?MK_RED:MK_WHITE);
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
  uiButton(80,92,200,43,"SCHERMO",2);
  uiButton(80,146,200,43,"GHIERA",2);
  uiButton(80,200,200,43,"BATTERIA",2);
  uiButton(80,254,200,43,"DIAGNOSTICA",2);
}
void drawDisplaySettings(){
  base("SCHERMO");
  centered("LUMINOSITA",107,2,MK_FAINT);
  uiButton(78,163,62,52,"-",3);
  uiButton(220,163,62,52,"+",3);
  char buf[16];snprintf(buf,sizeof(buf),"%u",brightness);
  centered(buf,175,2,MK_WHITE);
  centered("40-255: intensita luce",247,1,MK_FAINT);
  uiButton(109,280,142,37,"INDIETRO",2);
}
void displayValues(){
  if(page!=DISPLAY_SETTINGS)return;
  lcd->fillRect(144,164,73,54,MK_DARK);
  char buf[16];snprintf(buf,sizeof(buf),"%u",brightness);
  centered(buf,177,2,MK_WHITE);
}
void drawKnobSettings(){
  base("GHIERA");
  centered("DIREZIONE",77,2,MK_FAINT);
  uiButton(80,96,200,37,invertKnob?"INVERTITA":"NORMALE",2);
  centered("SENSIBILITA",142,2,MK_FAINT);
  uiButton(76,163,60,43,"-",3);
  uiButton(224,163,60,43,"+",3);
  char buf[8];snprintf(buf,sizeof(buf),"%u",knobDivider);
  centered(buf,172,3,MK_GOLD);
  centered("1 = uno scatto per vita",212,1,MK_FAINT);
  centered("SCATTO VERSO DESTRA",236,1,MK_GOLD);
  uiButton(79,251,202,35,clockwiseRising?"FRONTE SALITA":"FRONTE DISCESA",1);
  uiButton(112,293,136,34,"INDIETRO",2);
}
void knobValues(){
  if(page!=KNOB_SETTINGS)return;
  lcd->fillRect(145,164,70,42,MK_DARK);
  char buf[8];snprintf(buf,sizeof(buf),"%u",knobDivider);
  centered(buf,173,3,MK_GOLD);
}
void drawBattery(){
  base("BATTERIA");
  centered("MISURA TENSIONE",95,2,MK_FAINT);
  uiButton(106,285,148,37,"INDIETRO",2);
  // Readout is refreshed independently without clearing the whole display.
  updateBatteryDisplay();
}
void updateBatteryDisplay(){
  if(page!=BATTERY)return;
  lcd->fillRect(64,126,232,148,MK_DARK);
  char buf[32];
  if(!batterySampleValid){
    centered("NON DISPONIBILE",159,2,MK_AMBER);
    centered("Controllare il sensore",226,1,MK_FAINT);
    return;
  }
  snprintf(buf,sizeof(buf),"%.2f V",batteryVolts);
  centered(buf,150,4,MK_WHITE);
  if(batteryVolts>4.35f){
    centered("LETTURA USB?",210,2,MK_AMBER);
    centered("Carica non misurabile",240,1,MK_FAINT);
  }else if(batteryVolts<2.7f){
    centered("VALORE NON VALIDO",210,2,MK_RED);
    centered("Batteria assente?",240,1,MK_FAINT);
  }else{
    snprintf(buf,sizeof(buf),"~%d%% (stima)",batteryPercentApprox(batteryVolts));
    centered(buf,210,2,MK_GOLD);
    centered("Verificare senza USB",240,1,MK_FAINT);
  }
}
void drawDiagnostics(){
  base("DIAGNOSTICA");
  centered("RUOTA UN SOLO SCATTO",77,1,MK_FAINT);
  uiButton(58,278,116,42,"AZZERA",2);
  uiButton(184,278,116,42,"INDIETRO",1);
}
void redrawDiagnostics(){
  if(page!=DIAG)return;
  uint32_t a,b,af,ar,bf,br;
  noInterrupts();
  a=rawA;b=rawB;af=rawAFalling;ar=rawARising;
  bf=rawBFalling;br=rawBRising;
  interrupts();
  lcd->fillRect(52,99,256,169,MK_DARK);
  char s[48];
  snprintf(s,sizeof(s),"DESTRA: %lu",(unsigned long)a);
  centered(s,107,2,MK_BLUE);
  snprintf(s,sizeof(s),"GIU:%lu  SU:%lu",(unsigned long)af,(unsigned long)ar);
  centered(s,139,1,MK_FAINT);
  snprintf(s,sizeof(s),"SINISTRA: %lu",(unsigned long)b);
  centered(s,171,2,MK_AMBER);
  snprintf(s,sizeof(s),"GIU:%lu  SU:%lu",(unsigned long)bf,(unsigned long)br);
  centered(s,203,1,MK_FAINT);
  centered(clockwiseRising?"DESTRA: SALITA":"DESTRA: DISCESA",237,1,MK_GOLD);
}
void render(){
  switch(page) {
    case HOME:drawHome();break;case SETUP:drawSetup();break;
    case GAME:drawGame();break;case RESET_CONFIRM:drawResetConfirm();break;
    case COUNTERS:drawCounters();break;
    case TOOLS:drawTools();break;case ROLL:drawRoll();break;case TIMER:drawTimer();break;
    case SETTINGS:drawSettings();break;
    case DISPLAY_SETTINGS:drawDisplaySettings();break;
    case KNOB_SETTINGS:drawKnobSettings();break;
    case BATTERY:drawBattery();break;
    case DIAG:drawDiagnostics();redrawDiagnostics();break;
  }
  fresh=false;lifeDirty=false;lifeDirtyMask=0;selectionDirty=false;counterDirty=false;
}
void adjustLife(int p,int change) {
  if(!playing||p>=players)return;
  const int previous=life[p];
  const int updated=constrain(previous+change,-99,999);
  if(updated==previous)return;
  const bool selectionChanged=(selected!=p);
  life[p]=updated;selected=p;
  // Trigger once on crossing >0 to <=0. Never trigger again below zero.
  if(previous>0 && updated<=0 && players==2){
    knockout[p].active=true;
    knockout[p].frame=255;
    knockout[p].started=millis();
  }else if(updated>0){
    knockout[p].active=false;
  }
  markSaved();lifeDirty=true;lifeDirtyMask|=(1<<p);
  // In Alpha 4 we redrew both focus frames on EVERY pulse; avoid that.
  if(selectionChanged)selectionDirty=true;
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
      if(near(x,y,78,142))adjustLife(0,-1);
      else if(near(x,y,282,142))adjustLife(0,1);
      else if(near(x,y,78,282))adjustLife(1,-1);
      else if(near(x,y,282,282))adjustLife(1,1);
      else if(within(x,y,58,178,137,210))changePage(COUNTERS);
      else if(within(x,y,142,178,220,210))changePage(RESET_CONFIRM);
      else if(within(x,y,225,178,302,210))changePage(HOME);
      else if(within(x,y,99,76,260,171)){selected=0;selectionDirty=true;}
      else if(within(x,y,99,215,260,311)){selected=1;selectionDirty=true;}
    }
  } else if(page==RESET_CONFIRM){
    if(within(x,y,65,224,173,275))changePage(GAME);
    else if(within(x,y,187,224,295,275))newGame();
  } else if(page==COUNTERS){
    if(within(x,y,65,94,175,133)){selected=0;fresh=true;}
    else if(players==2&&within(x,y,185,94,295,133)){selected=1;fresh=true;}
    else if(within(x,y,68,157,110,191)){selectedCounter=(CounterType)((selectedCounter+3)%4);fresh=true;}
    else if(within(x,y,250,157,292,191)){selectedCounter=(CounterType)((selectedCounter+1)%4);fresh=true;}
    else if(near(x,y,78,234))adjustCounter(-1);
    else if(near(x,y,282,234))adjustCounter(1);
    else if(within(x,y,111,290,249,326))changePage(GAME);
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
    if(within(x,y,80,92,280,135))changePage(DISPLAY_SETTINGS);
    else if(within(x,y,80,146,280,189))changePage(KNOB_SETTINGS);
    else if(within(x,y,80,200,280,243))changePage(BATTERY);
    else if(within(x,y,80,254,280,297))changePage(DIAG);
  } else if(page==DISPLAY_SETTINGS){
    if(within(x,y,78,163,140,215)){brightness=max(40,(int)brightness-15);setBrightness();markSaved();displayValues();}
    else if(within(x,y,220,163,282,215)){brightness=min(255,(int)brightness+15);setBrightness();markSaved();displayValues();}
    else if(within(x,y,109,280,251,317))changePage(SETTINGS);
  } else if(page==KNOB_SETTINGS){
    if(within(x,y,80,96,280,133)){invertKnob=!invertKnob;markSaved();fresh=true;}
    else if(within(x,y,76,163,136,206)){knobDivider=max(1,(int)knobDivider-1);markSaved();knobValues();}
    else if(within(x,y,224,163,284,206)){knobDivider=min(4,(int)knobDivider+1);markSaved();knobValues();}
    else if(within(x,y,79,251,281,286)){
      noInterrupts();
      clockwiseRising=!clockwiseRising;
      lastPulseA=micros();
      interrupts();
      markSaved();fresh=true;
    }
    else if(within(x,y,112,293,248,327))changePage(SETTINGS);
  } else if(page==BATTERY){
    if(within(x,y,106,285,254,322))changePage(SETTINGS);
  } else if(page==DIAG){
    if(within(x,y,58,278,174,320)){
      noInterrupts();
      rawA=rawB=rawAFalling=rawARising=rawBFalling=rawBRising=0;
      interrupts();
      redrawDiagnostics();
    }else if(within(x,y,184,278,300,320))changePage(SETTINGS);
  }
}
void setup(){
  Serial.begin(115200);delay(260);load();
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT,ADC_11db);
  sampleBattery(true);
  pinMode(PIN_BL,OUTPUT);digitalWrite(PIN_BL,LOW);
  pinMode(PIN_A,INPUT_PULLUP);pinMode(PIN_B,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_A),knobClockwise,CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B),knobCounterclockwise,CHANGE);
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
      // Keep every counted pulse, including those accumulated while the
      // LCD is painting. Life limits are enforced inside adjustLife().
      if(page==GAME)adjustLife(players==1?0:selected,step);
      else if(page==COUNTERS)adjustCounter(step);
      else if(page==DISPLAY_SETTINGS){
        brightness=constrain((int)brightness+step*5,40,255);
        setBrightness();markSaved();displayValues();
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
    if(page==GAME){
      // Coalesce bursts of encoder events into at most ~40 numeric updates/s.
      // Encoder interrupts remain active and every step changes the model.
      const uint32_t now=millis();
      if(lifeDirty && (uint32_t)(now-lastLifePaintAt)>=25){
        uint8_t mask=lifeDirtyMask;
        lifeDirty=false;lifeDirtyMask=0;
        if(mask&0x01)lifeArea(0);
        if(players==2 && (mask&0x02))lifeArea(1);
        lastLifePaintAt=millis();
      }
      if(selectionDirty){
        // Only repaint labels when player focus actually changes.
        paintSelection();selectionDirty=false;
        lastSelectionPaintAt=millis();
      }
      updateKOAnimations();
    }
    else if(page==COUNTERS&&counterDirty){redrawCounter();counterDirty=false;}
    if(page==TIMER&&millis()-lastDiagnostics>300){lastDiagnostics=millis();repaintTimer();}
    if(page==DIAG&&millis()-lastDiagnostics>250){lastDiagnostics=millis();redrawDiagnostics();}
  }
  if(page==BATTERY && (uint32_t)(millis()-lastBatteryRead)>=2200) {
    sampleBattery(true);updateBatteryDisplay();
  } else if(page==DIAG) {
    sampleBattery();
  }
  if(needsSave&&millis()-lastChange>1800)persist();
  delay(2);
}
