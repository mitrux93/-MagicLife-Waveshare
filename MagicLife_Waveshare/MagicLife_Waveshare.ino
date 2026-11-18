#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <string.h>

// Waveshare ESP32-S3-Knob-Touch-LCD-1.8; JC3636W518 V2
#define LCD_CS 14
#define LCD_CLK 13
#define LCD_D0 15
#define LCD_D1 16
#define LCD_D2 17
#define LCD_D3 18
#define LCD_RST 21
#define LCD_BL 47
#define TOUCH_SDA 11
#define TOUCH_SCL 12
#define TOUCH_RST 10
#define TOUCH_ADDR 0x15
#define KNOB_A 8
#define KNOB_B 7

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS,LCD_CLK,LCD_D0,LCD_D1,LCD_D2,LCD_D3);
Arduino_GFX *lcd = new Arduino_ST77916(bus,LCD_RST,0,true,360,360);
Preferences prefs;
const uint16_t BG=0x1083, PANEL=0x2128, WHITE=0xffff, MUTED=0xad75;
const uint16_t BLUE=0x367d, ORANGE=0xfcc5, GREEN=0x65ef, RED=0xf3a7, EDGE=0x426d;
uint8_t players=1, initialLife=40, selected=0;
int16_t life[2]={40,40};
bool game=false, redraw=true, changed=false, touchLast=false;
uint32_t changedAt=0;
volatile int32_t turns=0;
volatile uint32_t pulseA=0,pulseB=0;
void IRAM_ATTR tickA(){uint32_t n=micros();if(n-pulseA>3500){turns++;pulseA=n;}}
void IRAM_ATTR tickB(){uint32_t n=micros();if(n-pulseB>3500){turns--;pulseB=n;}}
void txt(const char*s,int x,int y,int z,uint16_t c){lcd->setTextSize(z);lcd->setTextColor(c);lcd->setCursor(x,y);lcd->print(s);}
void center(const char*s,int y,int z,uint16_t c){txt(s,180-(int)strlen(s)*3*z,y,z,c);}
void button(int x,int y,int w,int h,const char*s,int z,uint16_t bg,uint16_t fg=WHITE){
 lcd->fillRoundRect(x,y,w,h,11,bg);
 txt(s,x+w/2-(int)strlen(s)*3*z,y+(h-8*z)/2,z,fg);
}
void control(int x,int y,bool plus,uint16_t c){
 lcd->fillCircle(x,y,29,PANEL);lcd->drawCircle(x,y,29,c);
 lcd->fillRoundRect(x-13,y-3,26,6,2,WHITE);
 if(plus)lcd->fillRoundRect(x-3,y-13,6,26,2,WHITE);
}
void bigNum(int n,int y,int z,uint16_t color){
 char s[12];snprintf(s,sizeof(s),"%d",n);
 center(s,y,z,color);
}
void base(){lcd->fillScreen(BG);lcd->drawCircle(180,180,175,EDGE);}
void menu(){
 base();center("MAGIC LIFE",44,3,WHITE);
 center("GIOCATORI",101,2,MUTED);
 button(65,131,108,48,"1",3,players==1?BLUE:PANEL);
 button(187,131,108,48,"2",3,players==2?ORANGE:PANEL);
 center("VITE INIZIALI",201,2,MUTED);
 button(86,228,82,43,"20",2,initialLife==20?BLUE:PANEL);
 button(192,228,82,43,"40",2,initialLife==40?BLUE:PANEL);
 button(103,290,154,43,"INIZIA",2,GREEN,BG);
}
void one(){
 base();center("MAGIC LIFE",52,2,MUTED);
 center("GIOCATORE 1",92,2,BLUE);
 bigNum(life[0],159,8,life[0]<=0?RED:WHITE);
 control(53,194,false,RED);control(307,194,true,GREEN);
 center("GHIERA: VITE",260,2,MUTED);
 button(124,298,112,35,"MENU",2,PANEL);
}
void two(){
 base();center("P1",31,3,selected==0?WHITE:BLUE);
 bigNum(life[0],94,5,life[0]<=0?RED:BLUE);
 control(61,110,false,BLUE);control(299,110,true,BLUE);
 button(134,162,92,35,"MENU",2,PANEL);
 center("P2",211,3,selected==1?WHITE:ORANGE);
 bigNum(life[1],272,5,life[1]<=0?RED:ORANGE);
 control(61,288,false,ORANGE);control(299,288,true,ORANGE);
}
void save(){
 prefs.putUChar("players",players);prefs.putUChar("initial",initialLife);
 prefs.putShort("life1",life[0]);prefs.putShort("life2",life[1]);
 prefs.putBool("game",game);changed=false;
}
void update(){redraw=true;changed=true;changedAt=millis();}
void adjust(int p,int delta){
 if(!game||p>=players)return;
 life[p]=constrain((int)life[p]+delta,-99,999);selected=p;update();
}
bool touch(int &x,int &y){
 uint8_t b[6];Wire.beginTransmission(TOUCH_ADDR);Wire.write((uint8_t)0x01);
 if(Wire.endTransmission(true)!=0)return false;
 if(Wire.requestFrom((uint8_t)TOUCH_ADDR,(uint8_t)6)!=6)return false;
 for(int i=0;i<6;i++)b[i]=Wire.read();
 if((b[1]&0x0f)==0)return false;
 x=((b[2]&15)<<8)|b[3];y=((b[4]&15)<<8)|b[5];
 return x>=0&&x<360&&y>=0&&y<360;
}
bool rect(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;}
bool circ(int x,int y,int a,int b){long dx=x-a,dy=y-b;return dx*dx+dy*dy<=38L*38L;}
void tap(int x,int y){
 if(!game){
  if(rect(x,y,65,131,173,179)){players=1;redraw=true;}
  else if(rect(x,y,187,131,295,179)){players=2;redraw=true;}
  else if(rect(x,y,86,228,168,271)){initialLife=20;redraw=true;}
  else if(rect(x,y,192,228,274,271)){initialLife=40;redraw=true;}
  else if(rect(x,y,103,290,257,333)){
   life[0]=life[1]=initialLife;selected=0;game=true;update();save();
  }
  return;
 }
 if(players==1){
  if(circ(x,y,53,194))adjust(0,-1);
  else if(circ(x,y,307,194))adjust(0,1);
  else if(rect(x,y,124,298,236,333)){game=false;update();save();}
 }else{
  if(rect(x,y,134,162,226,197)){game=false;update();save();}
  else if(circ(x,y,61,110))adjust(0,-1);
  else if(circ(x,y,299,110))adjust(0,1);
  else if(circ(x,y,61,288))adjust(1,-1);
  else if(circ(x,y,299,288))adjust(1,1);
  else if(rect(x,y,120,52,240,151)){selected=0;redraw=true;}
  else if(rect(x,y,120,230,240,330)){selected=1;redraw=true;}
 }
}
void setup(){
 Serial.begin(115200);
 prefs.begin("magiclife",false);
 players=prefs.getUChar("players",1)==2?2:1;
 initialLife=prefs.getUChar("initial",40)==20?20:40;
 life[0]=prefs.getShort("life1",initialLife);
 life[1]=prefs.getShort("life2",initialLife);
 game=prefs.getBool("game",false);
 if(life[0]<-99||life[0]>999||life[1]<-99||life[1]>999)game=false;
 Wire.begin(TOUCH_SDA,TOUCH_SCL);Wire.setClock(400000);
 pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);delay(20);
 digitalWrite(TOUCH_RST,HIGH);delay(120);
 pinMode(KNOB_A,INPUT_PULLUP);pinMode(KNOB_B,INPUT_PULLUP);
 attachInterrupt(digitalPinToInterrupt(KNOB_A),tickA,RISING);
 attachInterrupt(digitalPinToInterrupt(KNOB_B),tickB,RISING);
 ledcSetup(0,50000,8);ledcAttachPin(LCD_BL,0);ledcWrite(0,230);
 if(!lcd->begin(40000000)){Serial.println("LCD non inizializzato");return;}
 lcd->setRotation(0);lcd->setTextWrap(false);redraw=true;
}
void loop(){
 noInterrupts();int32_t steps=turns;turns=0;interrupts();
 if(steps&&game)adjust(selected,constrain(steps,-25L,25L));
 static uint32_t lastPoll=0;
 if(millis()-lastPoll>25){
  lastPoll=millis();int x,y;bool down=touch(x,y);
  if(down&&!touchLast)tap(x,y);touchLast=down;
 }
 if(changed&&millis()-changedAt>1500)save();
 if(redraw){if(!game)menu();else if(players==1)one();else two();redraw=false;}
 delay(2);
}
