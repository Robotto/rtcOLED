#include <Wire.h>

//OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
unsigned long lastUpdateTimeout;
bool redraw=false;
bool colon=true;

//RTC:
#include "RTClib.h"
RTC_DS1307 RTC;
DateTime now;
//char *weekDay[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
char *weekDay[] = {"S\x9bndag","Mandag","Tirsdag","Onsdag","Torsdag","Fredag","L\x9brdag"}; //omgwtfextendedASCII

//ENC:
#include <Encoder.h>
static unsigned encA=1, encB=0, encBTN=9;
Encoder myEnc(encA, encB);
long oldPosition;
int change=0;

//Power management:
static unsigned FETpin=7;
static unsigned long powerDelay=10000;
unsigned long onTime=0;

//Batt:
static unsigned battPin=A0;
unsigned long ADCfiltered=1024/5*3.7; //default to 3.7V
float vBatt=3.7;
static int filterBeta=10;

//Alarm:
static unsigned alarmPin=10;
bool alarmSet=false;
int alarmHours=0;
int alarmMinutes=0;
int snoozeOffset=0;
int snoozeDuration=5; //minutes

//States enum:
enum clockStates { //default state should be "get_data"
	toRunning,
	running,
	toAlarmOnOff,
	alarmOnOff,
	setAlarmH,
	setAlarmM,
	toAlarming,
	alarming,
	snoozing
};

clockStates state = running; //hit the ground

void setup()   {

	pinMode(FETpin,OUTPUT);
	digitalWrite(FETpin,HIGH); //keep alive
	onTime=millis();

	pinMode(encBTN,INPUT_PULLUP);
	digitalWrite(alarmPin,LOW);
	pinMode(alarmPin,OUTPUT);

	Serial.begin(9600);

	TX_RX_LED_INIT; //Arduino leonardo specific

	Wire.begin();
    RTC.begin();

	//RTC.adjust(DateTime(F(__DATE__),F(__TIME__)));
  	//if (! RTC.isrunning()) RTC.adjust(DateTime(F(__DATE__),F(__TIME__)));

  	now = RTC.now();

	oledInit();

	hhmm();
	//ss();
	alarmIcon();
	date();
	display.display();

	//load seconds animation
	for(int i=0;i<=now.second();i++) //count up from 0 to current seconds
		{
		for(int j=0; j<8;j++) display.drawPixel(4+2*i, j, WHITE); //count seconds with lines
		display.display(); //redraw for each second untill current
		//delay(1); //magic number.. it looks good at this rate :)
		}
}


void loop()
{

	if(millis()>onTime+powerDelay) digitalWrite(FETpin,LOW); //turns the power off after $powerDelay milliseconds.

	ADCfiltered=((ADCfiltered*filterBeta)+analogRead(battPin))/(filterBeta+1); //low pass filtering of ADC0
	vBatt=(float)5/(float)1024*(float)ADCfiltered; //ADC -> voltage

	switch(state)
	{
	case running:
		hhmm();
		ss();
		alarmIcon();
		if(now.hour()+now.minute()+now.second()==0) date(); //only do things at midnight
		break;

	case toAlarmOnOff:
		drawAlarmSetting();
	case alarmOnOff:

		if(readEncoder()!=0)
			{
				alarmSet=!alarmSet;
				drawAlarmSetting();
			}

		break;
	case setAlarmH:
		clearBlue();
		display.setTextSize(1);
		display.setCursor(12,48);
		display.print("Hours?");
		alarmHours+=readEncoder();
		if(alarmHours<0) alarmHours=23;
		if(alarmHours>23) alarmHours=0;
		display.setTextSize(4);
		display.setCursor(8,16);
		if(alarmHours<10) display.print("0");
		display.print(String(alarmHours));
		display.print(":");
		if(alarmMinutes<10) display.print("0");
		display.print(String(alarmMinutes));
		break;
	case setAlarmM:
		clearBlue();
		display.setTextSize(1);
		display.setCursor(80,48);
		display.print("Minutes?");
		alarmMinutes+=readEncoder();
		if(alarmMinutes<0) alarmMinutes=59;
		if(alarmMinutes>59) alarmMinutes=0;
		display.setTextSize(4);
		display.setCursor(8,16);
		if(alarmHours<10) display.print("0");
		display.print(String(alarmHours));
		display.print(":");
		if(alarmMinutes<10) display.print("0");
		display.print(String(alarmMinutes));
		break;
	case toAlarming:
		clearBlue();
		display.setTextSize(2);
		display.setCursor(35,48);
		display.print("ALARM!");
		break;
	case alarming:
		hhmm();
		ss();
		if(colon) display.invertDisplay(1);
		else display.invertDisplay(0);
		digitalWrite(alarmPin,HIGH);
		break;
	case snoozing:
		clearBlue();
		hhmm();
		ss();
		display.invertDisplay(0);
		digitalWrite(alarmPin,LOW);
		display.setTextSize(1);
		display.setCursor(0,52);
		display.print("Snooze: ");
		display.setTextSize(2);
		display.setCursor(48,48);
		display.print(alarmMinutes+snoozeOffset-now.minute()-1);
		display.print(":");
		if(59-now.second()<10) display.print("0");
		display.print(59-now.second());
		break;
	case toRunning:
		snoozeOffset=0;
		digitalWrite(alarmPin,LOW);
		display.invertDisplay(0);
		display.clearDisplay();
		date();
		break;
	default:
		break;
	}


	if(millis()>lastUpdateTimeout+490)
		{
			lastUpdateTimeout=millis();
			colon=!colon;
			redraw=true;
			now = RTC.now();
		}

	if (redraw)
		{
			display.display();
			redraw=false;
		}
	checkState();
}

void checkState()
{
	switch(state)
	{
		case running:
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				state=toAlarmOnOff;
				while(!digitalRead(encBTN)); //wait for release
			}
			else if(alarmSet && now.hour()==alarmHours && now.minute()==alarmMinutes+snoozeOffset && now.second()==0) state=toAlarming;
			break;
		case toAlarmOnOff:
			state=alarmOnOff;
			break;
		case alarmOnOff:
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				if(alarmSet) state = setAlarmH;
				else state = toRunning;
				while(!digitalRead(encBTN)); //wait for release
			}
			break;
		case setAlarmH:
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				state=setAlarmM;
				while(!digitalRead(encBTN)); //wait for release
			}
			break;
		case setAlarmM:
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				state=toRunning;
				while(!digitalRead(encBTN)); //wait for release
			}
			break;
		case toAlarming:
			//activate alarm by doing a thing
			state=alarming;
			break;
		case alarming:
			if(/*now.hour()!=alarmTime.hour() ||*/ now.minute()!=alarmMinutes+snoozeOffset) state=toRunning;
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				state=snoozing;
				snoozeOffset+=snoozeDuration;
				while(!digitalRead(encBTN)); //wait for release
			}
			break;
		case snoozing:
			if(/*now.hour()!=alarmTime.hour() ||*/ now.minute()==alarmMinutes+snoozeOffset) state=toAlarming;
			if(!digitalRead(encBTN)) //if button pressed
			{
				redraw=true;
				delay(20); //debounce
				state=toRunning;
				while(!digitalRead(encBTN)); //wait for release
			}
			break;
		case toRunning:
			//reset alarm / confirm alarm set.. do a thing
			state=running;
			break;
		default:
		 	break;
	}
}

void oledInit()
{
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)

	setContrast(&display, 0);  //contrast is a number between 0 and 255. Use a lower number for lower contrast

	display.clearDisplay();
	display.setTextSize(3);
	display.setCursor(0,20);
	display.setTextColor(WHITE);
	//display.print("OMGWTF!");
	//display.display();
	//crazy_draw();

	//clearYellow();
	//clearBlue();

	date();



	display.display();
}

void crazy_draw()
{
    for(int j=4; j<128; j+=2)
    {
    for(int i=0; i<8;i++)
    {
    display.drawPixel(j, i+8, WHITE);
    display.drawPixel(j, i+48, WHITE);
    //delay(1);
  }
    display.display();
  }
  for(int j=4; j<128; j+=2)
    {
    for(int i=0; i<8;i++)
    {
    display.drawPixel(j, i+8, BLACK);
    display.drawPixel(j, i+48, BLACK);
    //delay(1);
  }
    display.display();
  }
}

void ss()
{
	for(int i=0; i<8;i++) display.drawPixel(4+2*now.second(), i, WHITE); //count seconds with lines
	if (now.second()==0) clearYellow();
}

void hhmm()
{
	ClearHHMM();
	display.setTextSize(4);
	display.setCursor(8,16); //
	if(now.hour()<10)display.print("0");
	display.print(String(now.hour()));
	if(colon) display.print(":");
	else display.print(" ");
	if(now.minute()<10) display.print("0");
	display.print(String(now.minute()));
}

void clearYellow()
{
	display.fillRect(0,0,128,16,BLACK);
}

void clearBlue()
{
	display.fillRect(0,16,128,48,BLACK);
}

void ClearHHMM()
{
	display.fillRect(0,16,128,32,BLACK);
}
void clearDate()
{
	display.fillRect(0,56,128,8,BLACK);
}

void clearAlarm()
{
	display.fillRect(0,47,128,8,BLACK);
}

void date()
{
	clearDate();
	display.setTextSize(1);
	display.setCursor(0,56);
	display.print(weekDay[now.dayOfWeek()]);
	display.print(" ");
	if(now.day()<10) display.print("0");
	display.print(String(now.day()));
	display.print("/");
	if(now.month()<10) display.print("0");
	display.print(String(now.month()));
	display.print(" ");
	display.print(String(now.year()));
}

void alarmIcon()
{
	if(alarmSet) {
		display.setTextSize(1);
		display.setCursor(45,47);
		display.print("A:");
		if(alarmHours<9) display.print('0');
		display.print(String(alarmHours));
		display.print(":");
		if(alarmMinutes<9) display.print('0');
		display.print(String(alarmMinutes));
	}
	else
	{
		clearAlarm();
		display.setTextSize(1);
		display.setCursor(30,47);
		display.print("Batt: ");
		display.print(String(vBatt));
		display.print("V");
	}
}

int readEncoder()
{
	int returnVal=0;
	unsigned long newPosition = myEnc.read();

	if (newPosition != oldPosition)
  	{
  		change += newPosition-oldPosition;
		oldPosition = newPosition;
  	}

  	if(change) redraw=true;
  	if(change<-3) { returnVal=-1; change=0;}
  	if(change>3) { returnVal=1; change=0;} //divide ticks by four so the input matches the mechanical feedback
  	return returnVal;
}

void drawAlarmSetting()
{
		display.clearDisplay();
		display.setTextSize(2);
		display.setCursor(4,0);
		display.setTextColor(WHITE);
		display.print("ALARM SET:");

		//clearBlue();
		display.setTextSize(3);

		if(alarmSet)
		{
			display.fillRoundRect(0,16,64,48,10,WHITE);
			//display.fillRect(0,16,64,48,WHITE);
			display.setCursor(14,30);
			display.setTextColor(BLACK);
			display.print("ON");
			display.setCursor(72,30);
			display.setTextColor(WHITE);
			display.print("OFF");
		}
		else
		{
			display.fillRoundRect(64,16,64,48,10,WHITE);
			//display.fillRect(64,16,64,48,WHITE);
			display.setCursor(14,30);
			display.setTextColor(WHITE);
			display.print("ON");
			display.setCursor(72,30);
			display.setTextColor(BLACK);
			display.print("OFF");
		}
	display.setTextColor(WHITE);
}

void setContrast(Adafruit_SSD1306 *display, uint8_t contrast)
{
    display->ssd1306_command(SSD1306_SETCONTRAST);
    display->ssd1306_command(contrast);
}