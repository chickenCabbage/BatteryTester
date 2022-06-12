//Pins translation from ATmega328P hardware to Arduino pins according to:
//https://upload.wikimedia.org/wikipedia/commons/c/c9/Pinout_of_ARDUINO_Board_and_ATMega328PU.svg

#define debug false			//print to serial
#define pbPin 1				//PD1, ATmega pin 3

#define quickStartPin 2		//PD2, ATmega pin 4
bool quickStart = false;	//skip the start PB and shorten display times?
#define QUICKFACTOR 2		//by how much are quickStart delays faster?

#define delayPotPin A3		//PC3, ATmega pin pin 26
int printDelay = 1000;		//display time

#define socOnlyPin 0	//PD0, ATmega pin 2
bool socOnly = false;	//show only SOC vs SOC+SOH

#include <LiquidCrystal.h>
#define rs 5	//PD5, ATmega pin 11
#define en 9	//PB1, ATmega pin 15
#define d4 10	//PB2, ATmega pin 16
#define d5 11	//PB3, ATmega pin 17
#define d6 12	//PB4, ATmega pin 18
#define d7 13	//PB5, ATmega pin 19
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
//16 chars, 2 lines:
#define LCDCOL 16
#define LCDROW 2

#define indicatorPin 3	//PD3, ATmega pin 5
#define backlightPin 8	//PE0, ATmega pin 14

#define hcPin A1 //PC2, ATmega pin 25
#define lcPin A2 //PC1, ATmega p  in 24
enum state {
	EC, //extreme current
	HC, //high current
	LC, //low current
	OFF //no current
};

#define BLINKINTERVAL 500	//how long is a blink half-cycle
#define PBCHECKDELAY  10	//how often to check for PB

byte nextChar[] = { //filled arrow right symbol
	B00000,
	B01000,
	B01100,
	B01110,
	B01111,
	B01110,
	B01100,
	B01000
};

byte omegaChar[] = { //omega/ohms symbol
	B00000,
	B01110,
	B10001,
	B10001,
	B10001,
	B01010,
	B11011,
	B00000
};

#define voltsPin		A5	//PC5, ATmega pin 28
#define ampsPin			A4	//PC3, ATmega pin 27
#define MEASURMENTAMNT	250 //amount of measurments to take
#define MEASURMENTDELAY	2	//interval between each measurment

#define MAXVOLTS	15.4	//max voltage measurable [volts]
#define ISSADC		514		//steady state current [ADC]
#define ISSAMPS		0.035	//steady state current [amps]
#define MAXCURRENT	3.55	//max current measurable [amps]
/* calculation looks ~ like this:
 * avgVolts = map(avgVolts, 0, 1024, 0, MAXVOLTS);
 * avgAmps  = map(avgAmps,  ISSADC, 1024, ISSAMPS, MAXCURRENT);
 */
#define SOC100	13.5 //100% state of charge voltage
#define SOC0	10.5 //0% state of charge voltage

#define tempPin A0 //PC0, ATmega pin 23
#define MAXTEMP		50 //disallow testing resistance at this temp or higher
#define DISPTEMP	40 //from this temp onward, the temp will be displayed at the "Ready." screen
#define HIGHTEMP	50
#define LOWTEMP		23
#define HIGHTEMPADC	360
#define LOWTEMPADC	535
/* the temperature is calculated based on empiric measurments of the voltage divider with
 * the PTC's and a 10k resistor. temps measured with a FLIR camera.
 * temp calculation is a floatMap() call.
 */

//bind the stage message and the loadState together
struct stage {
	String msg;
	state loadState;
};

//to add another stage, first add it to the enum and setLoadState(), only then add to stages[]
//OFF stage must always be first in the array.
stage stages[] = {
	{ "Unloaded", OFF },
	{ "Low current", LC },
	{ "High current", HC }
};
const byte STAGESAMNT = sizeof(stages) / sizeof(stages[0]);
float volts[STAGESAMNT]; //gloval storage of parameters
float amps[STAGESAMNT];


void printAll(String msg, bool clr=false, int wait=0, byte line=0) { //clr, wait and line are optional
	//clr - clear the display before showing msg
	//wait - delay after display
	//line - line to print to on
	if(debug) {
		if(line) Serial.println("LINE " + String(line) + " | " + msg);
		else	 Serial.println(msg);
	}
	if(clr)	 lcd.clear();
	if(line) lcd.setCursor(0, line-1); //line 1 = setCursor(0, 0)
	lcd.print(msg);
	delay(wait);
}

void setup() {
	pinMode(rs, OUTPUT);
	pinMode(en, OUTPUT);
	pinMode(d4, OUTPUT);
	pinMode(d5, OUTPUT);
	pinMode(d6, OUTPUT);
	pinMode(d7, OUTPUT);
	pinMode(hcPin, OUTPUT);
	pinMode(lcPin, OUTPUT);
	pinMode(backlightPin, OUTPUT);
	pinMode(indicatorPin, OUTPUT);
	pinMode(pbPin, INPUT_PULLUP);
	pinMode(socOnlyPin, INPUT_PULLUP);
	pinMode(quickStartPin, INPUT_PULLUP);
	pinMode(voltsPin, INPUT);
	pinMode(ampsPin,  INPUT);
	pinMode(tempPin, INPUT);
	pinMode(delayPotPin, INPUT);
	
	lcd.begin(LCDCOL, LCDROW);
	lcd.createChar(1, omegaChar);
	lcd.createChar(2, nextChar);
	lcd.home();

	digitalWrite(backlightPin, HIGH);
	setPrintDelay();
	
	if(debug) {
		Serial.begin(9600);
		if(!quickStart) {
			lcd.print("Awaiting serial.");
			while(!Serial);
		}
		Serial.println(String(STAGESAMNT) + "-stage ATP.");
		Serial.println("QuickStart is " + String(quickStart) + ", display delay time: " + String(printDelay) + "ms.");
	}
}

void loop() {
	setPrintDelay();
	socOnly = !digitalRead(socOnlyPin);
	
	if(socOnly) {
		setLoadState(OFF);
		measureParams(0);
		printAll("V: " + String(volts[0], 2) + "V", true, 0, 1);
		float soc = floatMap(volts[0], SOC0, SOC100, 0, 100);
		printAll("SOC: " + String(soc, 1) + "%", false, printDelay*10, 2);
		
	}
	else {
		setLoadState(OFF);
		
		//before starting a test, check the temperature:
		float temp = floatMap(analogRead(tempPin), 535, 360, 22, 45);
		//over a preset temp, override quickStart and begin showing the temperature:
		if(temp >= DISPTEMP) {
			String tempText = "";
			if(temp >= MAXTEMP)	tempText += "     "; //center the text if displaying "OVER-HEAT!".
			tempText += String(temp, 1) + "C";
			printAll(tempText, true, 0, 2);	
			quickStart = false;
		}
		else lcd.clear();
		//if the temperature is too high, do not allow the user to proceed with the test.
		if(temp >= MAXTEMP) {
			printAll("   OVER-HEAT!", false, 500, 1);
			return;
			/* the return allows the re-checking of inputs and of the temp.
			 * instead of using a while loop and delay(), we can use the printAll() delay
			 * and the loop() function.
			 */
		}
		//if the temperature is valid, you will arrive here:
		
		byte	stageCounter = 0;	//iterates over the stages array
		float	avgRes = 0;			//resistance and voltage result arrives here
		if(!quickStart) {
			printAll("Ready.", false, 0, 1);
			waitForPB();
		}
		
		//go over every stage:
		for(stageCounter; stageCounter<STAGESAMNT; stageCounter++) {
			doStage(stages[stageCounter], stageCounter);
			if(stageCounter) {
				float delta = volts[0] - volts[stageCounter];
				avgRes += (delta / amps[stageCounter]);
			}
		}
		//calculate state of charge based on the OFF voltage measurment
		avgRes = (avgRes / (STAGESAMNT-1)) * 1000; //average the sum and convert to miliR
		avgRes -= 100; //remove the shunt's and the connection's resistance from the result (shunt is 73.33)
		
		digitalWrite(backlightPin, HIGH);
		printAll("Avg res: " + String((int) avgRes), true, 0, 1);
		lcd.setCursor(LCDCOL-2, 0);
		lcd.print("m");
		lcd.write(1); //show omega symbol for ohms units
		float soc = floatMap(volts[0], SOC0, SOC100, 0, 100);
		printAll("SOC: " + String(soc, 1) + "%", false, printDelay, 2);
		
		waitForPB();
	}
}

int setPrintDelay() {
	quickStart = !digitalRead(quickStartPin);
	int pot = analogRead(delayPotPin);
	/* get exponential delay:
	 * y(1024)=10, y(512)=2.5
	 * y is the delay in seconds, and x is the pot reading.
	 * 
	 *     x^2 * 10
	 * y = --------
	 *      1024^2
	 */
	//printDelay = ((10 * pot * pot) / (1024 * 1024)) * 1000;
	//split into multiple lines to prevent overflow and return 0 or negatives:
	float tempCalc = 10*pot;
	tempCalc /= (1024/pot);
	tempCalc /= 1024;
	tempCalc *= 1000;
	printDelay = tempCalc;
	if(quickStart) printDelay /= QUICKFACTOR;
}

void waitForPB() {
	delay(500);
	bool cursorOn = true; //start out showing the arrow symbol
	while(true) { //this will be broken by a return call
		lcd.setCursor(LCDCOL-1, LCDROW-1); //show cursor in bottom-right corner
		if(cursorOn) {
			lcd.write(2);
			digitalWrite(indicatorPin, HIGH);
		}
		else {
			lcd.print(" ");
			digitalWrite(indicatorPin, LOW);
		}
		//wait for the blinkInterval, while checking for a button press every PushButtonCheckDelay:
		int totalDelay = BLINKINTERVAL;
		for(totalDelay; totalDelay>0; totalDelay-=PBCHECKDELAY) {
			if(!digitalRead(pbPin)) return;
			delay(PBCHECKDELAY);
		}
		cursorOn = !cursorOn;
	}
}

void setLoadState(state set) {
	switch(set) {
		case EC: //extreme current, unused
			digitalWrite(hcPin, HIGH);
			digitalWrite(lcPin, HIGH);
			break;
		case HC: //high current
			digitalWrite(hcPin, HIGH);
			digitalWrite(lcPin, LOW);
			break;
		case LC: //low current
			digitalWrite(hcPin, LOW);
			digitalWrite(lcPin, HIGH);
			break;
		default:
			digitalWrite(hcPin, LOW);
			digitalWrite(lcPin, LOW);
	}
}

float floatMap(float val, float fromMin, float fromMax, float toMin, float toMax) {
	//map() does not take floats and returns only integers. hence floatMap().
	return (val-fromMin) * ((toMax-toMin) / (fromMax-fromMin)) + toMin;
}

void doStage(stage currStage, byte index) {
	printAll("Testing state:", true, 0, 1);
	printAll(currStage.msg, false, printDelay, 2);

	//take measurments:
	setLoadState(currStage.loadState);
	measureParams(index);
	setLoadState(OFF);
	
	printAll("Voltage: " + String(volts[index], 3) + "V", true, 0, 1);
	printAll("Current: " + String(amps[index], 3) + "A", false, printDelay, 2);
}

void measureParams(byte stage) {
	//remove things that could affect the current:
	lcd.clear();
	digitalWrite(indicatorPin, LOW);
	digitalWrite(backlightPin, LOW);
	
	//calculate average values over measurmentAmnt measurments with measurmentDelay interval:
	unsigned long voltsSum = 0, ampsSum = 0; //hold sums for avg calc
	int counter = 0;
	delay(100); //wait to reach steady state
	for(counter=counter; counter<MEASURMENTAMNT; counter++) {
		voltsSum += analogRead(voltsPin);
		ampsSum += analogRead(ampsPin);
		if(socOnly)	delay(MEASURMENTDELAY/2);
		else		delay(MEASURMENTDELAY);
	}
	if(printDelay > 200)
		digitalWrite(backlightPin, HIGH);
	float avgVolts = voltsSum / MEASURMENTAMNT;
	float avgAmps  = ampsSum / MEASURMENTAMNT;

	//map results by predefined calibration values:
	avgVolts = floatMap(avgVolts, 0, 1024, 0, MAXVOLTS);
	avgAmps  = floatMap(avgAmps,  ISSADC, 1024, ISSAMPS, MAXCURRENT);
	//assign to global storage:
	volts[stage] = avgVolts;
	amps[stage]  = avgAmps;
}
