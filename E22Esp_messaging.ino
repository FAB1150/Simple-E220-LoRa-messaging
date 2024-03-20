#include <Arduino.h>
#include "E220.h"

//#define USING_SOFTSERIAL  //uncomment if you're using softserial (like an arduino)

#ifdef USING_SOFTSERIAL
  #include <SoftwareSerial.h>
  
  //define the serial pins you're using. 
  //Connect the arduino's TX to the module's RX and RX to TX!
  #define TX_PIN = 2;
  #define RX_PIN = 3;
  
  SoftwareSerial receiver(RX_PIN, TX_PIN); //initializing our softserial receiver
#endif
  

//Define the pins you're using. Here are the ones I use in my ESP32.
//Commented ones are the ones I use on the arduino
#define m0 33 //7
#define m1 32 //6
#define aux 25 //5

#define serialPort 2 //the serial port

//we'll pass this to the library. If using hardware serial, modify "Serial2" with the port you're using
#ifdef USING_SOFTSERIAL
  Stream &mySerial = (Stream &)receiver;
#else
  Stream &mySerial = (Stream &)Serial2;
#endif

//here we actually define the radio module!
E220 radioModule(&mySerial, m0, m1, aux);

//------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------Under here you can edit the settings as you wish :)-------------------------------------------------
int sendAddress = 0007;  //address of the receiving device
int sendChannel = 20;  //channel of the receiving module

int moduleAddress = 0006;  //set the module address. Set a different one for each module you're using, or 65535 for broadcast!
int moduleChannel = 20; //The channel. Frequency = 850+channel MHz, values go from 0 to 80 (850 to 930MHz)
uint8_t modulePower = Power_30;  //Possible states: 21, 24, 27, 30 (numbers don't relate to dBm, 30 is max power)
uint8_t packetSize = SPS_32; //how many bytes to send per packet. Can be 200, 128, 64, 32

bool enableLBT = true; //Listen Before Talk. Recommended for noisy environments and legally required if you transmit frequently
bool appendRSSI = false; //Decide if it appends the RSSI value to every received message or not
bool useFixedTransmission = true; //Decide if you want to use transparent transmission or fixed transmission. Read the documentation to decide.

int usbBaudRate = 9600; //The baud rate at which the esp sends info via USB (not the module!)
char escapeCharacter = '\x04';
//-------------------------------------------------------------------------------------------------------------------------------------------

//a variable that stores how many bytes we can send per packet. We'll need it later!
int maxBytes = 0; 

void setup(){
    Serial.begin(usbBaudRate); //initialize the connection to the computer

    //initialize the connection to the module
    #ifdef USING_SOFTSERIAL
      receiver.begin(9600);
    #else
      Serial2.begin(9600);
    #endif

    mySerial.setTimeout(2000); //making sure the timeout is high enough to update the settings

    //initialise the module and check it communicates with us, else loop and keep trying
    Serial.println("Waiting for the module to initialize...");
    while(!radioModule.init()){
        delay(5000);
    }

    //seeing how many bytes we can send at a time, based on the setting by the user:
    switch (packetSize){
      case SPS_200:
        maxBytes = 200;
        break;
      case SPS_128:
        maxBytes = 128;
        break;
      case SPS_64:
        maxBytes = 64;
        break;
      case SPS_32:
        maxBytes = 32;
        break;
      default:
        maxBytes = -1;
        break;
    }

    //setting up the module with the settings we chose:
    Serial.println("Module initialized.");
    Serial.println("Setting up the module...");
    radioModule.setAddress(moduleAddress, true);
    radioModule.setChannel(moduleChannel,true);
    radioModule.setPower(modulePower,true);
    radioModule.setLBT(enableLBT, true);
    radioModule.setRSSIByteToggle(appendRSSI, true);
    radioModule.setFixedTransmission(useFixedTransmission, true);
    radioModule.setEscapeCharacter(escapeCharacter);
    radioModule.setSubPacketSize(packetSize, true);

    //printing the settings, to verify they're good (and because it looks pretty):
    Serial.println("Settings applied:");
    Serial.println("----------------------");
    Serial.print("Address: ");
      Serial.println(radioModule.getAddress());
    Serial.print("Channel: ");
      Serial.print(radioModule.getChannel());
      Serial.print(" --> ");
      Serial.print((radioModule.getChannel() + 850));
      Serial.println(".125MHz");
    Serial.print("Power: ");
      Serial.println(radioModule.getPower());
    Serial.print("LBT status: ");
      Serial.println(radioModule.getLBT());
    Serial.print("RSSI info status: ");
      Serial.println(radioModule.getRSSIByteToggle());
    Serial.print("Fixed transmission status: ");
      Serial.println(radioModule.getFixedTransmission());
    Serial.print("Subpacket size (max bytes sent at a time): ");
      Serial.println(radioModule.getSubPacketSize());
    Serial.println("----------------------");
    Serial.println("Beginning to listen for communications:");
    Serial.println();

    mySerial.setTimeout(10);  //lowering the serial timeout, by default it's way higher than necessary for the short bursts we need
}

//the function that sends the packets once we have our message
bool sendFixedDataPackets(int addr, int chan, String msg, char pktEndChar, int pktSize){
  int msglen = msg.length();  //get the message length
  int packetSize = pktSize - 4;  //buffer to insert the non-visible characters when message is split (3 bytes are used for the module transmission, one for the "end-of-packet" character
  String msgToSend = "";  //initializing our message string

  //calculating the number of packets of data to send
  int packetnr = (msglen/packetSize) + 1; 
  if(msglen%packetSize == 0){
    packetnr == msglen / packetSize;
  }

  //loop that actually sends the packets:
  for(int packet = 0; packet < packetnr; packet++){  //for each packet of data,
    for (int i = 0; i < packetSize; i++){            //for each character in the packet,
      if(i+packet*packetSize >= msglen){             //stop if we reached the end of the message.
        break;
      }
      msgToSend += msg[i+packet*packetSize];         //else add the character in the message String
    }
    if(packet != packetnr - 1){                      //if it isn't the last packet of data, append the "end-of-packet" character
      msgToSend += pktEndChar;
    }
    if(!radioModule.sendFixedData(addr, chan, msgToSend, true)){  //send the packet of data
      return false;
    } else {
		Serial.print(".");
	}
    msgToSend = ""; //reset the String to accept the next packet of data
  }
  return true;
}

bool decodeDataPackets(char pktEndChar, char magEndChar){
	String rxString = mySerial.readString(); //read the first packet
	String rxMessage = ""; //initialize the String containing the message
	
	if(rxString.indexOf(pktEndChar) != -1){ //only printing this if the message is long
		Serial.print("Message incoming");
    }
	
	while(rxString.indexOf(pktEndChar) != -1){
		Serial.print(".");
		
		rxString.remove(rxString.indexOf(pktEndChar));
		rxMessage += rxString;
		
		//wait for the module to receive the next packet.
		//To avoid an infinite loop in the case we only receive half the message,
		//we set a timeout
		unsigned long startTime = millis();  //get the start time
		while(!(mySerial.available()>0)){
			if (millis() - startTime > 5000){  //check if more than 5 seconds passed
				Serial.println();
				Serial.println("connection timed out.");
				Serial.print("Received message so far: ");
				Serial.println(rxMessage);
				return false;
			}
		}
		
		if(mySerial.available()){
			rxString = mySerial.readString(); //read the next message
		}
	}
	
	//check if we have the last packet of data:
	if(rxString.indexOf(escapeCharacter) != -1){
		rxString.remove(rxString.indexOf(escapeCharacter));  //remove the end-of-message character
        rxMessage += rxString;
        Serial.println();
	}
	
	Serial.print("Received: ");
	Serial.println(rxMessage);
	return true;
}

void loop(){
  //code for receiving data
  if(mySerial.available()){
        if(!decodeDataPackets('\x03', escapeCharacter)){
			Serial.println("Error while receiving the packet.");
		}
  }

  //code to send messages:
  if(Serial.available()){
    String message = Serial.readString();  //read the message from Serial
    Serial.print("Message: ");
    Serial.println(message);
    Serial.print("Sending");

    if(sendFixedDataPackets(sendAddress, sendChannel, message, '\x03', maxBytes)){
      Serial.println();
      Serial.println("Message sent!");
    } else{
      Serial.println();
      Serial.println("There was an error sending the message.");
    }
  }
}
