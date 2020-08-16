/*
 *  DSC Status 1.1 (esp8266)
 *
 *  Processes and prints the security system status to a serial interface, including reading from serial for the
 *  virtual keypad.  This demonstrates how to determine if the security system status has changed, what has
 *  changed, and how to take action based on those changes.
 *
 *  Release notes:
 *    1.1 - Added partition ready, access code, and timestamp status
 *    1.0 - Initial release
 *
 *  Wiring:
 *      DSC Aux(+) --- 5v voltage regulator --- esp8266 development board 5v pin (NodeMCU, Wemos)
 *
 *      DSC Aux(-) --- esp8266 Ground
 *
 *                                         +--- dscClockPin (esp8266: D1, D2, D8)
 *      DSC Yellow --- 15k ohm resistor ---|
 *                                         +--- 10k ohm resistor --- Ground
 *
 *                                         +--- dscReadPin (esp8266: D1, D2, D8)
 *      DSC Green ---- 15k ohm resistor ---|
 *                                         +--- 10k ohm resistor --- Ground
 *
 *  Virtual keypad (optional):
 *      DSC Green ---- NPN collector --\
 *                                      |-- NPN base --- 1k ohm resistor --- dscWritePin (esp8266: D1, D2, D8)
 *            Ground --- NPN emitter --/
 *
 *  Virtual keypad uses an NPN transistor to pull the data line low - most small signal NPN transistors should
 *  be suitable, for example:
 *   -- 2N3904
 *   -- BC547, BC548, BC549
 *
 *  Issues and (especially) pull requests are welcome:
 *  https://github.com/taligentx/dscKeybusInterface
 *
 *  This example code is in the public domain.
 */

#include <dscKeybusInterface.h>
#include <ESP8266WiFi.h>

// Configures the Keybus interface with the specified pins - dscWritePin is optional, leaving it out disables the
// virtual keypad.
#define dscClockPin D1  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
#define dscReadPin D2   // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
#define dscWritePin D8  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)

// Initialize components
dscKeybusInterface dsc(dscClockPin, dscReadPin, dscWritePin);


void setup() {
  // Turn of WIFI because we are not using it ref: https://www.bakke.online/index.php/2017/05/21/reducing-wifi-power-consumption-on-esp8266-part-2/
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();
  delay( 1 );

  // setup serial
  Serial.begin(9600);
  Serial.println();
  Serial.println();

  // Starts the Keybus interface and optionally specifies how to print data.
  // begin() sets Serial by default and can accept a different stream: begin(Serial1), etc.
  dsc.begin();

  Serial.println(F("DSC Keybus Interface is online."));
}


void loop() {
  bool forceStatus = false;

  // Reads from serial input and writes to the Keybus as a virtual keypad
  if (Serial.available() > 0) {
    byte c = Serial.read();
      if (c == '!') {
        //force sync status
          forceStatus = true;      
      } else {   
        dsc.write(c);
      }
  }

  dsc.loop();

  if (dsc.statusChanged || forceStatus) {      // Checks if the security system status has changed
    dsc.statusChanged = false;  // Reset the status tracking flag

    // If the Keybus data buffer is exceeded, the sketch is too busy to process all Keybus commands.  Call
    // loop() more often, or increase dscBufferSize in the library: src/dscKeybusInterface.h
    if (dsc.bufferOverflow) {
      Serial.println(F("L:Keybus buffer overflow"));
      dsc.bufferOverflow = false;
    }

    // Checks if the interface is connected to the Keybus
    if (dsc.keybusChanged || forceStatus) {
      dsc.keybusChanged = false;                 // Resets the Keybus data status flag
      if (dsc.keybusConnected) Serial.println(F("Keybus connected"));
      else Serial.println(F("L:Keybus disconnected"));
    }

    // Checks status per partition
    for (byte partition = 0; partition < dscPartitions; partition++) {

      // Skips processing if the partition is disabled or in installer programming
      if (dsc.disabled[partition]) continue;

      // Checks ready status
      if (dsc.readyChanged[partition]  || forceStatus) {
        dsc.readyChanged[partition] = false;  // Resets the partition ready status flag
        if (dsc.ready[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":READY"));
        }
        else {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":NREADY"));
        }
      }

      // Checks armed status
      if (dsc.armedChanged[partition] || forceStatus) {
        dsc.armedChanged[partition] = false;  // Resets the partition armed status flag
        if (dsc.armed[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.print(F(":ARM:"));
          if (dsc.armedAway[partition]) Serial.println(F("A"));
          if (dsc.armedStay[partition]) Serial.println(F("S"));
        }
        else {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":DISARM"));
        }
      }

      // Checks alarm triggered status
      if (dsc.alarmChanged[partition] || forceStatus) {
        dsc.alarmChanged[partition] = false;  // Resets the partition alarm status flag
        if (dsc.alarm[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":ALARM"));
        }
      }

      // Checks exit delay status
      if (dsc.exitDelayChanged[partition] || forceStatus) {
        dsc.exitDelayChanged[partition] = false;  // Resets the exit delay status flag
        if (dsc.exitDelay[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":EXIT_DELAY"));
        }
        else if (!dsc.armed[partition]) {  // Checks for disarm during exit delay
          Serial.print(F("S:P:"));
          Serial.print(partition +1);
          Serial.println(F(":DISARM"));
        }
      }

      // Checks entry delay status
      if (dsc.entryDelayChanged[partition] || forceStatus) {
        dsc.entryDelayChanged[partition] = false;  // Resets the exit delay status flag
        if (dsc.entryDelay[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":ENTRY_DELAY"));
        }
      }

      // Checks the access code used to arm or disarm
      if (dsc.accessCodeChanged[partition] || forceStatus) {
        dsc.accessCodeChanged[partition] = false;  // Resets the access code status flag
        Serial.print(F("S:P:"));
        Serial.print(partition + 1);
        switch (dsc.accessCode[partition]) {
          case 33: Serial.print(F(":duress")); break;
          case 34: Serial.print(F(":duress")); break;
          case 40: Serial.print(F(":master")); break;
          case 41: Serial.print(F(":supervisor")); break;
          case 42: Serial.print(F(":supervisor")); break;
          default: Serial.print(F(":user")); break;
        }
        Serial.print(F(":code:"));
        Serial.println(dsc.accessCode[partition]);
      }

      // Checks fire alarm status
      if (dsc.fireChanged[partition] || forceStatus) {
        dsc.fireChanged[partition] = false;  // Resets the fire status flag
        if (dsc.fire[partition]) {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":fire alarm on"));
        }
        else {
          Serial.print(F("S:P:"));
          Serial.print(partition + 1);
          Serial.println(F(":fire alarm restored"));
        }
      }
    }

    // Checks for open zones
    // Zone status is stored in the openZones[] and openZonesChanged[] arrays using 1 bit per zone, up to 64 zones
    //   openZones[0] and openZonesChanged[0]: Bit 0 = Zone 1 ... Bit 7 = Zone 8
    //   openZones[1] and openZonesChanged[1]: Bit 0 = Zone 9 ... Bit 7 = Zone 16
    //   ...
    //   openZones[7] and openZonesChanged[7]: Bit 0 = Zone 57 ... Bit 7 = Zone 64
    if (dsc.openZonesStatusChanged || forceStatus) {
      dsc.openZonesStatusChanged = false;                           // Resets the open zones status flag
      for (byte zoneGroup = 0; zoneGroup < dscZones; zoneGroup++) {
        for (byte zoneBit = 0; zoneBit < 8; zoneBit++) {
          if (bitRead(dsc.openZonesChanged[zoneGroup], zoneBit) || forceStatus) {  // Checks an individual open zone status flag
            bitWrite(dsc.openZonesChanged[zoneGroup], zoneBit, 0);  // Resets the individual open zone status flag
            delay(5); // slow down reporting to protect buffers

            if (bitRead(dsc.openZones[zoneGroup], zoneBit)) {       // Zone open

              Serial.print(F("S:Z:O:"));
              Serial.println(zoneBit + 1 + (zoneGroup * 8));        // Determines the zone number
            }
            else {                                                  // Zone closed
              Serial.print(F("S:Z:R:"));
              Serial.println(zoneBit + 1 + (zoneGroup * 8));        // Determines the zone number
            }
          }
        }
      }
    }

    // Checks for zones in alarm
    // Zone alarm status is stored in the alarmZones[] and alarmZonesChanged[] arrays using 1 bit per zone, up to 64 zones
    //   alarmZones[0] and alarmZonesChanged[0]: Bit 0 = Zone 1 ... Bit 7 = Zone 8
    //   alarmZones[1] and alarmZonesChanged[1]: Bit 0 = Zone 9 ... Bit 7 = Zone 16
    //   ...
    //   alarmZones[7] and alarmZonesChanged[7]: Bit 0 = Zone 57 ... Bit 7 = Zone 64
    if (dsc.alarmZonesStatusChanged || forceStatus) {
      dsc.alarmZonesStatusChanged = false;                           // Resets the alarm zones status flag
      for (byte zoneGroup = 0; zoneGroup < dscZones; zoneGroup++) {
        for (byte zoneBit = 0; zoneBit < 8; zoneBit++) {
          if (bitRead(dsc.alarmZonesChanged[zoneGroup], zoneBit) || forceStatus) {  // Checks an individual alarm zone status flag
            bitWrite(dsc.alarmZonesChanged[zoneGroup], zoneBit, 0);  // Resets the individual alarm zone status flag
            delay(5); // slow down reporting to protect buffers

            if (bitRead(dsc.alarmZones[zoneGroup], zoneBit)) {       // Zone alarm
              Serial.print(F("S:Z:A:"));
              Serial.println(zoneBit + 1 + (zoneGroup * 8));         // Determines the zone number
            }
            else {
              Serial.print(F("S:Z:NA:"));
              Serial.println(zoneBit + 1 + (zoneGroup * 8));         // Determines the zone number
            }
          }
        }
      }
    }

    // Checks for a panel timestamp
    //
    // The panel time can be set using dsc.setTime(year, month, day, hour, minute, "accessCode") - for example:
    //   dsc.setTime(2015, 10, 21, 7, 28, "1234")  # Sets 2015.10.21 07:28 with access code 1234
    //
    if (dsc.timestampChanged || forceStatus) {
      dsc.timestampChanged = false;
      Serial.print(F("T:"));
      Serial.print(dsc.year);                  // Returns year as a 4-digit unsigned int
      Serial.print(".");
      if (dsc.month < 10) Serial.print("0");
      Serial.print(dsc.month);                 // Returns month as a byte
      Serial.print(".");
      if (dsc.day < 10) Serial.print("0");
      Serial.print(dsc.day);                   // Returns day as a byte
      Serial.print(" ");
      if (dsc.hour < 10) Serial.print("0");
      Serial.print(dsc.hour);                  // Returns hour as a byteCompra
      Serial.print(":");
      if (dsc.minute < 10) Serial.print("0");
      Serial.println(dsc.minute);              // Returns minute as a byte
    }

    // Checks trouble status  
    if (dsc.troubleChanged || forceStatus) {
      dsc.troubleChanged = false;  // Resets the trouble status flag
      if (dsc.trouble) Serial.println(F("Trouble status on"));
      else Serial.println(F("Trouble status restored"));
    }

    // Checks AC power status
    if (dsc.powerChanged || forceStatus) {
      dsc.powerChanged = false;  // Resets the power trouble status flag
      if (dsc.powerTrouble) Serial.println(F("Panel AC power trouble"));
      else Serial.println(F("Panel AC power restored"));
    }

    // Checks panel battery status
    if (dsc.batteryChanged || forceStatus) {
      dsc.batteryChanged = false;  // Resets the battery trouble status flag
      if (dsc.batteryTrouble) Serial.println(F("Panel battery trouble"));
      else Serial.println(F("Panel battery restored"));
    }

    // Checks keypad fire alarm triggered
    if (dsc.keypadFireAlarm) {
      dsc.keypadFireAlarm = false;  // Resets the keypad fire alarm status flag
      Serial.println(F("Keypad fire alarm"));
    }

    // Checks keypad auxiliary alarm triggered
    if (dsc.keypadAuxAlarm) {
      dsc.keypadAuxAlarm = false;  // Resets the keypad auxiliary alarm status flag
      Serial.println(F("Keypad aux alarm"));
    }

    // Checks keypad panic alarm triggered
    if (dsc.keypadPanicAlarm) {
      dsc.keypadPanicAlarm = false;  // Resets the keypad panic alarm status flag
      Serial.println(F("Keypad panic alarm"));
    }
  }
}
