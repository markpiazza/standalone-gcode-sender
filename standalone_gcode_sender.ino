/*
Version 15-10-2017.002

Arduino MEGA
- Serial1 (pins 18 and 19) communicates with arduino with grbl shield

GCode Sender.
Hardware: Arduino Uno,
				 ,analog thumbstick (small joystick, with intgrated push button)
				 ,sd card reader with SPI interface
				 ,a general 4x20 LCD display with i2c interface

Limitations: It does not support directories on the SD card, only files in the root directory are supported.
			
(Micro) SD card attached to SPI bus as follows:
CS - pin 53
MOSI - pin 51
MISO - pin 50
CLK - pin 52

Joystick attached af follows:
x value - pin A0
y value - pin A1
push button - pin A2
NOTE: My joystick is mounted sideways so x = vertical, y = horizontal

LCD Display
the LCD display is connected to pins 20 (SDA) and 21 (SCL) (default i2c connections)

Arduino pins:
D2   : Button 12 o clock
D3   : Button  3 o clock
D4   : Button  6 o clock
D5   : Button  9 o clock
D6   : Connected to the reset pin to be able to self reset
D7   : 
D8   : Spindle Relay 1
D9   : Spindle Relay 2
D11  : Spindle LED Yellow
D12  : Spindle LED Green
D13  :
D18 rx1 : connects to tx on other arduino
D19 tx1 : connects to rx on other arduino
D20   : LCD Display (SDA) geel
D21   : LCD Display (SCL) wit
D53  : SD Card CS
D51  : SD Card MOSI
D50  : SD Card MISO
D52  : SD Card Clock
A0   : Joystick x value wit
A1   : Joystick y value rood
A2   : Joystick switch  zwart

*/

#include <LiquidCrystal_I2C.h> // by Frank de Brabander, available in the arduino library manager
#include <Wire.h> 
#include <SPI.h>
#include <SD.h>

#define SD_card_Reader   53 
#define joystick_xPin    A0  
#define joystick_yPin    A1  
#define joystick_switch  A2   
#define SpindleStartRelay 8
#define SpindleRelay      9
#define yellowLED        11
#define greenLED         12

#define Msw12 2  // the switch at 12 o'clock
#define Msw3  3  // the switch at 3  o'clock
#define Msw6  4  // the switch at 6  o'clock
#define Msw9  5  // the switch at 9  o'clock

#define resetpin 6

// Display
LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 20 chars and 4 line display


// Globals
char WposX[9];            // last known X pos on workpiece, space for 9 characters ( -999.999\0 )
char WposY[9];            // last known Y pos on workpiece
char WposZ[9];            // last known Z heighton workpiece, space for 8 characters is enough( -99.999\0 )
char MposX[9];            // last known X pos absolute to the machine
char MposY[9];            // last known Y pos absolute to the machine
char MposZ[9];            // last known Z height absolute to the machine

char machineStatus[10];   // last know state (Idle, Run, Hold, Door, Home, Alarm, Check)

bool awaitingOK = false;   // this is set true when we are waiting for the ok signal from the grbl board (see the sendCodeLine() void)

unsigned long runningTime;

void setup() {
	
	// display 
	lcd.init();                      // initialize the lcd 
	lcd.backlight();
	lcd.begin(20, 4);

	// inputs (write high to enable pullup)
	pinMode(joystick_switch,INPUT_PULLUP);
	pinMode(Msw12,INPUT_PULLUP);
	pinMode(Msw3,INPUT_PULLUP);
	pinMode(Msw6,INPUT_PULLUP);
	pinMode(Msw9,INPUT_PULLUP); 
	digitalWrite(resetpin,HIGH);
	pinMode(resetpin,OUTPUT);
	pinMode(SpindleStartRelay,OUTPUT);
	pinMode(SpindleRelay,OUTPUT);
	digitalWrite(SpindleStartRelay,HIGH);
	digitalWrite(SpindleRelay,HIGH);
	
	pinMode(yellowLED,OUTPUT); digitalWrite(yellowLED,LOW);
	pinMode(greenLED,OUTPUT);  digitalWrite(greenLED,LOW);
	 
	// Ask to connect (you might still use a computer and NOT connect this controller)
	setTextDisplay("",F("      Connect?    "),"","");
	while (digitalRead(joystick_switch)) {}  // wait for the button to be pressed
	delay(50);
	// Serial1 connections
	Serial.begin(115200);
	Serial.println("Mega GCode Sender, 2.0");
	Serial1.begin(115200);
	while (!digitalRead(joystick_switch)) {}  // be sure the button is released before we continue
	delay(50);     

	// init the sd card reader
	if (!SD.begin(SD_card_Reader)) {    
		 setTextDisplay(F("Error"),F("SD Card Fail!"),"",F("Auto RESET in 3 sec"));
		 delay(3000);
		 lcd.clear(); 
		 delay(500);
		 digitalWrite(resetpin,LOW);
	}   
}


void emergencyBreak(){
	Serial1.print(F("!"));  // feed hold
	setTextDisplay(F("Pause"),F("Green = Continue"),F("Red = Reset"),"");
	while (true) {
		if (!digitalRead(Msw3)) {Serial1.print("~");return;} // send continue and return
		if (!digitalRead(Msw6)) {
		Serial1.write(24); // soft reset, clear command buffer            
		delay(500);          
		digitalWrite(resetpin,LOW);
		}
	}
}

byte fileMenu() {
	/*
	This is the file menu.
	You can browse up and down to select a file.
	Click the button to select a file

	Move the stick right to exit the file menu and enter the Move menu
	*/
	byte fileindex=1;
	String fn; 
	byte fc = filecount();
	int xval,yval;

	fn= getFileName(fileindex);
	setTextDisplay(F("Files ")," -> " + (String)fn,"",F("Click to select"));
				
	while (true){
		xval=analogRead(joystick_xPin);
		yval=analogRead(joystick_yPin);
	 
		if (fileindex < fc && xval < 30) { // joystick down!            
			fileindex++;
			fn= getFileName(fileindex);
			lcd.setCursor(0, 1);
			lcd.print(F(" -> ")); lcd.print(fn);
			for (int u= fn.length() + 4; u < 20; u++){ lcd.print(" ");}
			
			waitForJoystickMid();     
			}
			
		if (xval > 900) { // joystick up!
			if (fileindex > 1) {      
				fileindex--;
				fn="";
				fn= getFileName(fileindex);
				lcd.setCursor(0, 1);
				lcd.print(F(" -> ")); lcd.print(fn);
				for (int u= fn.length() + 4; u < 20; u++){ lcd.print(" ");}
				waitForJoystickMid();
			}
		}

	
		if (fileindex > 0 && digitalRead(joystick_switch)==LOW && fn!="") {    // Pushed it!           
			 setTextDisplay(F("Send this file? ")," -> " + fn,"",F("Click to confirm"));  // Ask for confirmation
			 delay(50);
			 while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
			 delay(50);

			 unsigned long t = millis();
			 while (millis()-t <= 1500UL) {
				 if (digitalRead(joystick_switch)==LOW) {  // Press the button again to confirm
					 delay(10);
					 while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
					 return fileindex;  
					 break;
					 }
				 }
				 setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
		 }
		 
		// joystick to right exits this menu and joystick_switches to MOVE menu    
		if (yval > 900) { // full right!
			 waitForJoystickMid();
			 return 0;    
			 setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));             
			 }
	 }   
}

void waitForJoystickMid() {
	 int xval,yval;
	 while(true) { // wait for the joystick to be release (back to mid position)
					xval=analogRead(joystick_xPin);
					yval=analogRead(joystick_yPin);
					if ((xval >= 500 && xval <= 600) && (yval >= 500 && yval <= 600)) {break;}    
				}
	}
	
void moveMenu(){
	/*
	This is the Move menu,
	X and Y move with the joystick
	Z moves with the buttons
		switch at 12 o clock = up in big steps
		switch at  9 o clock = down in big steps
		switch at  3 o clock = up in small steps
		switch at  6 o clock = down in small steps
		
	Exit the menu by clicking the joystick
	*/

	lcd.clear();
	String MoveCommand;
	bool hardup,harddown,slowup, slowdown, updateDisplay;
	int xval,yval,qtime;
	unsigned long queue=0; // queue length in milliseconds
	unsigned long startTime,lastUpdate;
	
	char sln1[21];
	char sln2[21];
	char sln3[21];
	char sln4[21];
	char sla[30];
	char slb[30];
	clearRXBuffer();
	sendCodeLine(F("G21"),true);
	sendCodeLine(F("G91"),true); // Switch to relative coordinates
	
	while (MoveCommand!="-1") { 
		MoveCommand="";
		// read the state of all inputs
		xval=analogRead(joystick_xPin);
		yval=analogRead(joystick_yPin);
		hardup   = !digitalRead(Msw12);
		harddown = !digitalRead(Msw9);
		slowup   = !digitalRead(Msw3);
		slowdown = !digitalRead(Msw6);
				
		if (yval < 30)  {
			MoveCommand=F("G1 X-5 F2750");   // Full left
			} else {
				 if (yval < 300) {
					MoveCommand=F("G1 X-1 F500"); // Slow Left
				 } else {
					if (yval > 900) {
						 MoveCommand=F("G1 X5 F2750");   // Full right
						 } else {
							 if (yval > 600) {
								MoveCommand=F("G1 X1 F500");  // Slow Right
							 }
						 }
				 }
			}
				
		if (xval < 30)  {
			MoveCommand=F("G1 Y-5 F2750"); // Full Reverse
			} else {
				if (xval < 300) {MoveCommand=F("G1 Y-1 F500");   // slow in reverse
				} else {
					if (xval > 900) {
						MoveCommand=F("G1 Y5 F2750");  // Full forward
						}  else {
							 if (xval > 600) {
									MoveCommand=F("G1 Y1 F500");  // slow forward
								}
							}
					}  
				}
			 

		if (slowup)  {MoveCommand=F("G1 Z0.2 F110");}    // Up Z
		if (hardup)  {MoveCommand=F("G1 Z1 F2000");}     // Full up Z
		if (slowdown){MoveCommand=F("G1 Z-0.2 F110");}   // Down Z
		if (harddown){MoveCommand=F("G1 Z-1 F2000");}    // Full down Z

		if (MoveCommand!="") {
			// send the commands        
			sendCodeLine(MoveCommand,true);            
			MoveCommand="";       
		}
		
		if (MoveCommand=="") startTime = millis();
		// get the status of the machine and monitor the receive buffer for OK signals
		
		if (millis() - lastUpdate >= 500) {
			getStatus();
			lastUpdate=millis(); 
			updateDisplay = true;   
		}
		
		if (updateDisplay) {
			updateDisplay = false;  
		
			sprintf(sln1,"X: %s\0",WposX); 
			sprintf(sln2,"Y: %s\0",WposY);
			sprintf(sln3,"Z: %s\0",WposZ);
			sprintf(sln4,"Click stick to exit\0");
			sprintf(slb,"%s %s %s\0",WposX,WposY,WposZ);
						
			if (sla != slb) {
				setTextDisplay(sln1,sln2,sln3,sln4);        
				strcpy(sla,slb);
			}
		}
		
		if (digitalRead(joystick_switch)==LOW) { // button is pushed, exit the move loop   
			// set x,y and z to 0
			sendCodeLine(F("G92 X0 Y0 Z0"),true); //For GRBL v8
			getStatus();
			lcd.clear();
			MoveCommand=F("-1");      
			while (digitalRead(joystick_switch)==LOW) {}; // wait until the user releases the button
			delay(10);
		} 
	}
	

}
	
String getFileName(byte i){
	/*
		Returns a filename.
		if i = 1 it returns the first file
		if i = 2 it returns the second file name
		if i = 3 ... see what I did here?
	*/
	byte x = 0;
	String result;
	File root = SD.open("/");    
	while (result=="") {
		File entry =  root.openNextFile();
		if (!entry) {    
				// noting         
				} else {
					if (!entry.isDirectory()) {
						x++;
						if (x==i) result=entry.name();                         
					}
					entry.close(); 
				}
	}
	root.close();  
	return result;
}

byte filecount(){
	/*
		Count the number of files on the SD card.
	*/
	
	byte c =0;
	 File root = SD.open("/"); 
	 while (true) {
		File entry =  root.openNextFile();
		if (! entry) {
			root.rewindDirectory(); 
			root.close(); 
			return c; 
			break;} else  {
				 if (!entry.isDirectory()) c++;    
				 entry.close();
				}  
	 }
}
	

void setTextDisplay(String line1, String line2, String line3, String line4){
	/*
	 This writes text to the display
	*/        
		lcd.setCursor(0, 0);
		lcd.print(line1);
		for (int u= line1.length() ; u < 20; u++){ lcd.print(" ");}
		lcd.setCursor(0, 1);
		lcd.print(line2);
		for (int u= line2.length() ; u < 20; u++){ lcd.print(" ");}
		lcd.setCursor(0, 2);
		lcd.print(line3);
		for (int u= line3.length() ; u < 20; u++){ lcd.print(" ");}
		lcd.setCursor(0, 3);
		lcd.print(line4);
		for (int u= line4.length() ; u < 20; u++){ lcd.print(" ");}       
}


void sendFile(byte fileIndex){   
	/*
	This procedure sends the cgode to the grbl shield, line for line, waiting for the ok signal after each line

	It also queries the machine status every 500 milliseconds and writes some status information on the display
	*/
	String strLine="";
 
	File dataFile;
	unsigned long lastUpdate;
	
 
	String filename;
	filename= getFileName(fileIndex);
	dataFile = SD.open(filename);
	if (!dataFile) {
		setTextDisplay(F("File"),"", F("Error, file not found"),"");
		delay(1000); // show the error
		return;
		}

	 // Set the Work Position to zero
	sendCodeLine(F("G90"),true); // absolute coordinates
	sendCodeLine(F("G21"),true);
	sendCodeLine(F("G92 X0 Y0 Z0"),true);  // set zero
	clearRXBuffer();

	// Start the spindle
	SpindleSlowStart();
		
	// reset the timer
	runningTime = millis();
	
	// Read the file and send it to the machine
	while ( dataFile.available() ) {
		
		if (!awaitingOK) { 
			// If we are not waiting for OK, send the next line      
			strLine = dataFile.readStringUntil('\n'); 
			strLine = ignoreUnsupportedCommands(strLine);
			if (strLine !="") sendCodeLine(strLine,true);    // sending it!  
		}

		// get the status of the machine and monitor the receive buffer for OK signals
		if (millis() - lastUpdate >= 250) {      
			lastUpdate=millis();  
			updateDisplayStatus(runningTime);          
		}
		if (!digitalRead(Msw12)) {emergencyBreak();}
	}
	
	
	/* 
	 End of File!
	 All Gcode lines have been send but the machine may still be processing them
	 So we query the status until it goes Idle
	*/
	
	 while (strcmp (machineStatus,"Idle") != 0) {
		if (!digitalRead(Msw12)) {emergencyBreak();}
		delay(250);
		getStatus();
		updateDisplayStatus(runningTime);          
	 }
	 // Now it is done.      

	 // Stop the spindle
	 StopSpindle();   
	
	 lcd.setCursor(0, 1);
	 lcd.print(F("                ")); 
	 lcd.setCursor(0, 2);
	 lcd.print(F("                ")); 
	 lcd.setCursor(0, 3);
	 lcd.print(F("                "));
	 while (digitalRead(joystick_switch)==HIGH) {} // Wait for the button to be pressed
	 delay(50);
	 while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
	 delay(50);
	 dataFile.close();
	 resetSDReader();
	 return; 
}



void updateDisplayStatus(unsigned long runtime){
	/*
	 I had some issues with updating the display while carving a file
	 I created this extra void, just to update the display while carving.
	*/

	unsigned long t = millis() - runtime;
	int H,M,S;
	char timeString[9];
	char p[3];
	
	t=t/1000;
	// Now t is the a number of seconds.. we must convert that to "hh:mm:ss"
	H = floor(t/3600);
	t = t - (H * 3600);
	M = floor(t/60);
	S = t - (M * 60);

	sprintf (timeString,"%02d:%02d:%02d",H,M,S);
	timeString[8]= '\0';
	
	getStatus();
	lcd.clear();
	lcd.print(machineStatus);
	lcd.print(" ");
	lcd.print(timeString);
	lcd.setCursor(0, 1);
	lcd.print(F("X: "));  lcd.print(WposX);lcd.print(F("  "));
	
	lcd.setCursor(0, 2);
	lcd.print(F("Y: "));  lcd.print(WposY);lcd.print(F("  "));
	
	lcd.setCursor(0, 3);
	lcd.print(F("Z: "));  lcd.print(WposZ);lcd.print(F("  "));
	
	}

void resetSDReader() {
	/* 
	 This next SD.begin is to fix a problem, I do not like it but there you go.
	 Without this sd.begin, I could not open anymore files after the first run.

	 To make this work I have changed the SD library a bit (just added one line of code)
	 I added root.close() to SD.cpp
	 as explained here http://forum.arduino.cc/index.php?topic=66415.0
	*/
	
	 while (!SD.begin(SD_card_Reader)) {    
		 setTextDisplay(F("Error"),F("SD Card Fail!"),"","");
		 delay(2000); 
		 }
}


void sendCodeLine(String lineOfCode, bool waitForOk ){
	/*
		This void sends a line of code to the grbl shield, the grbl shield will respond with 'ok'
		but the response may take a while (depends on the command).
		So we immediately check for a response, if we get it, great!
		if not, we set the awaitingOK variable to true, this tells the sendfile() to stop sending code
		We continue to monitor the rx buffer for the 'ok' signal in the getStatus() procedure.
	*/
	int updateScreen =0 ;
	Serial.print("Send ");
	if ( waitForOk ) Serial.print("and wait, ");
	Serial.println(lineOfCode);
	
	Serial1.println(lineOfCode);
	awaitingOK = true;  
	// delay(10);
	Serial.println("SendCodeLine calls for CheckForOk");
	checkForOk();  
	
	while (waitForOk && awaitingOK) {
		delay(50);
		// this may take long, so still update the timer on screen every second or so
		if (updateScreen++ > 4) {
			updateScreen=0;
			updateDisplayStatus(runningTime);
		}
		checkForOk();      
		}
}
	
void clearRXBuffer(){
	/*
	Just a small void to clear the RX buffer.
	*/
	char v;
		while (Serial1.available()) {
			v=Serial1.read();
			delay(3);
		}
	}
	
String ignoreUnsupportedCommands(String lineOfCode){
	/*
	Remove unsupported codes, either because they are unsupported by GRBL or because I choose to.  
	*/
	removeIfExists(lineOfCode,F("G64"));   // Unsupported: G64 Constant velocity mode 
	removeIfExists(lineOfCode,F("G40"));   // unsupported: G40 Tool radius comp off 
	removeIfExists(lineOfCode,F("G41"));   // unsupported: G41 Tool radius compensation left
	removeIfExists(lineOfCode,F("G81"));   // unsupported: G81 Canned drilling cycle 
	removeIfExists(lineOfCode,F("G83"));   // unsupported: G83 Deep hole drilling canned cycle 
	removeIfExists(lineOfCode,F("M6"));    // ignore Tool change
	removeIfExists(lineOfCode,F("M7"));    // ignore coolant control
	removeIfExists(lineOfCode,F("M8"));    // ignore coolant control
	removeIfExists(lineOfCode,F("M9"));    // ignore coolant control
	removeIfExists(lineOfCode,F("M10"));   // ignore vacuum, pallet clamp
	removeIfExists(lineOfCode,F("M11"));   // ignore vacuum, pallet clamp
	removeIfExists(lineOfCode,F("M5"));    // ignore spindle off
	lineOfCode.replace(F("M2 "),"M5 M2 "); // Shut down spindle on program end.
	
	// Ignore comment lines 
	// Ignore tool commands, I do not support tool changers
	if (lineOfCode.startsWith("(") || lineOfCode.startsWith("T") ) {lineOfCode="";}    
	lineOfCode.trim();  
	return lineOfCode;
}

String removeIfExists(String lineOfCode,String toBeRemoved ){
	if (lineOfCode.indexOf(toBeRemoved) >= 0 ) lineOfCode.replace(toBeRemoved," ");
	return lineOfCode;
}

void checkForOk() {
	// read the receive buffer (if anything to read)
	char c,lastc;
	c=64;
	lastc=64;
	 while (Serial1.available()) {
		c = Serial1.read();  
		if (lastc=='o' && c=='k') {awaitingOK=false; Serial.println("< OK");}
		lastc=c;
		delay(3);          
		}    
}

void getStatus(){
	/*
		This gets the status of the machine
		The status message of the machine might look something like this (this is a worst scenario message)
		The max length of the message is 72 characters long (including carriage return).
		
		<Check,MPos:-995.529,-210.560,-727.000,WPos:-101.529,-115.440,-110.000>    
	*/
	
	char content[80];
	char character;
	byte index=0;
	bool completeMessage=false;
	int i=0;
	int c=0;
	Serial.println("GetStatus calls for CheckForOk");
	checkForOk();

	Serial1.print(F("?"));  // Ask the machine status
	while (Serial1.available() == 0) { }  // Wait for response 
	while (Serial1.available()) {
		character=Serial1.read();  
		content[index] = character;    
		if (content[index] =='>') completeMessage=true; // a simple check to see if the message is complete
		if (index>0) {if (content[index]=='k' && content[index-1]=='o') {awaitingOK=false; Serial.println("< OK from status");}}
		index++;
		delay(1); 
		}
	
	if (!completeMessage) { return; }   
	Serial.println(content);
	i++;
	while (c<9 && content[i] !=',') {machineStatus[c++]=content[i++]; machineStatus[c]=0; } // get the machine status
	while (content[i++] != ':') ; // skip until the first ':'
	c=0;
	while (c<8 && content[i] !=',') { MposX[c++]=content[i++]; MposX[c] = 0;} // get MposX
	c=0; i++;
	while (c<8 && content[i] !=',') { MposY[c++]=content[i++]; MposY[c] = 0;} // get MposY
	c=0; i++;
	while (c<8 && content[i] !=',') { MposZ[c++]=content[i++]; MposZ[c] = 0;} // get MposZ
	while (content[i++] != ':') ; // skip until the next ':'
	c=0;
	while (c<8 && content[i] !=',') { WposX[c++]=content[i++]; WposX[c] = 0;} // get WposX
	c=0; i++;
	while (c<8 && content[i] !=',') { WposY[c++]=content[i++]; WposY[c] = 0;} // get WposY
	c=0; i++;
	while (c<8 && content[i] !='>') { WposZ[c++]=content[i++]; WposZ[c] = 0;} // get WposZ

	if (WposZ[0]=='-')   
	 { WposZ[5]='0';WposZ[6]=0;}
	else 
	 { WposZ[4]='0';WposZ[5]=0;}
		
}

void  StopSpindle() {
	digitalWrite(SpindleStartRelay,HIGH);
	digitalWrite(SpindleRelay,HIGH);
	digitalWrite(yellowLED,LOW); 
	digitalWrite(greenLED,LOW);
	}

void SpindleSlowStart(){   
	 // The first relay gives power to the spindle through a 1 ohm power resistor.
	 // This limits the current just enough to prevent the current protection.
	 digitalWrite(yellowLED,HIGH);   
	 digitalWrite(SpindleStartRelay,LOW);
	 delay(500);
	 digitalWrite(yellowLED,LOW);   
	 
	 // After the spindle reaches full speed, the second relay takes over, this relay powers the
	 // spindle without any resitors
	 digitalWrite(SpindleRelay,LOW);
	 digitalWrite(greenLED,HIGH);
	 delay(1000); // wait for the spindle to rev up completely
}

void loop() {  
	byte a;
	a = fileMenu(); 
	if (a==0) {    
		moveMenu();
	} else {
		sendFile(a);
	}
}