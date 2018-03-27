/*****************
   A Tally box for Blackmagic ATEM Switchers

   - wifi module added
   - and other stuff

*/

#include <SPI.h>            // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <EEPROM.h>         // For storing IP numbers
#include <Streaming.h>

#include <SkaarhojPgmspace.h>
#include "SkaarhojTools.h"
SkaarhojTools sTools(1);

uint8_t ipc[4];           // Will hold the C200 IP address in config mode
uint8_t ip[4];            // Will hold the C200 IP address
uint8_t atem_ip[4];       // Will hold the ATEM IP address
uint8_t mac[6];           // Will hold the Arduino Ethernet shield/board MAC address (loaded from EEPROM memory, set with ConfigEthernetAddresses example sketch)

#include <MemoryFree.h>

// Include ATEM library and make an instance:
#include <ATEMmin.h>
ATEMmin AtemSwitcher;

uint8_t greenLED = 22;
uint8_t redLED = 23;

bool isConfigMode;            // If set, the system will run the Web Configurator, not the normal program
int ind1, ind2, ind3, ind4;   // temp storage for parsing 
String temp;

uint8_t programTally;         // for tally stuff
uint8_t transitionTally;

/***********************************************************
                       MAIN PROGRAM CODE AHEAD
 **********************************************************/

void setup() {
  Serial.begin(9600);
  Serial << F("\n- - - - - - - -\nSerial Started\n\n");

  pinMode(A1, INPUT_PULLUP);
  delay(300);
  isConfigMode = (analogRead(A1) < 500) ? true : false;

  // *********************************
  // INITIALIZE EEPROM memory:
  // *********************************
  // Check if EEPROM has ever been initialized, if not, install default IP
  if (EEPROM.read(0) != 12 ||  EEPROM.read(1) != 232)  {  // Just randomly selected values which should be unlikely to be in EEPROM by default.
    // Set these random values so this initialization is only run once!
    EEPROM.write(0, 12);
    EEPROM.write(1, 232);

    // Set default IP address for Arduino panel
    EEPROM.write(2, 192);
    EEPROM.write(3, 168);
    EEPROM.write(4, 0);
    EEPROM.write(5, 111); // Just some value I chose, probably below DHCP range?

    // Set default IP address for ATEM Switcher (192.168.10.240):
    EEPROM.write(6, 192);
    EEPROM.write(7, 168);
    EEPROM.write(8, 0);
    EEPROM.write(9, 110);
  }

  // *********************************
  // Setting up IP addresses, starting Ethernet
  // *********************************

  // Setting the IP address:
  ip[0] = EEPROM.read(0 + 2);
  ip[1] = EEPROM.read(1 + 2);
  ip[2] = EEPROM.read(2 + 2);
  ip[3] = EEPROM.read(3 + 2);

  // Setting the ATEM IP address:
  atem_ip[0] = EEPROM.read(0 + 2 + 4);
  atem_ip[1] = EEPROM.read(1 + 2 + 4);
  atem_ip[2] = EEPROM.read(2 + 2 + 4);
  atem_ip[3] = EEPROM.read(3 + 2 + 4);

  // Setting MAC address:
  mac[0] = EEPROM.read(10);
  mac[1] = EEPROM.read(11);
  mac[2] = EEPROM.read(12);
  mac[3] = EEPROM.read(13);
  mac[4] = EEPROM.read(14);
  mac[5] = EEPROM.read(15);
  char buffer[18];
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial << F("SKAARHOJ device MAC address: ") << buffer << "\n";

  /*
    Serial << F("SKAARHOJ device MAC address: ") << buffer << F(" - Checksum: ")
      << ((mac[0]+mac[1]+mac[2]+mac[3]+mac[4]+mac[5]) & 0xFF);
  */

  Serial << F(" SKAARHOJ device IP Address: ") << ip[0] << "." << ip[1] << "." << ip[2] << "." << ip[3] << "\n";
  Serial << F("   ATEM switcher IP Address: ") << atem_ip[0] << "." << atem_ip[1] << "." << atem_ip[2] << "." << atem_ip[3] << "\n";

  if ((uint8_t)EEPROM.read(16) != ((mac[0] + mac[1] + mac[2] + mac[3] + mac[4] + mac[5]) & 0xFF))  {
    Serial << F("MAC address not found in EEPROM memory!\n") <<
           F("Please load example sketch ConfigEthernetAddresses to set it.\n");
    /*
      F("The MAC address is found on the backside of your Ethernet Shield/Board\n (STOP)");
    */    while (true);

  }

  // Update the boot counter:
  EEPROM.write(17, EEPROM.read(17) + 1);

  // Start the Ethernet, Serial (debugging) and UDP:
  Ethernet.begin(mac, ip);

  // Sets the Bi-color LED to off = "no connection"
  digitalWrite(greenLED, false);
  digitalWrite(redLED, false);
  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);

  // *********************************
  // Final Setup based on mode
  // *********************************
  if (isConfigMode)  {
    Serial.println();
    Serial.println("    ^^^ ABOVE settings are current! ^^^");
    Serial.println();
    Serial.println("******* Config Mode *******");
    Serial.println();
    Serial.println(" In this mode one can setup IP address of this device and ATEM switcher over serial console.");
    Serial.println();
    Serial.println(" EXAMPLE for device IP: D_192.168.0.1 [enter]");
    Serial.println();
    Serial.println("   EXAMPLE for ATEM IP: A_192.168.0.1 [enter]");
    Serial.println();
    Serial.println("After input the device will reply if data has been successfully accepted/stored.");
    Serial.println();
    Serial.println("Reboot device to apply settings!");
    Serial.println();
  }
  else {

    // Initialize a connection to the switcher:
    AtemSwitcher.begin(IPAddress(atem_ip[0], atem_ip[1], atem_ip[2], atem_ip[3]), 56417);    // <= SETUP!
    // AtemSwitcher.serialOutput(true);
    AtemSwitcher.connect();

    // Set Bi-color LED orange - indicates "connecting...":
    digitalWrite(greenLED, true);
    digitalWrite(redLED, true);
  }
  Serial << F("Setup DONE!\n- - - - - - - -\n\n");
}


bool AtemOnline = false;
bool displayed = false;

void loop() {
  if (!isConfigMode)  {

    // Check for packets, respond to them etc. Keeping the connection alive!
    AtemSwitcher.runLoop();

    // If the switcher has been initialized, check for button presses as reflect status of switcher in button lights:
    if (AtemSwitcher.hasInitialized())  {
      if (!AtemOnline)  {
        AtemOnline = true;
        // Set Bi-color LED to red or green depending on mode:
        digitalWrite(redLED, false);
        digitalWrite(greenLED, true);
      }


      DejanTallyLights();

    }

    // If connection is gone anyway, try to reconnect:
    else  {
      if (AtemOnline)  {
        AtemOnline = false;

        // Set Bi-color LED off = "no connection"
        digitalWrite(greenLED, false);
        digitalWrite(redLED, false);
        Serial.println("Atem not online!");
      }

      //Serial.println("Connection to ATEM Switcher has timed out - reconnecting!");

      // Set Bi-color LED orange - indicates "connecting...":
      digitalWrite(greenLED, true);
      digitalWrite(redLED, true);
    }
  }

  // config MODE
  else {
    if (!displayed) Serial.println("Config MODE - please enter new IP's or change A1 pin mode and reboot");
    displayed = true;

    while (Serial.available() > 0 ) {
      String str = Serial.readString();
      if (str.charAt(0) == 'D' && str.charAt(1) == '_') {
        Serial.println("identified D");
        parseInput(str);
        Serial << F("new device IP: ") << ipc[0] << F(".") << ipc[1] << F(".") << ipc[2] << F(".") << ipc[3] << F("\n");

        // Set NEW IP address for Arduino panel
        EEPROM.write(2, ipc[0]);
        EEPROM.write(3, ipc[1]);
        EEPROM.write(4, ipc[2]);
        EEPROM.write(5, ipc[3]);
        Serial.println("data SAVED!");
      }
      else if (str.charAt(0) == 'A' && str.charAt(1) == '_') {
        Serial.println("identified A");
        parseInput(str);
        Serial << F("new ATEM IP: ") << ipc[0] << F(".") << ipc[1] << F(".") << ipc[2] << F(".") << ipc[3] << F("\n");

        // Set NEW IP address for Arduino panel
        EEPROM.write(6, ipc[0]);
        EEPROM.write(7, ipc[1]);
        EEPROM.write(8, ipc[2]);
        EEPROM.write(9, ipc[3]);
        Serial.println("data SAVED!");
      }

      else {
        Serial.println("invalid data received...");
      }
    }
  }
}

void DejanTallyLights() {
  // get tally 2x
  programTally = AtemSwitcher.getProgramInputVideoSource(0);

  // zapakirati v en byte in poslati nazaj - TODO
  //
  Serial.print("Svetim rdece program: ");
  Serial.println(programTally);
  if (AtemSwitcher.getTransitionInTransition(0)) {
    transitionTally = AtemSwitcher.getPreviewInputVideoSource(0);
    Serial.print("Svetim rdece ker v transitionu: ");
    Serial.println(transitionTally);
  }
  Serial.println();
}

void parseInput(String str) {
  ind1 = str.indexOf('.');                          //finds location of first
  temp = str.substring(2, ind1);                    //captures first data String
  ipc[0] = temp.toInt();

  ind2 = str.indexOf('.', ind1 + 1 );               //finds location of second
  temp = str.substring(ind1 + 1, ind2);             //captures second data String
  ipc[1] = temp.toInt();

  ind3 = str.indexOf('.', ind2 + 1 );
  temp = str.substring(ind2 + 1, ind3);
  ipc[2] = temp.toInt();

  ind4 = str.indexOf('.', ind3 + 1 );
  temp = str.substring(ind3 + 1);                   //captures remain part of data after last
  ipc[3] = temp.toInt();
}
