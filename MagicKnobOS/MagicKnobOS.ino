#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <string.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "NetTypes.h"

// Magic Knob OS 2.0-alpha8 | ESP-NOW host / read-only spectator synchronization
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
enum Page : uint8_t {HOME, SETUP, GAME, GAME_MENU, FIRST_PLAYER, GAME_STATUS, COMMANDER_DAMAGE, HISTORY, RESET_CONFIRM, COUNTERS, TOOLS, ROLL, TIMER, SETTINGS, DISPLAY_SETTINGS, KNOB_SETTINGS, BATTERY, DIAG, NETWORK};
enum RollType : uint8_t {ROLL_D4, ROLL_D6, ROLL_D8, ROLL_D10, ROLL_D12, ROLL_D20, ROLL_D100, ROLL_COIN};
RollType rollType=ROLL_D20;
Page page=HOME;
enum CounterType : uint8_t {POISON, ENERGY, TAX, TREASURE, EXPERIENCE};
constexpr uint8_t COUNTER_COUNT=5;
CounterType selectedCounter=POISON;
uint8_t players=2, startLife=40, selected=0, brightness=210;
uint8_t activeTurn=0, firstCandidate=0, monarchOwner=255, initiativeOwner=255;
uint32_t turnSteps=0, turnStartedAt=0, lastTurnPaintAt=0;
enum UndoKind : uint8_t {UNDO_NONE, UNDO_LIFE, UNDO_COUNTER, UNDO_COMMANDER};
struct UndoRecord {
  UndoKind kind=UNDO_NONE;
  uint8_t player=0, previousSelected=0, source=0, target=0, slot=0;
  CounterType counter=POISON;
  int16_t before=0, previousLife=0;
  bool lifeApplied=false;
};
UndoRecord lastUndo;
uint8_t knobDivider=1;
bool invertKnob=false, playing=false, fresh=true, lifeDirty=false, selectionDirty=false, counterDirty=false;
uint8_t lifeDirtyMask=0x03;
uint8_t paintedSplitSelected=255;
struct KnockoutEffect {
  bool active=false;
  uint8_t frame=255;
  uint32_t started=0;
};
KnockoutEffect knockout[4];
int16_t life[4]={40,40,40,40}, poison[4]={0,0,0,0}, energy[4]={0,0,0,0};
int16_t tax[4]={0,0,0,0}, cmdDamage[4]={0,0,0,0}, treasure[4]={0,0,0,0};
int16_t experience[4]={0,0,0,0};
// Indexed by commander owner, commander slot A/B and defending player.
// Do not merge values from two commanders: 21 combat damage is per commander.
int16_t commanderMatrix[4][2][4]={};
bool partnerEnabled[4]={false,false,false,false};
bool commanderAutoLife=true;
uint8_t commanderSource=0, commanderTarget=1, commanderSlot=0;
enum EventKind : uint8_t {EV_START, EV_LIFE, EV_COUNTER, EV_COMMANDER, EV_TURN, EV_MONARCH, EV_INITIATIVE, EV_UNDO};
struct GameEvent {
  uint32_t seconds=0;
  EventKind kind=EV_START;
  uint8_t actor=0,target=0,slot=0;
  int16_t before=0,after=0;
};
constexpr uint8_t HISTORY_CAPACITY=32, HISTORY_ROWS=5;
GameEvent events[HISTORY_CAPACITY];
uint8_t historySize=0, historyNext=0, historyOffset=0;
uint32_t gameStartedAt=0;
void recordEvent(EventKind kind,uint8_t actor,uint8_t target,uint8_t slot,int16_t before,int16_t after){
  GameEvent &e=events[historyNext];
  e.seconds=(millis()-gameStartedAt)/1000;
  e.kind=kind;e.actor=actor;e.target=target;e.slot=slot;e.before=before;e.after=after;
  historyNext=(historyNext+1)%HISTORY_CAPACITY;
  if(historySize<HISTORY_CAPACITY)historySize++;
  historyOffset=0;
}
bool commanderKnockout(int target){
  for(int p=0;p<players;p++)for(int slot=0;slot<2;slot++){
    if(slot==1&&!partnerEnabled[p])continue;
    if(commanderMatrix[p][slot][target]>=21)return true;
  }
  return false;
}

bool needsSave=false;
// ESP-NOW Alpha 8: one authoritative host and one passive scoreboard
// mirror. Single Wi-Fi channel and bounded packets (<250 bytes).
// No password-based cryptography: four-digit matching code is a discovery
// convenience, NOT secure authentication. Do not use for sensitive data.
NetRole netRole=NET_OFF,netChoice=NET_HOST;
uint16_t netPin=0,netEntry=0;
uint8_t netDigit=0;
uint32_t netSession=0,netSendSequence=0,netReceivedSequence=0;
uint32_t netLastStateSent=0,netLastBeaconSent=0,netLastJoinSent=0,netLastSeen=0,netLastAckAt=0;
uint32_t netAckSequence=0;
bool netReady=false,netPaired=false,netDirty=false,netError=false;
uint8_t netRemoteMac[6]={};
NetPacket netLastSnapshot={};
NetState netLocalBackup={};
UndoRecord netUndoBackup={};
bool netBackupValid=false;
portMUX_TYPE netMux=portMUX_INITIALIZER_UNLOCKED;
IncomingNetPacket netQueue[4]={};
volatile uint8_t netHead=0,netTail=0;
volatile uint32_t netOverruns=0;

void onEspNowReceive(const uint8_t *mac,const uint8_t *data,int len){
  if(len!=(int)sizeof(NetPacket)||!mac||!data)return;
  NetPacket input;
  memcpy(&input,data,sizeof(input));
  if(input.magic!=NET_MAGIC||input.version!=NET_VERSION)return;
  if(input.type<NET_BEACON||input.type>NET_ACK)return;
  portENTER_CRITICAL(&netMux);
  uint8_t next=(netHead+1)%4;
  if(next!=netTail){
    netQueue[netHead].packet=input;
    memcpy(netQueue[netHead].mac,mac,6);
    netHead=next;
  }else netOverruns++;
  portEXIT_CRITICAL(&netMux);
}
bool netAddPeer(const uint8_t* mac){
  if(esp_now_is_peer_exist(mac))return true;
  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,mac,6);
  peer.channel=NET_CHANNEL;
  peer.ifidx=WIFI_IF_STA;
  peer.encrypt=false;
  return esp_now_add_peer(&peer)==ESP_OK;
}
NetState netCaptureState(){
  NetState state={};
  state.players=players;state.startLife=startLife;
  state.selected=selected;state.activeTurn=activeTurn;
  state.playing=playing;state.monarch=monarchOwner;
  state.initiative=initiativeOwner;state.autoLife=commanderAutoLife;
  state.turns=turnSteps;
  state.elapsedTurnMs=millis()-turnStartedAt;
  for(int p=0;p<4;p++){
    state.life[p]=life[p];state.poison[p]=poison[p];
    state.energy[p]=energy[p];state.tax[p]=tax[p];
    state.treasure[p]=treasure[p];state.experience[p]=experience[p];
    if(partnerEnabled[p])state.partnerMask|=(1<<p);
  }
  memcpy(state.commander,commanderMatrix,sizeof(commanderMatrix));
  return state;
}
bool netSend(NetMsgType type,const uint8_t* addr,uint32_t sequence,bool includeState){
  if(!netReady||!netAddPeer(addr))return false;
  NetPacket packet={};
  packet.magic=NET_MAGIC;packet.version=NET_VERSION;packet.type=type;
  packet.pairingCode=netPin;packet.session=netSession;packet.sequence=sequence;
  if(includeState)packet.state=netCaptureState();
  esp_err_t err=esp_now_send(addr,(const uint8_t*)&packet,sizeof(packet));
  if(type==NET_STATE && err==ESP_OK)netLastSnapshot=packet;
  netError=(err!=ESP_OK);
  return err==ESP_OK;
}
void netStop(){
  const bool restore=(netRole==NET_VIEWER && netBackupValid);
  if(netReady)esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  netReady=false;netRole=NET_OFF;netPaired=false;netError=false;
  netLastSeen=0;netLastAckAt=0;netDirty=false;netReceivedSequence=0;
  portENTER_CRITICAL(&netMux);
  netHead=netTail=0;
  portEXIT_CRITICAL(&netMux);
  if(restore){
    netApplyState(netLocalBackup);
    lastUndo=netUndoBackup;
    netBackupValid=false;
  }
}
bool netStart(NetRole role){
  if(needsSave)persist();
  netStop();
  if(role==NET_VIEWER){
    netLocalBackup=netCaptureState();
    netUndoBackup=lastUndo;
    netBackupValid=true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false,false);
  if(esp_wifi_set_channel(NET_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK){
    WiFi.mode(WIFI_OFF);netError=true;return false;
  }
  if(esp_now_init()!=ESP_OK){
    WiFi.mode(WIFI_OFF);netError=true;return false;
  }
  esp_now_register_recv_cb(onEspNowReceive);
  netReady=true;netRole=role;netError=false;netPaired=false;
  netSession=(role==NET_HOST)?esp_random():0;
  if(!netSession&&role==NET_HOST)netSession=1;
  netPin=role==NET_HOST?(uint16_t)(1000+(esp_random()%9000)):netEntry;
  netSendSequence=0;netReceivedSequence=0;netAckSequence=0;
  netLastBeaconSent=netLastJoinSent=netLastStateSent=0;
  netLastSeen=netLastAckAt=0;
  memset(netRemoteMac,0,sizeof(netRemoteMac));
  netDirty=true;
  return netAddPeer(NET_BROADCAST);
}
bool netPacketPop(IncomingNetPacket &out){
  bool got=false;
  portENTER_CRITICAL(&netMux);
  if(netTail!=netHead){
    out=netQueue[netTail];netTail=(netTail+1)%4;got=true;
  }
  portEXIT_CRITICAL(&netMux);
  return got;
}
void netApplyState(const NetState &state){
  // Never trust remote sizes or indices. The viewer is a read-only copy.
  if(state.players<1||state.players>4||state.activeTurn>=state.players||
     state.selected>=state.players)return;
  const bool major=(players!=state.players || playing!=(state.playing!=0) ||
       selected!=state.selected || activeTurn!=state.activeTurn ||
       turnSteps!=state.turns ||
       monarchOwner!=(state.monarch<state.players?state.monarch:255) ||
       initiativeOwner!=(state.initiative<state.players?state.initiative:255));
  bool oldKO[4];
  for(int p=0;p<4;p++)oldKO[p]=commanderKnockout(p);
  uint8_t changedMask=0;
  for(int p=0;p<4;p++){
    if(life[p]!=state.life[p]||poison[p]!=state.poison[p]||
       energy[p]!=state.energy[p]||tax[p]!=state.tax[p])
       changedMask|=(1<<p);
  }
  players=state.players;
  startLife=state.startLife==20?20:40;
  playing=state.playing!=0;
  selected=state.selected;activeTurn=state.activeTurn;
  monarchOwner=(state.monarch<players)?state.monarch:255;
  initiativeOwner=(state.initiative<players)?state.initiative:255;
  turnSteps=state.turns;turnStartedAt=millis()-state.elapsedTurnMs;
  commanderAutoLife=state.autoLife!=0;
  for(int p=0;p<4;p++){
    life[p]=constrain(state.life[p],-99,999);
    poison[p]=constrain(state.poison[p],0,999);
    energy[p]=constrain(state.energy[p],0,999);
    tax[p]=constrain(state.tax[p],0,999);
    treasure[p]=constrain(state.treasure[p],0,999);
    experience[p]=constrain(state.experience[p],0,999);
    partnerEnabled[p]=(state.partnerMask&(1<<p))!=0;
  }
  memcpy(commanderMatrix,state.commander,sizeof(commanderMatrix));
  for(int p=0;p<4;p++)for(int c=0;c<2;c++)for(int t=0;t<4;t++)
    commanderMatrix[p][c][t]=constrain(commanderMatrix[p][c][t],0,999);
  for(int p=0;p<players;p++)if(oldKO[p]!=commanderKnockout(p))
    changedMask|=(1<<p);
  // Do not persist remote data or create local history.
  clearUndo();
  if(page==NETWORK)return; // leave the pairing/status controls accessible
  if(page!=GAME || major){
    changePage(GAME);
  }else if(changedMask){
    lifeDirty=true;lifeDirtyMask|=changedMask;
  }
}
void netHandlePacket(const IncomingNetPacket &received){
  const NetPacket &packet=received.packet;
  if(!netReady||packet.pairingCode!=netPin)return;
  if(netRole==NET_HOST){
    if(packet.type==NET_JOIN){
      if(netPaired && memcmp(netRemoteMac,received.mac,6)!=0)return;
      if(!netAddPeer(received.mac))return;
      memcpy(netRemoteMac,received.mac,6);
      netPaired=true;netLastSeen=millis();netDirty=true;
    }else if(packet.type==NET_ACK&&netPaired&&
             packet.session==netSession&&
             memcmp(netRemoteMac,received.mac,6)==0){
      if(packet.sequence>=netAckSequence){
        netAckSequence=packet.sequence;netLastAckAt=millis();
      }
      netLastSeen=millis();
    }
  }else if(netRole==NET_VIEWER&&packet.type==NET_STATE&&packet.session!=0){
    if(netPaired && (memcmp(netRemoteMac,received.mac,6)!=0 ||
                     packet.session!=netSession))return;
    if(!netPaired){
      if(!netAddPeer(received.mac))return;
      memcpy(netRemoteMac,received.mac,6);
      netSession=packet.session;netPaired=true;netReceivedSequence=0;
    }
    netLastSeen=millis();
    if(packet.sequence>netReceivedSequence){
      netReceivedSequence=packet.sequence;
      netApplyState(packet.state);
    }
    netSend(NET_ACK,netRemoteMac,netReceivedSequence,false);
  }
}
void netPoll(){
  if(!netReady)return;
  IncomingNetPacket item;
  for(int i=0;i<4&&netPacketPop(item);i++)netHandlePacket(item);
  const uint32_t now=millis();
  if(netRole==NET_HOST){
    if(!netPaired && (!netLastBeaconSent||(uint32_t)(now-netLastBeaconSent)>=1200)){
      netLastBeaconSent=now;netSend(NET_BEACON,NET_BROADCAST,0,false);
    }
    if(netPaired && (netDirty||
        (uint32_t)(now-netLastStateSent)>=1100)){
      if((uint32_t)(now-netLastStateSent)>=120){
        netLastStateSent=now;
        netSendSequence++;
        if(netSend(NET_STATE,netRemoteMac,netSendSequence,true))
          netDirty=false;
      }
    }
    if(netPaired && (uint32_t)(now-netLastSeen)>8000){
      netPaired=false;
      netDirty=true;
    }
  }else if(netRole==NET_VIEWER){
    if(!netPaired && (!netLastJoinSent||(uint32_t)(now-netLastJoinSent)>=1200)){
      netLastJoinSent=now;
      netSend(NET_JOIN,NET_BROADCAST,0,false);
    }
    if(netPaired && (uint32_t)(now-netLastSeen)>=4500){
      netPaired=false;netSession=0;netReceivedSequence=0;
      changePage(NETWORK);
    }
  }
}
void netChangeDigit(int amount){
  static const int powers[]={1000,100,10,1};
  const int power=powers[netDigit];
  const int previous=(netEntry/power)%10;
  const int next=(previous+amount%10+10)%10;
  netEntry=(uint16_t)(netEntry+(next-previous)*power);
}

uint32_t lastChange=0, lastTouch=0, lastDiagnostics=0;
uint32_t lastBatteryRead=0;
float batteryVolts=0.0f;
bool batterySampleValid=false;
uint32_t lastLifePaintAt=0;
uint32_t lastSelectionPaintAt=0;
volatile int32_t knobDelta=0;
volatile uint32_t rawA=0, rawB=0;
volatile uint32_t rawAFalling=0, rawARising=0, rawBFalling=0, rawBRising=0;
volatile uint32_t lastPulseA=0, lastPulseB=0;
volatile uint32_t lastAcceptedUs=0, rejectedCrossA=0, rejectedCrossB=0;
volatile uint32_t rejectedBounceA=0, rejectedBounceB=0;
volatile uint8_t lastAcceptedDir=0; // 0 none, 1 right, 2 left
volatile uint8_t crossLockMs=16; // Adjustable: 0, 8, 16, 32 ms
portMUX_TYPE knobMux=portMUX_INITIALIZER_UNLOCKED;
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

// The two GPIO pins are separate directional pulse lines, not a quadrature
// encoder. Count both signal edges for diagnostics; only the configured active
// edge enters the lifetime filter. Rejections never reach knobDelta.
// Cross-direction suppression is a hypothesis to test on actual hardware,
// not a replacement for the independent 2400 us same-channel debounce.
constexpr uint32_t PULSE_DEBOUNCE_US=2400;
void IRAM_ATTR countClockwiseEdge(uint32_t t) {
  portENTER_CRITICAL_ISR(&knobMux);
  if((uint32_t)(t-lastPulseA)<PULSE_DEBOUNCE_US){
    rejectedBounceA++;
  }else{
    lastPulseA=t;
    if(lastAcceptedDir==2 && crossLockMs>0 &&
       (uint32_t)(t-lastAcceptedUs)<(uint32_t)crossLockMs*1000U){
      rejectedCrossA++;
    }else{
      lastAcceptedDir=1;lastAcceptedUs=t;rawA++;knobDelta++;
    }
  }
  portEXIT_CRITICAL_ISR(&knobMux);
}
void IRAM_ATTR countCounterclockwiseEdge(uint32_t t) {
  portENTER_CRITICAL_ISR(&knobMux);
  if((uint32_t)(t-lastPulseB)<PULSE_DEBOUNCE_US){
    rejectedBounceB++;
  }else{
    lastPulseB=t;
    if(lastAcceptedDir==1 && crossLockMs>0 &&
       (uint32_t)(t-lastAcceptedUs)<(uint32_t)crossLockMs*1000U){
      rejectedCrossB++;
    }else{
      lastAcceptedDir=2;lastAcceptedUs=t;rawB++;knobDelta--;
    }
  }
  portEXIT_CRITICAL_ISR(&knobMux);
}
void IRAM_ATTR knobClockwise(){
  const uint32_t t=micros();
  const bool high=gpio_get_level((gpio_num_t)PIN_A)!=0;
  portENTER_CRITICAL_ISR(&knobMux);
  if(high)rawARising++;else rawAFalling++;
  const bool active=high==clockwiseRising;
  portEXIT_CRITICAL_ISR(&knobMux);
  if(active)countClockwiseEdge(t);
}
void IRAM_ATTR knobCounterclockwise(){
  const uint32_t t=micros();
  const bool high=gpio_get_level((gpio_num_t)PIN_B)!=0;
  portENTER_CRITICAL_ISR(&knobMux);
  if(high)rawBRising++;else rawBFalling++;
  portEXIT_CRITICAL_ISR(&knobMux);
  if(!high)countCounterclockwiseEdge(t);
}
void markSaved() {
  if(netRole==NET_VIEWER)return; // mirror never overwrites its own game in NVS
  needsSave=true;lastChange=millis();
  if(netRole==NET_HOST)netDirty=true;
}
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
  if(page!=HOME&&page!=NETWORK)
    centered(netRole==NET_VIEWER?"TOCCA: RETE":"SWIPE > HOME",342,1,MK_FAINT);
}
void changePage(Page next) {
  if(next!=GAME)for(auto &fx:knockout)fx.active=false;
  page=next;fresh=true;lifeDirty=true;lifeDirtyMask=0x0f;
  selectionDirty=true;counterDirty=true;
}
void persist() {
  store.putUChar("players",players);store.putUChar("initial",startLife);
  store.putUChar("bright",brightness);store.putUChar("pulseDiv",knobDivider);
  store.putBool("reverse",invertKnob);store.putBool("cwRise",clockwiseRising);store.putBool("playing",playing);
  store.putUChar("crossMs",crossLockMs);
  store.putUChar("focused",selected);store.putUChar("turnPlayer",activeTurn);
  store.putUInt("turnSteps",turnSteps);store.putUChar("monarch",monarchOwner);
  store.putUChar("initiative",initiativeOwner);
  store.putBool("cmdAutoLife",commanderAutoLife);
  uint8_t mask=0;
  for(int p=0;p<4;p++)if(partnerEnabled[p])mask|=(1<<p);
  store.putUChar("cmdPartners",mask);
  store.putBytes("cmdMatrix",commanderMatrix,sizeof(commanderMatrix));
  for(int p=0;p<4;p++){
    String suffix=String(p);
    store.putShort(("life"+suffix).c_str(),life[p]);
    store.putShort(("poison"+suffix).c_str(),poison[p]);
    store.putShort(("energy"+suffix).c_str(),energy[p]);
    store.putShort(("tax"+suffix).c_str(),tax[p]);
    store.putShort(("cmd"+suffix).c_str(),cmdDamage[p]);
    store.putShort(("treasure"+suffix).c_str(),treasure[p]);
    store.putShort(("experience"+suffix).c_str(),experience[p]);
  }
  needsSave=false;
}
void load() {
  store.begin("magic-os",false);
  players=constrain((int)store.getUChar("players",2),1,4);
  startLife=store.getUChar("initial",40)==20?20:40;
  brightness=constrain(store.getUChar("bright",210),40,255);
  knobDivider=constrain((int)store.getUChar("pulseDiv",1),1,4);
  invertKnob=store.getBool("reverse",false);
  clockwiseRising=store.getBool("cwRise",true);
  uint8_t crossSetting=store.getUChar("crossMs",16);
  crossLockMs=(crossSetting==0||crossSetting==8||crossSetting==16||crossSetting==32)?crossSetting:16;
  playing=store.getBool("playing",false);
  selected=constrain((int)store.getUChar("focused",0),0,(int)players-1);
  activeTurn=constrain((int)store.getUChar("turnPlayer",0),0,(int)players-1);
  turnSteps=store.getUInt("turnSteps",0);
  monarchOwner=store.getUChar("monarch",255);
  initiativeOwner=store.getUChar("initiative",255);
  commanderAutoLife=store.getBool("cmdAutoLife",true);
  uint8_t pmask=store.getUChar("cmdPartners",0);
  for(int p=0;p<4;p++)partnerEnabled[p]=(pmask&(1<<p))!=0;
  if(store.getBytesLength("cmdMatrix")==sizeof(commanderMatrix))
    store.getBytes("cmdMatrix",commanderMatrix,sizeof(commanderMatrix));
  for(int p=0;p<4;p++)for(int q=0;q<2;q++)for(int t=0;t<4;t++)
    commanderMatrix[p][q][t]=constrain(commanderMatrix[p][q][t],0,999);
  commanderSource=0;commanderTarget=players>1?1:0;commanderSlot=0;
  if(monarchOwner>=players)monarchOwner=255;
  if(initiativeOwner>=players)initiativeOwner=255;
  for(int p=0;p<4;p++){
    String suffix=String(p);
    life[p]=constrain(store.getShort(("life"+suffix).c_str(),startLife),-99,999);
    poison[p]=constrain(store.getShort(("poison"+suffix).c_str(),0),0,99);
    energy[p]=constrain(store.getShort(("energy"+suffix).c_str(),0),0,999);
    tax[p]=constrain(store.getShort(("tax"+suffix).c_str(),0),0,999);
    cmdDamage[p]=constrain(store.getShort(("cmd"+suffix).c_str(),0),0,999);
    treasure[p]=constrain(store.getShort(("treasure"+suffix).c_str(),0),0,999);
    experience[p]=constrain(store.getShort(("experience"+suffix).c_str(),0),0,999);
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

void clearUndo(){lastUndo.kind=UNDO_NONE;}
void newGame() {
  memset(commanderMatrix,0,sizeof(commanderMatrix));
  for(int p=0;p<4;p++)partnerEnabled[p]=false;
  commanderSource=0;commanderTarget=players>1?1:0;commanderSlot=0;
  historySize=0;historyNext=0;historyOffset=0;gameStartedAt=millis();
  for(int i=0;i<4;i++){
    life[i]=startLife;poison[i]=0;energy[i]=0;tax[i]=0;cmdDamage[i]=0;
    treasure[i]=0;experience[i]=0;knockout[i].active=false;
  }
  selected=0;activeTurn=0;turnSteps=0;turnStartedAt=millis();
  monarchOwner=initiativeOwner=255;clearUndo();
  playing=true;
  firstCandidate=(uint8_t)(esp_random()%players);
  markSaved();changePage(FIRST_PLAYER);
}
void chooseFirstPlayer(){
  activeTurn=firstCandidate;selected=activeTurn;
  turnSteps=0;turnStartedAt=millis();
  recordEvent(EV_START,activeTurn,activeTurn,0,0,1);
  markSaved();changePage(GAME);
}
void nextTurn(){
  if(!playing)return;
  uint8_t previous=activeTurn;
  activeTurn=(activeTurn+1)%players;
  turnSteps++;
  selected=activeTurn;turnStartedAt=millis();
  recordEvent(EV_TURN,activeTurn,activeTurn,0,previous,activeTurn);
  markSaved();changePage(GAME);
}
void drawTurnLine(){
  if(page!=GAME)return;
  lcd->fillRect(64,50,232,12,MK_DARK);
  uint32_t seconds=(millis()-turnStartedAt)/1000;
  char line[44];
  snprintf(line,sizeof(line),"P%u  GIRO %lu  %02lu:%02lu",
           activeTurn+1,(unsigned long)(1+turnSteps/players),
           (unsigned long)((seconds/60)%100),(unsigned long)(seconds%60));
  centered(line,52,1,MK_GOLD);
  lastTurnPaintAt=millis();
}
void drawOwners(){
  if(page!=GAME)return;
  char s[50],m[10],i[10];
  if(monarchOwner==255)strcpy(m,"-");else snprintf(m,sizeof(m),"P%u",monarchOwner+1);
  if(initiativeOwner==255)strcpy(i,"-");else snprintf(i,sizeof(i),"P%u",initiativeOwner+1);
  snprintf(s,sizeof(s),"MONARCA:%s  INIZIATIVA:%s",m,i);
  // Reserve the status strip below the bottom buttons (1/3/4-player UI).
  lcd->fillRect(54,319,252,10,MK_DARK);
  centered(s,320,1,MK_FAINT);
  if(netRole!=NET_OFF)
    printAt(netRole==NET_HOST?"H":"V",61,320,1,
            netPaired?MK_GREEN:MK_AMBER);
}
void drawHome(){
  base("MAGIC KNOB OS");centered("2.0 ALPHA 8",77,2,MK_FAINT);
  uiButton(90,111,180,54,"MAGIC",3,0x10c4);
  uiButton(90,174,180,42,"NUOVA",2);
  uiButton(90,222,180,42,"STRUMENTI",2);
  uiButton(90,271,180,42,"IMPOSTAZ.",2);
}
void drawSetup(){
  base("NUOVA PARTITA");
  centered("Giocatori",77,2,MK_FAINT);
  for(int p=0;p<4;p++){
    char n[3];snprintf(n,sizeof(n),"%u",p+1);
    int x=(p%2==0)?75:189, y=(p<2)?104:158;
    uiButton(x,y,96,44,n,3,players==p+1?MK_BLUE:MK_PANEL);
  }
  centered("Punti vita",209,2,MK_FAINT);
  uiButton(72,232,100,43,"20",2,startLife==20?MK_BLUE:MK_PANEL);
  uiButton(188,232,100,43,"40",2,startLife==40?MK_AMBER:MK_PANEL);
  uiButton(105,287,150,42,"INIZIA",2,MK_GREEN);
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
  if(life[p]>0&&!commanderKnockout(p))return;
  lcd->fillRoundRect(251,y,39,21,7,color);
  printAt("KO",263,y+7,1,MK_DARK);
}
// 3 players: a top arch-like card and two lower cards; 4 players: four
// balanced regions. All card bounds stay inside the visible 360px circle.
bool splitCardBounds(int p,int &x,int &y,int &w,int &h){
  if(p<0||p>=players||players<3)return false;
  if(players==3){
    if(p==0){x=97;y=72;w=166;h=91;}
    else {x=p==1?47:189;y=196;w=124;h=98;}
  }else{
    x=(p%2)?189:47;
    y=(p<2)?77:196;
    w=124;h=98;
  }
  return true;
}
void splitChangeButton(int x,int y,bool plus,uint16_t accent){
  lcd->fillCircle(x,y,13,MK_DARK);
  lcd->drawCircle(x,y,13,accent);
  lcd->fillRect(x-7,y-1,14,3,MK_WHITE);
  if(plus)lcd->fillRect(x-1,y-7,3,14,MK_WHITE);
}
void drawSplitCard(int p){
  if(page!=GAME)return;
  int x,y,w,h;
  if(!splitCardBounds(p,x,y,w,h))return;
  uint16_t accents[4]={MK_BLUE,MK_AMBER,MK_GREEN,MK_RED};
  uint16_t accent=commanderKnockout(p)?MK_RED:accents[p];
  lcd->fillRoundRect(x,y,w,h,13,MK_PANEL);
  lcd->drawRoundRect(x,y,w,h,13,selected==p?MK_GOLD:accent);
  if(selected==p)lcd->drawRoundRect(x+2,y+2,w-4,h-4,11,MK_GOLD);
  char b[24];snprintf(b,sizeof(b),"P%d%s",p+1,activeTurn==p?"*":"");
  printAt(b,x+(w-(int)strlen(b)*12)/2,y+9,2,selected==p?MK_GOLD:accent);
  snprintf(b,sizeof(b),"%d",life[p]);
  int digits=strlen(b);
  int scale=digits<=2?5:(digits==3?4:3);
  int textX=x+(w-digits*6*scale)/2;
  printAt(b,textX,y+34,scale,life[p]<=0||commanderKnockout(p)?MK_RED:MK_WHITE);
  splitChangeButton(x+16,y+55,false,MK_RED);
  splitChangeButton(x+w-16,y+55,true,MK_GREEN);
  snprintf(b,sizeof(b),"V%d E%d T%d",poison[p],energy[p],tax[p]);
  int statsY=(players==3&&p==0)?y+74:y+82;
  printAt(b,x+(w-(int)strlen(b)*6)/2,statsY,1,MK_FAINT);
}
void drawSplitMenu(){
  // Repaint this after a card update: card corners overlap its circular edge.
  lcd->fillCircle(180,184,25,MK_DARK);
  lcd->drawCircle(180,184,24,MK_GOLD);
  printAt("MENU",168,180,1,MK_GOLD);
}
void drawSplitGame(){
  base("MAGIC LIFE");
  if(players==4)lcd->drawFastVLine(180,75,229,MK_PANEL2);
  lcd->drawFastHLine(53,184,254,MK_PANEL2);
  for(int p=0;p<players;p++)drawSplitCard(p);
  paintedSplitSelected=selected;
  drawSplitMenu();
}
void lifeArea(int p){
  char buf[16];snprintf(buf,sizeof(buf),"%d",life[p]);
  if(players>=3){drawSplitCard(p);drawSplitMenu();return;}
  if(players!=2){
    if(p!=selected)return;
    // Update both large life total and the selected player's overview chip.
    int w=players==1?110:76,gap=players==1?0:4;
    int left=(360-(players*w+(players-1)*gap))/2;
    char chip[20];snprintf(chip,sizeof(chip),"P%d:%d",p+1,life[p]);
    uiButton(left+p*(w+gap),91,w,37,chip,1,
             commanderKnockout(p)?MK_RED:MK_BLUE);
    lcd->fillRect(92,173,176,85,MK_DARK);
    int digits=strlen(buf);
    int size=digits<=2?9:(digits==3?7:5);
    int x=180-(digits*6*size)/2;
    int y=212-(8*size)/2;
    printAt(buf,x,y,size,life[p]<=0?MK_RED:(life[p]<=5?MK_GOLD:MK_BLUE));
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
  if(players>=3){
    if(paintedSplitSelected<players && paintedSplitSelected!=selected)
      drawSplitCard(paintedSplitSelected);
    drawSplitCard(selected);
    paintedSplitSelected=selected;
    drawSplitMenu();
    return;
  }
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
  if(players>=3)drawSplitGame();
  else if(players!=2){
    int buttonW=players==1?110:76;
    int gap=players==1?0:4;
    int total=players*buttonW+(players-1)*gap;
    int offset=(360-total)/2;
    for(int p=0;p<players;p++){
      char b[20];
      snprintf(b,sizeof(b),"P%d:%d",p+1,life[p]);
      uiButton(offset+p*(buttonW+gap),91,buttonW,37,b,1,
               commanderKnockout(p)?MK_RED:(selected==p?MK_BLUE:MK_PANEL));
    }
    char focus[22];snprintf(focus,sizeof(focus),"GIOCATORE %u",selected+1);
    centered(focus,145,2,MK_GOLD);
    plusMinus(53,214,false,MK_RED);plusMinus(307,214,true,MK_GREEN);
    uiButton(61,276,107,40,"SEGNALI",1);
    uiButton(192,276,107,40,"MENU",2);
    lifeArea(selected);
  }else{
    duelPanel(0);duelPanel(1);
    lcd->drawFastHLine(89,194,182,MK_GOLD);
    uiButton(58,178,79,32,"SEGN.",2,MK_PANEL);
    uiButton(142,178,78,32,"TURNO",2,MK_PANEL);
    uiButton(225,178,77,32,"MENU",2,MK_PANEL);
    lifeArea(0);lifeArea(1);
    paintSelection();
  }
  if(players==1 && commanderKnockout(selected))
    centered("KO: DANNI COMMANDER",264,1,MK_RED);
  drawTurnLine();drawOwners();
}

void drawGameMenu(){
  base("MENU PARTITA");
  const char* labels[10]={"COMMANDER","STORICO","DADI","STATO","TURNO","PRIMO","ANNULLA","RESET","HOME","PARTITA"};
  for(int i=0;i<10;i++){
    int x=i%2?187:59, y=76+(i/2)*51;
    uint16_t color=i==6?(lastUndo.kind==UNDO_NONE?MK_PANEL:MK_GREEN):
                    (i==7?MK_RED:(i==9?MK_BLUE:MK_PANEL2));
    uiButton(x,y,112,40,labels[i],i==0||i==6||i==9?1:2,color);
  }
}
void drawCommanderValue(){
  if(page!=COMMANDER_DAMAGE)return;
  lcd->fillRect(111,232,138,75,MK_DARK);
  int value=commanderMatrix[commanderSource][commanderSlot][commanderTarget];
  char buf[18];snprintf(buf,sizeof(buf),"%d",value);
  int digits=strlen(buf);
  int size=digits>=3?6:7;
  centered(buf,244,size,value>=21?MK_RED:MK_WHITE);
  if(value>=21)centered("KO CMD: 21+",294,1,MK_RED);
  else centered("21 = SCONFITTA",294,1,MK_FAINT);
}
void drawCommanderDamage(){
  base("DANNI COMMANDER");
  centered("COMANDANTE DEL GIOCATORE",72,1,MK_FAINT);
  for(int p=0;p<players;p++){
    char b[4];snprintf(b,sizeof(b),"P%d",p+1);
    uiButton(36+p*74,94,68,32,b,2,p==commanderSource?MK_BLUE:MK_PANEL);
  }
  uiButton(56,137,118,35,commanderSlot?"CMD B":"CMD A",2);
  uiButton(187,137,118,35,partnerEnabled[commanderSource]?"PARTNER SI":"PARTNER NO",1,
           partnerEnabled[commanderSource]?MK_GREEN:MK_PANEL);
  centered("SUBISCE IL DANNO",179,1,MK_FAINT);
  for(int p=0;p<players;p++){
    char b[4];snprintf(b,sizeof(b),"P%d",p+1);
    uiButton(36+p*74,195,68,32,b,2,p==commanderTarget?MK_AMBER:MK_PANEL);
  }
  plusMinus(70,264,false,MK_RED);plusMinus(290,264,true,MK_GREEN);
  drawCommanderValue();
  uiButton(92,308,82,29,commanderAutoLife?"VITE SI":"VITE NO",1,
           commanderAutoLife?MK_GREEN:MK_PANEL2);
  uiButton(187,308,82,29,"MENU",2);
}
void drawHistory(){
  base("CRONOLOGIA");
  char summary[35];
  snprintf(summary,sizeof(summary),"%u azioni - sessione",historySize);
  centered(summary,68,1,MK_FAINT);
  if(historySize==0)centered("NESSUNA AZIONE",160,2,MK_FAINT);
  for(int i=0;i<HISTORY_ROWS;i++){
    if(i+historyOffset>=historySize)break;
    int index=(historyNext+HISTORY_CAPACITY-1-historyOffset-i)%HISTORY_CAPACITY;
    const GameEvent &ev=events[index];
    int y=85+i*38;
    char elapsed[32];
    snprintf(elapsed,sizeof(elapsed),"%02lu:%02lu",
             (unsigned long)((ev.seconds/60)%100),(unsigned long)(ev.seconds%60));
    printAt(elapsed,74,y,1,MK_FAINT);
    char label[64]={};
    switch(ev.kind){
      case EV_START:snprintf(label,sizeof(label),"INIZIA: P%u",ev.actor+1);break;
      case EV_LIFE:snprintf(label,sizeof(label),"P%u VITE %d > %d",ev.actor+1,ev.before,ev.after);break;
      case EV_COUNTER:snprintf(label,sizeof(label),"P%u %s %d>%d",ev.actor+1,
                 ev.slot==POISON?"VEL":ev.slot==ENERGY?"ENE":ev.slot==TAX?"TAX":
                 ev.slot==TREASURE?"TES":"ESP",ev.before,ev.after);break;
      case EV_COMMANDER:snprintf(label,sizeof(label),"CMD P%u%c > P%u  %d>%d",
                  ev.actor+1,ev.slot?'B':'A',ev.target+1,ev.before,ev.after);break;
      case EV_TURN:snprintf(label,sizeof(label),"TURNO P%u",ev.actor+1);break;
      case EV_MONARCH:
        if(ev.after==255)strcpy(label,"MONARCA: NESSUNO");
        else snprintf(label,sizeof(label),"MONARCA: P%d",ev.after+1);
        break;
      case EV_INITIATIVE:
        if(ev.after==255)strcpy(label,"INIZIATIVA: NESSUNO");
        else snprintf(label,sizeof(label),"INIZIATIVA: P%d",ev.after+1);
        break;
      default:snprintf(label,sizeof(label),"ANNULLATA ULTIMA MODIFICA");break;
    }
    printAt(label,74,y+13,1,ev.kind==EV_COMMANDER?MK_AMBER:MK_WHITE);
    lcd->drawFastHLine(74,y+29,212,MK_PANEL2);
  }
  uiButton(61,284,113,31,"PRECEDENTI",1);
  uiButton(186,284,113,31,"RECENTI",1);
  uiButton(117,317,126,28,"MENU",2);
}
void drawFirstPlayer(){
  base("CHI INIZIA?");
  centered("ESTRAZIONE CASUALE",84,1,MK_FAINT);
  char n[5];snprintf(n,sizeof(n),"P%u",firstCandidate+1);
  centered(n,134,10,MK_GOLD);
  uiButton(64,258,109,49,"LANCIA",2);
  uiButton(187,258,109,49,"CONFERMA",1,MK_GREEN);
}
void drawGameStatus(){
  base("STATO PARTITA");
  centered("ASSEGNA A GIOCATORE",73,1,MK_FAINT);
  int w=players==1?100:69, gap=players==1?0:6;
  int total=players*w+(players-1)*gap, start=(360-total)/2;
  for(int p=0;p<players;p++){
    char n[4];snprintf(n,sizeof(n),"P%d",p+1);
    uiButton(start+p*(w+gap),96,w,39,n,2,
             p==selected?MK_BLUE:MK_PANEL);
  }
  centered("MONARCA",157,2,MK_GOLD);
  char m[30];if(monarchOwner==255)strcpy(m,"NESSUNO");
  else snprintf(m,sizeof(m),"P%u",monarchOwner+1);
  uiButton(96,178,168,38,m,2,monarchOwner==selected?MK_GREEN:MK_PANEL);
  centered("INIZIATIVA",226,2,MK_GOLD);
  char t[30];if(initiativeOwner==255)strcpy(t,"NESSUNO");
  else snprintf(t,sizeof(t),"P%u",initiativeOwner+1);
  uiButton(96,247,168,38,t,2,initiativeOwner==selected?MK_GREEN:MK_PANEL);
  uiButton(110,297,140,34,"PARTITA",2);
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
  case TAX:return "TASSA CMD";case TREASURE:return "TESORI";
  default:return "ESPERIENZA";
}}
int16_t& counterFor(int p,CounterType type){
  switch(type){
    case POISON:return poison[p];case ENERGY:return energy[p];
    case TAX:return tax[p];case TREASURE:return treasure[p];
    default:return experience[p];
  }
}
int16_t &currentCounter(){return counterFor(selected,selectedCounter);}
void undoLast(){
  if(lastUndo.kind==UNDO_NONE)return;
  const UndoRecord u=lastUndo;clearUndo();
  selected=u.previousSelected<players?u.previousSelected:0;
  if(u.kind==UNDO_LIFE){
    life[u.player]=u.before;
    knockout[u.player].active=false;
    markSaved();recordEvent(EV_UNDO,u.player,u.player,0,0,0);
    changePage(GAME);
  }else if(u.kind==UNDO_COUNTER){
    counterFor(u.player,u.counter)=u.before;
    selected=u.player;selectedCounter=u.counter;
    markSaved();recordEvent(EV_UNDO,u.player,u.player,0,0,0);
    changePage(COUNTERS);
  }else if(u.kind==UNDO_COMMANDER){
    commanderMatrix[u.source][u.slot][u.target]=u.before;
    if(u.lifeApplied)life[u.target]=u.previousLife;
    commanderSource=u.source;commanderTarget=u.target;commanderSlot=u.slot;
    markSaved();recordEvent(EV_UNDO,u.source,u.target,u.slot,0,0);
    changePage(COMMANDER_DAMAGE);
  }
}
void adjustCommanderDamage(int amount){
  if(!playing||commanderSource>=players||commanderTarget>=players)return;
  if(commanderSlot==1&&!partnerEnabled[commanderSource])return;
  int16_t &value=commanderMatrix[commanderSource][commanderSlot][commanderTarget];
  int before=value,after=constrain(before+amount,0,999);
  if(before==after)return;
  lastUndo.kind=UNDO_COMMANDER;lastUndo.source=commanderSource;
  lastUndo.target=commanderTarget;lastUndo.slot=commanderSlot;
  lastUndo.before=before;lastUndo.previousLife=life[commanderTarget];
  lastUndo.previousSelected=selected;lastUndo.lifeApplied=commanderAutoLife;
  value=after;
  if(commanderAutoLife){
    life[commanderTarget]=constrain((int)life[commanderTarget]-(after-before),-99,999);
    lifeDirty=true;lifeDirtyMask|=(1<<commanderTarget);
  }
  recordEvent(EV_COMMANDER,commanderSource,commanderTarget,commanderSlot,before,after);
  markSaved();drawCommanderValue();
}
void drawCounters(){
  base("SEGNALINI");
  centered("SCEGLI GIOCATORE",75,1,MK_FAINT);
  int w=players==1?110:70,gap=players==1?0:6;
  int x0=(360-(players*w+(players-1)*gap))/2;
  for(int p=0;p<players;p++){
    char buf[5];snprintf(buf,sizeof(buf),"P%d",p+1);
    uiButton(x0+p*(w+gap),94,w,39,buf,2,p==selected?MK_BLUE:MK_PANEL);
  }
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

const char* rollName(){
  switch(rollType){
    case ROLL_D4:return "DADO D4";case ROLL_D6:return "DADO D6";
    case ROLL_D8:return "DADO D8";case ROLL_D10:return "DADO D10";
    case ROLL_D12:return "DADO D12";case ROLL_D20:return "DADO D20";
    case ROLL_D100:return "DADO D100";default:return "MONETA";
  }
}
void drawTools() {
  base("DADI E STRUMENTI");
  const char* labels[9]={"D4","D6","D8","D10","D12","D20","D100","MONETA","TIMER"};
  for(int i=0;i<9;i++){
    int x=45+(i%3)*91,y=95+(i/3)*57;
    uiButton(x,y,84,44,labels[i],i==7?1:2);
  }
  uiButton(110,286,140,40,"INDIETRO",1);
}
void throwTool() {
  if(rollType==ROLL_COIN)randomValue=esp_random()&1;
  else{
    uint32_t sides=6;
    switch(rollType){
      case ROLL_D4:sides=4;break;case ROLL_D6:sides=6;break;
      case ROLL_D8:sides=8;break;case ROLL_D10:sides=10;break;
      case ROLL_D12:sides=12;break;case ROLL_D20:sides=20;break;
      case ROLL_D100:sides=100;break;default:break;
    }
    randomValue=(esp_random()%sides)+1;
  }
  fresh=true;
}
void drawRoll() {
  base(rollName());
  lcd->drawCircle(180,172,104,MK_GOLD);
  lcd->drawCircle(180,172,98,MK_PANEL2);
  char result[20];
  if(rollType==ROLL_COIN){
    strcpy(result,randomValue?"TESTA":"CROCE");
    centered(result,146,5,MK_WHITE);
  }else{
    snprintf(result,sizeof(result),"%lu",(unsigned long)randomValue);
    centered(result,126,strlen(result)>=3?9:12,MK_WHITE);
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
  uiButton(80,75,200,39,"SCHERMO",2);
  uiButton(80,122,200,39,"GHIERA",2);
  uiButton(80,169,200,39,"ALIMENTAZIONE",1);
  uiButton(80,216,200,39,"DIAGNOSTICA",2);
  uiButton(80,263,200,39,"RETE ESP-NOW",2);
}
void drawNetwork(){
  base("RETE ESP-NOW");
  centered("UN PRINCIPALE + UN VISORE",72,1,MK_FAINT);
  uiButton(72,94,104,39,"HOST",2,(netRole?netRole:netChoice)==NET_HOST?MK_BLUE:MK_PANEL);
  uiButton(184,94,104,39,"VISORE",2,(netRole?netRole:netChoice)==NET_VIEWER?MK_AMBER:MK_PANEL);
  centered("CODICE ABBINAMENTO",142,1,MK_FAINT);
  const uint16_t value=netRole==NET_HOST?netPin:netEntry;
  for(int i=0;i<4;i++){
    int div=i==0?1000:(i==1?100:(i==2?10:1));
    char b[2];
    if(netRole==NET_OFF && netChoice==NET_HOST)strcpy(b,"-");
    else snprintf(b,sizeof(b),"%u",(value/div)%10);
    int x=77+i*52;
    uint16_t c=(netRole==NET_OFF&&netChoice==NET_VIEWER&&netDigit==i)?MK_GOLD:MK_PANEL2;
    uiButton(x,156,46,42,b,3,c);
  }
  uiButton(69,207,74,34,"-",3);
  uiButton(217,207,74,34,"+",3);
  centered(netRole==NET_OFF&&netChoice==NET_VIEWER?"TOCCA CIFRA, POI +/-":
           "CODICE: DOPO AVVIO",245,1,MK_FAINT);
  uiButton(100,254,160,41,netRole==NET_OFF?"AVVIA":"FERMA",2,
           netRole==NET_OFF?MK_GREEN:MK_RED);
  char msg[48];
  if(netError)strcpy(msg,"ERRORE RADIO");
  else if(netRole==NET_OFF)strcpy(msg,"SPENTO  |  CANALE 1");
  else if(netRole==NET_HOST)
    snprintf(msg,sizeof(msg),"%s  ACK %lu",netPaired?"COLLEGATO":"IN ATTESA",
             (unsigned long)netAckSequence);
  else snprintf(msg,sizeof(msg),"%s  STATO %lu",
               netPaired?"COLLEGATO":"RICERCA",
               (unsigned long)netReceivedSequence);
  centered(msg,303,1,netPaired?MK_GREEN:MK_FAINT);
  uiButton(119,318,122,25,netRole!=NET_OFF&&playing?"PARTITA":"INDIETRO",1);
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
  centered("DIREZIONE",73,1,MK_FAINT);
  uiButton(85,89,190,35,invertKnob?"INVERTITA":"NORMALE",2);
  centered("SCATTI PER VITA",134,1,MK_FAINT);
  uiButton(80,148,58,38,"-",3);
  uiButton(222,148,58,38,"+",3);
  char buf[16];snprintf(buf,sizeof(buf),"%u",knobDivider);
  centered(buf,156,3,MK_GOLD);
  centered("FRONTE DESTRA",195,1,MK_FAINT);
  uiButton(83,209,194,32,clockwiseRising?"SALITA":"DISCESA",2);
  centered("FILTRO INCROCI",248,1,MK_FAINT);
  snprintf(buf,sizeof(buf),"%u MS",crossLockMs);
  uiButton(95,260,170,30,buf,2,crossLockMs?MK_BLUE:MK_PANEL);
  uiButton(113,301,134,28,"INDIETRO",1);
}
void knobValues(){
  if(page!=KNOB_SETTINGS)return;
  lcd->fillRect(147,148,66,37,MK_DARK);
  char buf[8];snprintf(buf,sizeof(buf),"%u",knobDivider);
  centered(buf,156,3,MK_GOLD);
}

void drawBattery(){
  base("ALIMENTAZIONE");
  centered("TENSIONE LINEA 5V",95,2,MK_FAINT);
  uiButton(106,285,148,37,"INDIETRO",2);
  updateBatteryDisplay();
}
void updateBatteryDisplay(){
  if(page!=BATTERY)return;
  lcd->fillRect(57,124,246,152,MK_DARK);
  if(!batterySampleValid){
    centered("NON DISPONIBILE",156,2,MK_AMBER);
    centered("Controllare il sensore",221,1,MK_FAINT);
    return;
  }
  char buf[32];snprintf(buf,sizeof(buf),"%.2f V",batteryVolts);
  centered(buf,150,4,MK_WHITE);
  centered("NON E' LA CARICA BATTERIA",214,1,MK_AMBER);
  centered("Percentuale: non misurata",239,1,MK_FAINT);
}

void drawDiagnostics(){
  base("DIAGNOSTICA");
  centered("RUOTA SOLO A DESTRA",74,1,MK_FAINT);
  uiButton(58,278,116,42,"AZZERA",2);
  uiButton(184,278,116,42,"INDIETRO",1);
}
void redrawDiagnostics(){
  if(page!=DIAG)return;
  uint32_t a,b,af,ar,bf,br,ca,cb,ba,bb;
  uint8_t lockMs;
  portENTER_CRITICAL(&knobMux);
  a=rawA;b=rawB;af=rawAFalling;ar=rawARising;
  bf=rawBFalling;br=rawBRising;
  ca=rejectedCrossA;cb=rejectedCrossB;
  ba=rejectedBounceA;bb=rejectedBounceB;
  lockMs=crossLockMs;
  portEXIT_CRITICAL(&knobMux);
  lcd->fillRect(52,94,256,175,MK_DARK);
  char s[58];
  snprintf(s,sizeof(s),"DESTRA OK: %lu",(unsigned long)a);
  centered(s,99,2,MK_BLUE);
  snprintf(s,sizeof(s),"su:%lu giu:%lu  X:%lu R:%lu",
           (unsigned long)ar,(unsigned long)af,
           (unsigned long)ca,(unsigned long)ba);
  centered(s,129,1,MK_FAINT);
  lcd->drawFastHLine(77,153,206,MK_PANEL2);
  snprintf(s,sizeof(s),"SINISTRA OK: %lu",(unsigned long)b);
  centered(s,161,2,MK_AMBER);
  snprintf(s,sizeof(s),"su:%lu giu:%lu  X:%lu R:%lu",
           (unsigned long)br,(unsigned long)bf,
           (unsigned long)cb,(unsigned long)bb);
  centered(s,192,1,MK_FAINT);
  snprintf(s,sizeof(s),"X = incroci   R = rimbalzi");
  centered(s,223,1,MK_FAINT);
  snprintf(s,sizeof(s),"BLOCCO %u MS | DX %s",lockMs,clockwiseRising?"SALITA":"DISCESA");
  centered(s,247,1,MK_GOLD);
}
void render(){
  switch(page) {
    case HOME:drawHome();break;case SETUP:drawSetup();break;
    case GAME:drawGame();break;
    case GAME_MENU:drawGameMenu();break;
    case COMMANDER_DAMAGE:drawCommanderDamage();break;
    case HISTORY:drawHistory();break;
    case FIRST_PLAYER:drawFirstPlayer();break;
    case GAME_STATUS:drawGameStatus();break;
    case RESET_CONFIRM:drawResetConfirm();break;
    case COUNTERS:drawCounters();break;
    case TOOLS:drawTools();break;case ROLL:drawRoll();break;case TIMER:drawTimer();break;
    case SETTINGS:drawSettings();break;
    case DISPLAY_SETTINGS:drawDisplaySettings();break;
    case KNOB_SETTINGS:drawKnobSettings();break;
    case BATTERY:drawBattery();break;
    case DIAG:drawDiagnostics();redrawDiagnostics();break;
    case NETWORK:drawNetwork();break;
  }
  fresh=false;lifeDirty=false;lifeDirtyMask=0;selectionDirty=false;counterDirty=false;
}
void adjustLife(int p,int change) {
  if(!playing||p>=players)return;
  const int previous=life[p];
  const int updated=constrain(previous+change,-99,999);
  if(updated==previous)return;
  lastUndo.kind=UNDO_LIFE;lastUndo.player=p;lastUndo.before=previous;
  lastUndo.previousSelected=selected;
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
  recordEvent(EV_LIFE,p,p,0,previous,updated);
  markSaved();lifeDirty=true;lifeDirtyMask|=(1<<p);
  // In Alpha 4 we redrew both focus frames on EVERY pulse; avoid that.
  if(selectionChanged)selectionDirty=true;
}
void adjustCounter(int amount) {
  int16_t &c=currentCounter();
  int updated=constrain((int)c+amount,0,999);
  if(updated==c)return;
  lastUndo.kind=UNDO_COUNTER;lastUndo.player=selected;lastUndo.counter=selectedCounter;
  lastUndo.before=c;lastUndo.previousSelected=selected;
  c=updated;
  recordEvent(EV_COUNTER,selected,selected,(uint8_t)selectedCounter,lastUndo.before,updated);
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
  if(netRole==NET_VIEWER && page!=NETWORK){
    // Mirror can only open its network configuration; it cannot modify
    // life, Commander damage, ownership, or create a new game.
    changePage(NETWORK);
    return;
  }
  if(page==HOME){
    if(within(x,y,90,111,270,165))changePage(playing?GAME:SETUP);
    else if(within(x,y,90,174,270,216))changePage(SETUP);
    else if(within(x,y,90,222,270,264))changePage(TOOLS);
    else if(within(x,y,90,271,270,313))changePage(SETTINGS);

  } else if(page==SETUP){
    if(within(x,y,75,104,171,148)){players=1;fresh=true;markSaved();}
    else if(within(x,y,189,104,285,148)){players=2;fresh=true;markSaved();}
    else if(within(x,y,75,158,171,202)){players=3;fresh=true;markSaved();}
    else if(within(x,y,189,158,285,202)){players=4;fresh=true;markSaved();}
    else if(within(x,y,72,232,172,275)){startLife=20;fresh=true;markSaved();}
    else if(within(x,y,188,232,288,275)){startLife=40;fresh=true;markSaved();}
    else if(within(x,y,105,287,255,329))newGame();
  } else if(page==FIRST_PLAYER){
    if(within(x,y,64,258,173,307)){
      firstCandidate=(uint8_t)(esp_random()%players);fresh=true;
    }else if(within(x,y,187,258,296,307))chooseFirstPlayer();
  } else if(page==GAME){
    if(players>=3){
      if(abs(x-180)<=28 && abs(y-184)<=28)changePage(GAME_MENU);
      else if(y>=49&&y<=70&&x>=95&&x<=265)nextTurn();
      else{
        for(int p=0;p<players;p++){
          int rx,ry,rw,rh;
          if(!splitCardBounds(p,rx,ry,rw,rh))continue;
          if(!within(x,y,rx,ry,rx+rw,ry+rh))continue;
          if(abs(x-(rx+16))<=16 && abs(y-(ry+55))<=18)adjustLife(p,-1);
          else if(abs(x-(rx+rw-16))<=16 && abs(y-(ry+55))<=18)adjustLife(p,1);
          else if(selected!=p){selected=p;selectionDirty=true;markSaved();}
          break;
        }
      }
    }else if(players!=2){
      if(near(x,y,53,214))adjustLife(selected,-1);
      else if(near(x,y,307,214))adjustLife(selected,1);
      else if(within(x,y,61,276,168,316))changePage(COUNTERS);
      else if(within(x,y,192,276,299,316))changePage(GAME_MENU);
      else if(y>=91&&y<=128){
        int w=players==1?110:76,gap=players==1?0:4;
        int left=(360-(players*w+(players-1)*gap))/2;
        for(int p=0;p<players;p++)
          if(within(x,y,left+p*(w+gap),91,left+p*(w+gap)+w,128)){
            if(selected!=p){selected=p;markSaved();fresh=true;}
            break;
          }
      }else if(y>=49&&y<=72)nextTurn();
    }else{
      if(near(x,y,78,142))adjustLife(0,-1);
      else if(near(x,y,282,142))adjustLife(0,1);
      else if(near(x,y,78,282))adjustLife(1,-1);
      else if(near(x,y,282,282))adjustLife(1,1);
      else if(within(x,y,58,178,137,210))changePage(COUNTERS);
      else if(within(x,y,142,178,220,210))nextTurn();
      else if(within(x,y,225,178,302,210))changePage(GAME_MENU);
      else if(within(x,y,99,76,260,171)){selected=0;markSaved();selectionDirty=true;}
      else if(within(x,y,99,215,260,311)){selected=1;markSaved();selectionDirty=true;}
      else if(y>=49&&y<=72)nextTurn();
    }

  } else if(page==GAME_MENU){
    if(within(x,y,59,76,171,116))changePage(COMMANDER_DAMAGE);
    else if(within(x,y,187,76,299,116)){historyOffset=0;changePage(HISTORY);}
    else if(within(x,y,59,127,171,167))changePage(TOOLS);
    else if(within(x,y,187,127,299,167))changePage(GAME_STATUS);
    else if(within(x,y,59,178,171,218))nextTurn();
    else if(within(x,y,187,178,299,218)){
      firstCandidate=(uint8_t)(esp_random()%players);changePage(FIRST_PLAYER);
    }else if(within(x,y,59,229,171,269))undoLast();
    else if(within(x,y,187,229,299,269))changePage(RESET_CONFIRM);
    else if(within(x,y,59,280,171,320))changePage(HOME);
    else if(within(x,y,187,280,299,320))changePage(GAME);
  } else if(page==COMMANDER_DAMAGE){
    if(y>=94&&y<=126){
      for(int p=0;p<players;p++)if(within(x,y,36+p*74,94,104+p*74,126)){
        commanderSource=p;
        if(!partnerEnabled[p])commanderSlot=0;
        if(players>1&&commanderTarget==commanderSource)
          commanderTarget=(commanderSource+1)%players;
        fresh=true;break;
      }
    }else if(within(x,y,56,137,174,172)){
      if(partnerEnabled[commanderSource])commanderSlot=1-commanderSlot;
      else commanderSlot=0;
      fresh=true;
    }else if(within(x,y,187,137,305,172)){
      bool canDisable=true;
      if(partnerEnabled[commanderSource]){
        for(int t=0;t<4;t++)if(commanderMatrix[commanderSource][1][t]!=0)canDisable=false;
      }
      if(canDisable){
        partnerEnabled[commanderSource]=!partnerEnabled[commanderSource];
        if(!partnerEnabled[commanderSource])commanderSlot=0;
        markSaved();fresh=true;
      }
    }else if(y>=195&&y<=227){
      for(int p=0;p<players;p++)if(within(x,y,36+p*74,195,104+p*74,227)){
        commanderTarget=p;fresh=true;break;
      }
    }else if(near(x,y,70,264))adjustCommanderDamage(-1);
    else if(near(x,y,290,264))adjustCommanderDamage(1);
    else if(within(x,y,92,308,174,337)){
      commanderAutoLife=!commanderAutoLife;markSaved();fresh=true;
    }else if(within(x,y,187,308,269,337))changePage(GAME_MENU);
  } else if(page==HISTORY){
    if(within(x,y,61,284,174,315)){
      int maximum=historySize>HISTORY_ROWS?historySize-HISTORY_ROWS:0;
      historyOffset=constrain((int)historyOffset+HISTORY_ROWS,0,maximum);
      fresh=true;
    }else if(within(x,y,186,284,299,315)){
      historyOffset=max(0,(int)historyOffset-HISTORY_ROWS);
      fresh=true;
    }else if(within(x,y,117,317,243,345))changePage(GAME_MENU);
  } else if(page==GAME_STATUS){
    int w=players==1?100:69,gap=players==1?0:6;
    int left=(360-(players*w+(players-1)*gap))/2;
    if(y>=96&&y<=135){
      for(int p=0;p<players;p++)
        if(within(x,y,left+p*(w+gap),96,left+p*(w+gap)+w,135)){
          selected=p;markSaved();fresh=true;break;
        }
    }else if(within(x,y,96,178,264,216)){
      uint8_t before=monarchOwner;
      monarchOwner=(monarchOwner==selected)?255:selected;
      recordEvent(EV_MONARCH,selected,selected,0,before,monarchOwner);
      markSaved();fresh=true;
    }else if(within(x,y,96,247,264,285)){
      uint8_t before=initiativeOwner;
      initiativeOwner=(initiativeOwner==selected)?255:selected;
      recordEvent(EV_INITIATIVE,selected,selected,0,before,initiativeOwner);
      markSaved();fresh=true;
    }else if(within(x,y,110,297,250,331))changePage(GAME);
  } else if(page==RESET_CONFIRM){
    if(within(x,y,65,224,173,275))changePage(GAME);
    else if(within(x,y,187,224,295,275))newGame();

  } else if(page==COUNTERS){
    int w=players==1?110:70,gap=players==1?0:6;
    int left=(360-(players*w+(players-1)*gap))/2;
    if(y>=94&&y<=133){
      for(int p=0;p<players;p++)
        if(within(x,y,left+p*(w+gap),94,left+p*(w+gap)+w,133)){
          selected=p;fresh=true;markSaved();break;
        }
    }else if(within(x,y,68,157,110,191)){
      selectedCounter=(CounterType)((selectedCounter+COUNTER_COUNT-1)%COUNTER_COUNT);fresh=true;
    }else if(within(x,y,250,157,292,191)){
      selectedCounter=(CounterType)((selectedCounter+1)%COUNTER_COUNT);fresh=true;
    }else if(near(x,y,78,234))adjustCounter(-1);
    else if(near(x,y,282,234))adjustCounter(1);
    else if(within(x,y,111,290,249,326))changePage(GAME);
  } else if(page==TOOLS){
    if(y>=95&&y<=253){
      int col=-1,row=-1;
      for(int c=0;c<3;c++)if(x>=45+c*91&&x<=129+c*91)col=c;
      for(int r=0;r<3;r++)if(y>=95+r*57&&y<=139+r*57)row=r;
      if(col>=0&&row>=0){
        int index=row*3+col;
        if(index==8)changePage(TIMER);
        else{rollType=(RollType)index;throwTool();changePage(ROLL);}
      }
    }else if(within(x,y,110,286,250,326))changePage(playing?GAME_MENU:HOME);
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
    if(within(x,y,80,75,280,114))changePage(DISPLAY_SETTINGS);
    else if(within(x,y,80,122,280,161))changePage(KNOB_SETTINGS);
    else if(within(x,y,80,169,280,208))changePage(BATTERY);
    else if(within(x,y,80,216,280,255))changePage(DIAG);
    else if(within(x,y,80,263,280,302))changePage(NETWORK);
  } else if(page==NETWORK){
    if(within(x,y,72,94,176,133)&&netRole==NET_OFF){netChoice=NET_HOST;fresh=true;}
    else if(within(x,y,184,94,288,133)&&netRole==NET_OFF){netChoice=NET_VIEWER;fresh=true;}
    else if(y>=156&&y<=198&&netRole==NET_OFF&&netChoice==NET_VIEWER){
      for(int i=0;i<4;i++)if(within(x,y,77+i*52,156,123+i*52,198)){netDigit=i;fresh=true;break;}
    }else if(within(x,y,69,207,143,241)&&netRole==NET_OFF&&netChoice==NET_VIEWER){
      netChangeDigit(-1);fresh=true;
    }else if(within(x,y,217,207,291,241)&&netRole==NET_OFF&&netChoice==NET_VIEWER){
      netChangeDigit(1);fresh=true;
    }else if(within(x,y,100,254,260,295)){
      if(netRole==NET_OFF)netStart(netChoice);
      else netStop();
      fresh=true;
    }else if(within(x,y,119,318,241,343)){
      if(netRole!=NET_OFF&&playing)changePage(GAME);
      else changePage(SETTINGS);
    }
  } else if(page==DISPLAY_SETTINGS){
    if(within(x,y,78,163,140,215)){brightness=max(40,(int)brightness-15);setBrightness();markSaved();displayValues();}
    else if(within(x,y,220,163,282,215)){brightness=min(255,(int)brightness+15);setBrightness();markSaved();displayValues();}
    else if(within(x,y,109,280,251,317))changePage(SETTINGS);
  } else if(page==KNOB_SETTINGS){
    if(within(x,y,85,89,275,124)){invertKnob=!invertKnob;markSaved();fresh=true;}
    else if(within(x,y,80,148,138,186)){knobDivider=max(1,(int)knobDivider-1);markSaved();knobValues();}
    else if(within(x,y,222,148,280,186)){knobDivider=min(4,(int)knobDivider+1);markSaved();knobValues();}
    else if(within(x,y,83,209,277,241)){
      portENTER_CRITICAL(&knobMux);
      clockwiseRising=!clockwiseRising;lastPulseA=micros();
      portEXIT_CRITICAL(&knobMux);
      markSaved();fresh=true;
    }else if(within(x,y,95,260,265,290)){
      portENTER_CRITICAL(&knobMux);
      crossLockMs=crossLockMs==0?8:(crossLockMs==8?16:(crossLockMs==16?32:0));
      lastAcceptedDir=0;
      portEXIT_CRITICAL(&knobMux);
      markSaved();fresh=true;
    }
    else if(within(x,y,113,301,247,329))changePage(SETTINGS);
  } else if(page==BATTERY){
    if(within(x,y,106,285,254,322))changePage(SETTINGS);
  } else if(page==DIAG){
    if(within(x,y,58,278,174,320)){
      portENTER_CRITICAL(&knobMux);
      rawA=rawB=rawAFalling=rawARising=rawBFalling=rawBRising=0;
      rejectedCrossA=rejectedCrossB=rejectedBounceA=rejectedBounceB=0;
      lastPulseA=lastPulseB=lastAcceptedUs=0;lastAcceptedDir=0;
      knobDelta=0;
      portEXIT_CRITICAL(&knobMux);
      pulseAccumulator=0;redrawDiagnostics();
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
  randomSeed(esp_random());turnStartedAt=millis();gameStartedAt=millis();fresh=true;
  render(); // draw first frame while backlight remains off
  ledcSetup(0,50000,8);ledcAttachPin(PIN_BL,0);
  setBrightness();
}
void loop(){
  netPoll();
  if(page==NETWORK){
    static uint32_t lastNetDraw=0;
    if(millis()-lastNetDraw>=850){lastNetDraw=millis();fresh=true;}
  }
  portENTER_CRITICAL(&knobMux);
  int32_t amount=knobDelta;knobDelta=0;
  portEXIT_CRITICAL(&knobMux);
  if(amount!=0){
    if(invertKnob)amount=-amount;
    pulseAccumulator+=amount;
    if(abs(pulseAccumulator)>=knobDivider){
      int step=pulseAccumulator/knobDivider;
      pulseAccumulator-=(step*knobDivider);
      // Keep every counted pulse, including those accumulated while the
      // LCD is painting. Life limits are enforced inside adjustLife().
      if(netRole==NET_VIEWER && page!=NETWORK){
        // Ignore remote viewer encoder inputs, preserve the authoritative host.
      }else if(page==NETWORK && netRole==NET_OFF && netChoice==NET_VIEWER){
        netChangeDigit(step);fresh=true;
      }else if(page==GAME)adjustLife(selected,step);
      else if(page==COUNTERS)adjustCounter(step);
      else if(page==COMMANDER_DAMAGE)adjustCommanderDamage(step);
      else if(page==HISTORY){
        int maximum=historySize>HISTORY_ROWS?historySize-HISTORY_ROWS:0;
        historyOffset=constrain((int)historyOffset+step,0,maximum);
        fresh=true;
      }
      else if(page==FIRST_PLAYER){
        firstCandidate=(uint8_t)(((int)firstCandidate+step%players+players)%players);
        fresh=true;
      }else if(page==GAME_STATUS){
        selected=(uint8_t)(((int)selected+step%players+players)%players);
        markSaved();fresh=true;
      }
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
        if(netRole!=NET_VIEWER && page!=HOME && !swipeAlreadyHandled && touchStartX<=115 &&
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
        if(players==2){
          if(mask&0x01)lifeArea(0);
          if(mask&0x02)lifeArea(1);
        }else if(players>=3){
          for(int p=0;p<players;p++)if(mask&(1<<p))lifeArea(p);
        }else if(mask&(1<<selected))lifeArea(selected);
        lastLifePaintAt=millis();
      }
      if(selectionDirty){
        // Only repaint labels when player focus actually changes.
        paintSelection();selectionDirty=false;
        lastSelectionPaintAt=millis();
      }
      updateKOAnimations();
      if((uint32_t)(millis()-lastTurnPaintAt)>=1000)drawTurnLine();
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
