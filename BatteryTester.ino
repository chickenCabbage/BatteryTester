//Pins translation from ATmega328P hardware to Arduino pins according to:
//https://upload.wikimedia.org/wikipedia/commons/c/c9/Pinout_of_ARDUINO_Board_and_ATMega328PU.svg

#define debug false			//print to serial
#define pbPin 1				//PD1, ATmega pin 3

#define quickRunPin 2		//PD2, ATmega pin 4
bool quickRun = false;		//skip the start PB and shorten display times?
#define QUICKFACTOR 2		//by how much are quickRun delays faster?

#define delayPotPin A3		//PC3, ATmega pin pin 26
int printDelay = 1000;		//display time

#define socOnlyPin 0	//PD0, ATmega pin 2
bool socOnly = false;	//show only SOC vs SOC+SOH

#include <LiquidCrystal.h>
#define rs 5	//PD5, ATmega pin 11
#define en 11	//PB3, ATmega pin 17
#define d4 10	//PB2, ATmega pin 16
#define d5 9	//PB1, ATmega pin 15
#define d6 12	//PB4, ATmega pin 18
#define d7 13	//PB5, ATmega pin 19
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
//16 chars, 2 lines:
#define LCDCOL 16
#define LCDROW 2

#define pbLEDPin 3		//PD3, ATmega pin 5
#define indicatorPin 4	//PD4, ATmega pin 4
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
#define MEASURMENTAMNT	512 //amount of measurments to take
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

#define SOH100	0.011	//11 mR is a 100% healthy battery per WPIlib.
#define SOH0	0.025	//25 mR is a dead battery.

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
float volts[STAGESAMNT]; //global storage of parameters
float amps[STAGESAMNT];

/*
 * free pins:
 * PB6, PC6
 * 4, 6, 7
 */


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
	pinMode(pbLEDPin, OUTPUT);
	pinMode(pbPin, INPUT_PULLUP);
	pinMode(backlightPin, OUTPUT);
	pinMode(indicatorPin, OUTPUT);
	pinMode(socOnlyPin, INPUT_PULLUP);
	pinMode(quickRunPin, INPUT_PULLUP);
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
		if(!quickRun) {
			lcd.print("Awaiting serial.");
			while(!Serial);
		}
		Serial.println(String(STAGESAMNT) + "-stage ATP.");
		Serial.println("quickRun is " + String(quickRun) + ", display delay time: " + String(printDelay) + "ms.");
	}
}

void loop() {
	setPrintDelay();
	socOnly = !digitalRead(socOnlyPin);
	
	if(socOnly) {
		digitalWrite(indicatorPin, HIGH);
		setLoadState(OFF);
		measureParams(0);
		printAll("V: " + String(volts[0], 2) + "V", true, 0, 1);
		float soc = floatMap(volts[0], SOC0, SOC100, 0, 100);
		digitalWrite(indicatorPin, LOW);
		printAll("SOC: " + String(soc, 1) + "%", false, printDelay*10, 2);
		
	}
	else {
		setLoadState(OFF);
		
		//before starting a test, check the temperature:
		float temp = floatMap(analogRead(tempPin), 535, 360, 22, 45);
		//over a preset temp, override quickRun and begin showing the temperature:
		if(temp >= DISPTEMP) {
			String tempText = "";
			tempText += String(temp, 1) + "C";
			
			//center the text if displaying "OVER-HEAT!":
			if(temp >= MAXTEMP) {
				for(byte counter=0; counter<((LCDCOL - tempText.length())/2); counter++) {
					tempText = " " + tempText;
				}
			}
			printAll(tempText, true, 0, 2);	
			quickRun = false;
		}
		else lcd.clear();
		//if the temperature is too high, do not allow the user to proceed with the test.
		if(temp >= MAXTEMP) {
			digitalWrite(indicatorPin, HIGH);
			printAll("   OVER-HEAT!", false, BLINKINTERVAL, 1);
			digitalWrite(indicatorPin, LOW);
			delay(BLINKINTERVAL);
			return;
			/* the return allows the re-checking of inputs and of the temp.
			 * instead of using a while loop and delay(), we can use the printAll() delay
			 * and the loop() function.
			 */
		}
		//if the temperature is valid, you will arrive here:
		
		byte	stageCounter = 0;	//iterates over the stages array
		float	avgRes = 0;			//resistance result arrives here
		if(!quickRun) {
			printAll("Ready.", false, 0, 1);
			waitForPB();
		}
		
		//go over every stage:
		digitalWrite(indicatorPin, HIGH);
		for(stageCounter; stageCounter<STAGESAMNT; stageCounter++) {
			doStage(stages[stageCounter], stageCounter);
			if(stageCounter) { //if you're past the first stage (floating voltage test)
				float delta = volts[0] - volts[stageCounter];
				avgRes += (delta / amps[stageCounter]);
			}
		}
		//calculate state of charge based on the OFF voltage measurment
		avgRes = (avgRes / (STAGESAMNT-1)) * 1000; //average the sum and convert to miliR
		avgRes -= 100; //remove the shunt's and the connection's resistance from the result (shunt is 73.33)
		
		lcd.write(1); //show omega symbol for ohms units
		float soc = floatMap(volts[0], SOC0, SOC100, 0, 100);
		digitalWrite(indicatorPin, LOW);
		float soh = floatMap(avgRes, SOH0, SOH100, 0, 100);
		digitalWrite(backlightPin, HIGH);

		bool sohWarn = soh < 0;
		if(!quickRun) {
			//if you've got time: print warnings.
			bool socWarn = soc < 50;
			if(socWarn || sohWarn) {
				printAll("    WARNING!", true, 0, 1);
				String warnings = "";
				if(socWarn) warnings += "SOC";
				if(sohWarn && socWarn) warnings += ", ";
				if(sohWarn) warnings += "SOH";

				//add spaces to center:
				for(byte counter=0; counter<((LCDCOL - warnings.length())/2); counter++) {
					warnings = " " + warnings;
				}
				printAll(warnings, false, printDelay, 2);
				waitForPB();
			}
		}

		//print SOH if it is okay. print resistance if not okay:
		if(!sohWarn) 
			printAll("SOH: " + String((int) soh) + "%", true, 0, 1);
		else {
			printAll("Avg Rint: " + String((int) avgRes), true, 0, 1);
			lcd.setCursor(LCDCOL-2, 0);
			lcd.print("m");
			lcd.write(1);
		}
		printAll("SOC: " + String(soc, 1) + "%", false, printDelay, 2);
		
		waitForPB();
	}
}

int setPrintDelay() {
	quickRun = !digitalRead(quickRunPin);
	if(!quickRun) {
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
	}
	else {
		printDelay = 0;
	}
}

void waitForPB() {
	delay(BLINKINTERVAL);
	bool cursorOn = true; //start out showing the arrow symbol
	bool resetted = false;
	while(true) { //this will be broken by a return call
		lcd.setCursor(LCDCOL-1, LCDROW-1); //show cursor in bottom-right corner
		if(cursorOn) {
			lcd.write(2);
			digitalWrite(pbLEDPin, HIGH);
		}
		else {
			lcd.print(" ");
			digitalWrite(pbLEDPin, LOW);
		}
		//wait for the blinkInterval, while checking for a button press every PushButtonCheckDelay:
		int totalDelay = BLINKINTERVAL;
		for(totalDelay; totalDelay>0; totalDelay-=PBCHECKDELAY) {
			if(digitalRead(pbPin)) resetted = true; //wait for the user to let go of the button
			if(resetted && !digitalRead(pbPin)) return;
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
	if(!quickRun) {
		printAll("Testing state:", true, 0, 1);
		printAll(currStage.msg, false, printDelay, 2);
	}

	//take measurments:
	setLoadState(currStage.loadState);
	measureParams(index);
	setLoadState(OFF);

	if(!quickRun) {
		printAll("Voltage: " + String(volts[index], 3) + "V", true, 0, 1);
		printAll("Current: " + String(amps[index], 3) + "A", false, printDelay, 2);
	}
}

void measureParams(byte stage) {
	//remove things that could affect the current:
	lcd.clear();
	digitalWrite(pbLEDPin, LOW);
	digitalWrite(backlightPin, LOW);
	
	//calculate average values over measurmentAmnt measurments with measurmentDelay interval:
	unsigned long voltsSum = 0, ampsSum = 0; //hold sums for avg calc
	int counter, maxCount;
	if(!quickRun)	maxCount = MEASURMENTAMNT;
	else			maxCount = MEASURMENTAMNT/QUICKFACTOR;
	delay(100); //wait to reach steady state
	for(counter=0; counter<maxCount; counter++) {
		voltsSum += analogRead(voltsPin);
		ampsSum += analogRead(ampsPin);
		if(socOnly || quickRun)	delay(MEASURMENTDELAY/QUICKFACTOR);
		else					delay(MEASURMENTDELAY);
	}
	if(printDelay > 200)
		digitalWrite(backlightPin, HIGH);

	float avgVolts, avgAmps;
	if(!quickRun) {
		avgVolts = voltsSum / MEASURMENTAMNT;
		avgAmps  = ampsSum / MEASURMENTAMNT;
	}
	else {
		avgVolts = (voltsSum * QUICKFACTOR) / MEASURMENTAMNT;
		avgAmps  = (ampsSum * QUICKFACTOR) / MEASURMENTAMNT;
	}

	//map results by predefined calibration values:
	avgVolts = floatMap(avgVolts, 0, 1024, 0, MAXVOLTS);
	avgAmps  = floatMap(avgAmps,  ISSADC, 1024, ISSAMPS, MAXCURRENT);
	//assign to global storage:
	volts[stage] = avgVolts;
	amps[stage]  = avgAmps;
}
