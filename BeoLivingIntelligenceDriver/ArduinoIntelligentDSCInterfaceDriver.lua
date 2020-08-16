--- DRIVER SPECIFICATION ---                            
driver_label = "Arduino DSC PowerSeries Security (Community)"
driver_default_channel= "RS232"
driver_channels = {   TCP(8002, "172.16.5.20", "RS232 over Ethernet", "RS232 over Ethernet connection", {} ),
	rs232("DSCChannel", "rs232", "DSCChannel", "RS232 channel to alarm system", { roNumericArgument("timeBetweenMessages",300000) }) }  

driver_load_system_help= "Discover zones"
driver_help = [[
Arduino DSC PowerSeries 
======================= 

Communicates with DSC PowerSeries Security systems using the *Arduino DSC Interface* . Directly connect the Arduino to the USB port of the BeoLiving Intelligence or using an ethernet interface.

Allows the user to Arm/Disarm partitions of the alarm system, and to receive feedback of states and notifications. 

Resources
=========

ALARM
-----

+ Address: id of the partition [1-8]

+ States:

 - MODE: ARM AWAY, ARM HOME, DISARM
 - ALARM: The alarm is ringing 
 - READY: Alarm ready to be armed   

+ Commands:

 - ARM: arm partition 
     - Arguments:
         - MODE: AWAY, HOME
         - CODE: 4 digits code or 6 digit code
 - DISARM: disarm partition
     - Arguments:
         - CODE: 4 digits code or 6 digit code

+ Events:

 - MODE: ARM AWAY, ARM HOME, DISARM
 - ALARM: The alarm is ringing 
 - READY: Alarm ready to be armed  


ALARM\_ZONE
----------

+ Address: id of the zone [1-128]. Must have 3 digits, example: 001,012,128,...

+ States:

 - OPEN: true if zone is open, false if closed
 - TROUBLE: true if zone has some trouble, false otherwise
 - ALARM: true if this zone has triggered the alarm
 - TAMPER: true if zone has been tampered with, false otherwise


Channel
-------
+ Baudrate: 9600
+ Fc mode:  none


Changelog
=========

- v0.2 | 2019-06-26
    - Removes some logs message.

]]
driver_min_blgw_version= "1.5.0"
driver_version= "0.2"
        
                           
--- CONSTANTS ---
local ALARM = "ALARM"
local ALARM_ZONE = "ALARM_ZONE"
local timeout = 3
local terminator  = "\r\n"
local reserved    = "00"
local numberOfPartitions = 8
local fixedLength = 8 ---> (length + msg type + submsg type + reserved + checksum)
local armingStatusProcessed = "armingStatusProcessed"
local zoneStatusProcessed = "zoneStatusProcessed"
local readyForNextCommandProcessed = "readyForNextCommandProcessed"
local eventProcessed = "eventProcessed"
local zonePartitionsProcessed = "zonePartitionsProcessed"
local outputResourceAddress = "Output"

--- VARIABLES ---
local partitionAlarmState
local needToUpdateAlarmStates
local needToUpdateZoneStates
local loadZones


--- RESOURCE TYPES ---
resource_types = {
  
  
  --- ALARM RESOURCE
  [ALARM] = {
    standardResourceType =  ALARM,
    address = {
	  stringArgumentRegEx("address", "1", "[1-8]", {context_help= "Partition number: [1-8]"})
	},
	
    states = {
      enumArgument("MODE", {"ARM AWAY","ARM HOME","DISARM"},"DISARM"),
      boolArgument("ALARM",false),
      boolArgument("READY",true)
    },
    
    commands = {
    
      ["ARM"] = { 
        arguments= {
          stringArgumentRegEx("CODE", "1234", "[0-9]*", {context_help= "digits code"}),
          enumArgument("MODE", {"AWAY","HOME", "_ARM_AWAY", "_ARM_STAY", "_ARM_INSTANT"},"AWAY")
        }
      },
      
      ["DISARM"] = { 
        arguments= {
          stringArgumentRegEx("CODE", "1234", "[0-9][0-9][0-9][0-9]", {context_help= "4 digits code"})
        }
      },    
    },
    
    events = {
      ["_eventNotification"] = {
        arguments = {
          stringArgumentMinMax("_code", "  ", 2, 2, {context_help= "See alarm documentation for event code's description (2 characters)"}),
          stringArgumentMinMax("_zone", "000", 3, 3, {context_help= "Zone number (3 digits)"}),
          --[[stringArgumentMinMax("_user", "000", 3, 3, {context_help= "User number (deprecated) (3 digits)"}),]]
          stringArgumentRegEx("_timeStamp", "00/00/00/00/00", "[0-9][0-9]/[0-9][0-9]/[0-9][0-9]/[0-9][0-9]/[0-9][0-9]", {context_help= "Format: mm/hh/dd/MM/YY (minute/hour/day/month/year"}) 
        }  
      }  
    }
    
  },
  
  --- ZONE RESOURCE 
  [ALARM_ZONE] = {
    standardResourceType =  ALARM_ZONE,
    address = {
      stringArgumentRegEx("address", "1", "[0-9]\\|[0-9][0-9]\\|1[0-1][0-9]\\|12[0-8]", {context_help= "Zone number"}) 

	},
	
    states = {
      boolArgument("OPEN",true),
      boolArgument("TROUBLE",false),
      boolArgument("ALARM",false),
      boolArgument("_TAMPER",false),
      boolArgument("BYPASSED",false),
    }
    
  }
  

}

-- Helper functinos
local function startsWith(str, start)
  if (not str) then 
        Trace("startsWith: null, " .. start ) 
  elseif (not start) then
        Trace("startsWith: " .. str .. ", null" ) 
  elseif start then
    Trace("startsWith: input: " .. str .. ", test:" .. start ) 
    return str:sub(1, #start)  == start
  else 
    return false
  end
end

  

local function sendCommand(sendData)
  Trace("SEND: "..sendData)
  channel.write(sendData) 
end




local function readMessage()
 
end



local MSG_PARTITION_ ="S:P:"
local MSG_PARTITION_ARMED_AWAY =":ARM:A"
local MSG_PARTITION_ARMED_HOME =":ARM:S"

local MSG_PARTITION_DISARMED =":DISARM"
local MSG_PARTITION_ENDTRY_DELAY =":ENTRY_DELAY"
local MSG_PARTITION_EXIT_DELAY =":EXIT_DELAY"

local MSG_PARTITION_ALARM =":ALARM"
local MSG_PARTITION_NOT_READY =":NREADY"
local MSG_PARTITION_READY =":READY"
--"Timestamp: 2019.10.05 11:04"

local MSG_ZONE_ALARM_RESTORED = "S:Z:NA:"
local MSG_ZONE_ALARM = "S:Z:A:"
local MSG_ZONE_OPEN = "S:Z:O:"
local MSG_ZONE_RESTORED = "S:Z:R:"

local MSG_AC_POWER_TROUBLE = "Panel AC power trouble"
local MSG_AC_POWER_RESTORED = "Panel AC power restored"
local MSG_BATTERY_TROUBLE = "Panel battery trouble"
local MSG_BATTERY_RESTORED = "Panel battery restored"



local function getAddress( msgType, msg )
  local submsg = string.sub(msg, #msgType)
  return string.sub(submsg, string.find(submsg, "%d+"))
end


local function processMessage(data)
   if (#data > 3) then 
   -- strip terminator
   local msg = data:sub(1, #data - #terminator)
   data = msg

    --- update alarm states

    if startsWith(data, MSG_ZONE_ALARM) then -- == "601" then --Zone Alarm
        setResourceState(ALARM_ZONE, {address= getAddress(MSG_ZONE_ALARM,data)}, {["ALARM"] = true})
       -- setResourceState(ALARM, {address= data:sub(4,4)}, {["ALARM"] = true})
    
    elseif startsWith(data, MSG_ZONE_ALARM_RESTORED) then --Zone Alarm Restored  
        setResourceState(ALARM_ZONE, {address= getAddress(MSG_ZONE_ALARM_RESTORED,data)}, {["ALARM"] = false})
      
    elseif startsWith(data, MSG_ZONE_OPEN) then --Zone Open
        setResourceState(ALARM_ZONE, {address= getAddress(MSG_ZONE_OPEN,data)}, {["OPEN"] = true})
    
    elseif startsWith(data, MSG_ZONE_RESTORED) then --Zone closed  
        setResourceState(ALARM_ZONE, {address= getAddress(MSG_ZONE_RESTORED,data)}, {["OPEN"] = false})

    elseif startsWith(data, MSG_PARTITION_ ) then -- Partition MSG
       local number = getAddress(MSG_PARTITION_ , data)
       local action = data:sub(#MSG_PARTITION_ + #number + 1)
       Trace("Address: '" .. number .. "' changes state to '" .. action .. "'")
       if startsWith(action, MSG_PARTITION_READY ) then -- Partition Ready
         setResourceState(ALARM, {address= number}, {["READY"] = true})    
      elseif startsWith(action, MSG_PARTITION_NOT_READY )  then -- Partition Not Ready 
        setResourceState(ALARM, {address= number}, {["READY"] = false})

     
     elseif startsWith(action, MSG_PARTITION_ARMED_AWAY ) then -- Partition Armed away
      	   setResourceState(ALARM, {address= number}, {["MODE"] = "ARM AWAY"}) 
     elseif startsWith(action, MSG_PARTITION_ARMED_HOME ) then -- Partition Armed home
      	   setResourceState(ALARM, {address= number}, {["MODE"] = "ARM HOME"}) 
     elseif startsWith(action, MSG_PARTITION_ALARM ) then -- Partition Armed home
           setResourceState(ALARM, {address= number}, {["ALARM"] = true})
     elseif startsWith(action, MSG_PARTITION_DISARMED ) then        
       setResourceState(ALARM, {address= number}, {["MODE"] = "DISARM"})
       setResourceState(ALARM, {address= number}, {["ALARM"] = false})
     elseif startsWith(action, MSG_PARTITION_EXIT_DELAY ) then 
       setResourceState(ALARM, {address= number}, {["MODE"] = "ARM AWAY"}) 
     elseif startsWith(action, MSG_PARTITION_ENTRY_DELAY ) then 
      -- setResourceState(ALARM, {address= number}, {["MODE"] = "ARM AWAY"}) 
     end
        
  	end
    end
end
    
   
  

--- EXECUTE COMMAND ---
function executeCommand(command, resource, commandArgs)
   Trace("EXEC: "..command)

   if command == "ARM" or command == "DISARM" then
    local subCommand = ""
    local codedata   = ""

    codedata = commandArgs["CODE"]

    
    if command == "ARM" then
      if commandArgs["MODE"] == "AWAY" then
        subCommand = "w"
      elseif commandArgs["MODE"] == "HOME" then
        subCommand = "s"
      elseif commandArgs["MODE"] == "_ARM_AWAY" then
        subCommand = "w" -- FXIME: Set partition pending
      elseif commandArgs["MODE"] == "_ARM_STAY" then
        subCommand = "s" -- FXIME: Set partition pending
      elseif commandArgs["MODE"] == "_ARM_INSTANT" then
        subCommand = "w" .. resource.address -- no user code required to send this command
      end
    elseif command == "DISARM" then
      subCommand = codedata .. "#"
    end  	
    sendCommand(subCommand)

  end
  
end








function onResourceUpdate(resource)
  sendCommand("!") --Request status update from Security system
end
  

function requestResources()
  loadZones = true
  sendCommand("!") --Request status update from Security system
  return true, -1
end


--- PROCESS ---
function process ()
  
    --- INITIALIZATION ---
    needToUpdateZoneStates = true

    driver.setOnline()
  
    while channel.status() do
      if needToUpdateZoneStates then
        needToUpdateZoneStates = false
        sendCommand("!") -- Request status update from Security system
      end
          
      local code, data = channel.readUntil(terminator,timeout)
      if code == CONST.OK and #data > 0 then
         Trace("RECEIVED: "..data)
          processMessage(data)
       end  
    end
  
    channel.retry("Connection failed, retrying in 10 seconds", 10)
    driver.setError()
    return CONST.HW_ERROR
end

