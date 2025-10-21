/**
 * @file       codeFINAL_TTS_withBatteryAndSpecialCharactersAndLearningMode.ino
 * @authors    Imperial, Kendra Angela S. and Sioco, Princess R.
 * @copyright  Copyright (c) 2025 Imperial & Sioco
 * @date       October 2025
 */


#include <Adafruit_MCP23017.h>
#include <Wire.h>
#include <utilities.h>
#include <TinyGsmClient.h>
#include <StreamDebugger.h>
#include <U8g2lib.h>

#define SerialMon Serial
#define TINY_GSM_DEBUG SerialMon
#define DUMP_AT_COMMANDS

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_MCP23017 mcp;

#define SLEEP_PIN 0        //Sleep button
#define BUTTON_MODE_PIN 5  //Mode switcher
#define AUX_A_PIN 1        //Select/Reply/Space
#define AUX_B_PIN 2        //Prev/Back/Mode
#define AUX_C_PIN 3        //Add/Delete/Backspace
#define AUX_D_PIN 4        //Next/Read/Send

const int braillePins[6] = { 32, 33, 15, 14, 13, 12 };  //Braille input

StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);

//for TTS interrupt
volatile bool isSpeaking = false;
String nextSpeech = "";
volatile bool hasNextSpeech = false;

void speakLater(const String& text) {
  nextSpeech = text;
  hasNextSpeech = true;
}

void pollTTS() {
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    if (line.indexOf("+CTTS: 0") != -1) {   // finished or stopped
      isSpeaking = false;
      // If something is queued, say it now
      if (hasNextSpeech) {
        String toSay = nextSpeech;
        nextSpeech = "";
        hasNextSpeech = false;
        speak(toSay);         // will set isSpeaking = true again
      }
    }

    //NEW SMS arrived and stored (URC)
    else if (line.startsWith("+CMTI:")) {
      // +CMTI: "SM",<index>
      int comma = line.indexOf(',');
      int simIndex = (comma != -1) ? line.substring(comma + 1).toInt() : -1;
      onNewSms(simIndex);
    }
  }
}

//>>>>>>>OLED idle/sleep<<<<<<<<<<
unsigned long lastActivityMs = 0;
const unsigned long IDLE_TIMEOUT_MS = 60000; //60s
bool screenSleeping = false;

//Modes
enum MainMode {
  INBOX_MODE,
  CONTACTS_MODE,
  MESSAGING_MODE,
  LEARNING_MODE
};
MainMode mainMode = INBOX_MODE;

enum InboxState {
  INBOX_LIST,
  MESSAGE_SELECTED
};
InboxState inboxState = INBOX_LIST;

enum ContactsState {
  CONTACTS_LIST,
  CONTACT_SELECTED,
  ADD_NUMBER,
  ADD_NAME
};
ContactsState contactsState = CONTACTS_LIST;

enum MessagingState {
  MESSAGE_COMPOSE
};
MessagingState messagingState = MESSAGE_COMPOSE;

enum ConfirmationState {
  NO_CONFIRMATION,
  CONFIRM_NUMBER,
  CONFIRM_NAME,
  CONFIRM_MESSAGE
};
ConfirmationState confirmationState = NO_CONFIRMATION;

enum InputMode {
  LETTER_MODE,
  NUMBER_MODE,
  SPECIAL_CHAR_MODE
};

char brailleToChar(String pattern, InputMode mode);
InputMode inputMode = LETTER_MODE;

enum LearningState {
  LEARNING_IDLE,
  LEARNING_INPUT,
  LEARNING_RESULT
};
LearningState learningState = LEARNING_IDLE;

String learningInput = "";
String learningResult = "";
unsigned long lastLearningInputTime = 0;
#define LEARNING_TIMEOUT 5000
#define LEARNING_DOT_RADIUS 3
#define LEARNING_DOT_SPACING 8

//Reply, Compose, and Add Contact buffers
String replyNumber = "";
String messageText = "";
String addContactNumber = "";
String addContactName = "";

unsigned long lastSpeakTime = 0;
unsigned long speakInterval = 500;

// Battery monitoring
int batteryPercentage = 0;
unsigned long lastBatteryCheck = 0;
bool lowBatteryWarningGiven = false;
bool criticalBatteryWarningGiven = false;
uint16_t batteryVoltage = 0;
uint8_t batteryChargeState = 0;
bool usingModemBattery = false;

#define BATTERY_PIN BOARD_BAT_ADC_PIN
#define BATTERY_CHECK_INTERVAL 30000
#define MAX_BATTERY_VOLTAGE 4200
#define MIN_BATTERY_VOLTAGE 3200
#define VOLTAGE_DIVIDER_RATIO 2
#define REFERENCE_VOLTAGE 3300

#define MAX_MESSAGES 40
#define MAX_CONTACTS 250

struct SmsEntry {
  int cmglIndex;
  String sender;
  String date;
  String message;
};
SmsEntry messages[MAX_MESSAGES];
int totalMessages = 0;
int currentMessageIndex = 0;

struct Contact {
  String name;
  String number;
};
Contact contacts[MAX_CONTACTS];
int totalContacts = 0;
int currentContactIndex = 0;

struct BrailleMapping {
  const char* pattern;
  char character;
};
BrailleMapping brailleAlphabet[] = {
  { "100000", 'A' },  //1
  { "110000", 'B' },  //2
  { "100100", 'C' },  //3
  { "100110", 'D' },  //4
  { "100010", 'E' },  //5
  { "110100", 'F' },  //6
  { "110110", 'G' },  //7
  { "110010", 'H' },  //8
  { "010100", 'I' },  //9
  { "010110", 'J' },  //0
  { "101000", 'K' },
  { "111000", 'L' }, { "101100", 'M' }, { "101110", 'N' }, { "101010", 'O' }, { "111100", 'P' }, 
  { "111110", 'Q' }, { "111010", 'R' }, { "011100", 'S' }, { "011110", 'T' }, { "101001", 'U' },
  { "111001", 'V' }, { "010111", 'W' }, { "101101", 'X' }, { "101111", 'Y' }, { "101011", 'Z' }
};

const int brailleAlphabetSize = sizeof(brailleAlphabet) / sizeof(brailleAlphabet[0]);

BrailleMapping brailleSpecialChars[] = {
  { "010011", ',' },   // Comma
  { "010001", '.' },   // Period
  { "011011", '?' },   // Question mark
  { "011001", '!' },   // Exclamation mark
  { "010010", ';' },   // Semicolon
  { "011010", ':' },   // Colon
  { "010101", '-' },   // Hyphen
  { "011101", '\'' },  // Apostrophe
  { "010111", '"' },   // Quotation mark
  { "011000", '@' },   // At symbol (common in emails)
  { "001111", '#' },   // Hash/Pound
  { "001110", '$' },   // Dollar sign
  { "101111", '%' },   // Percent
  { "111101", '&' },   // Ampersand
  { "011110", '*' },   // Asterisk
  { "110101", '+' },   // Plus
  { "111111", '=' },   // Equals
  { "110011", '/' },   // Forward slash
  { "001100", '\\' },  // Backslash
  { "101011", '(' },   // Open parenthesis
  { "111011", ')' },   // Close parenthesis
  { "010110", '[' },   // Open bracket
  { "110110", ']' },   // Close bracket
  { "100111", '{' },   // Open brace
  { "110111", '}' },   // Close brace
  { "011111", '<' },   // Less than
  { "111110", '>' }    // Greater than
};

const int brailleSpecialCharsSize = sizeof(brailleSpecialChars) / sizeof(brailleSpecialChars[0]);

String monthToString(int month) {
  const char* months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
  };
  if (month >= 1 && month <= 12) {
    return months[month - 1];
  }
  return "";
}

String numberToWords(int num) {
  String units[] = {"", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  String teens[] = {"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  String tens[]  = {"", "ten", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

  if (num == 0) return "zero";
  if (num < 10) return units[num];
  if (num < 20) return teens[num - 10];
  if (num < 100) {
  if (num % 10 == 0) return tens[num / 10];
    return tens[num / 10] + " " + units[num % 10];
  }
  if (num < 1000) {
    if (num % 100 == 0) return units[num / 100] + " hundred";
    return units[num / 100] + " hundred " + numberToWords(num % 100);
  }
  return String(num); // fallback for numbers >= 100
}

String yearToString(String yy) {
  int year = yy.toInt();
  if (year == 0) return "two thousand";
  if (year < 10) return "two thousand " + numberToWords(year);
  return "twenty " + numberToWords(year);
}

String timeToString(String timeStr) {
  // Input format: "HH:MM:00"
  int colonPos = timeStr.indexOf(':');
  int hour = timeStr.substring(0, colonPos).toInt();
  int minute = timeStr.substring(colonPos + 1, colonPos + 3).toInt();
  
  String period = (hour < 12) ? "AM" : "PM";
  if (hour > 12) hour -= 12;
  if (hour == 0) hour = 12;
  
  String hourStr = numberToWords(hour);
  String minuteStr;
  
  if (minute == 0) {
    minuteStr = "";
  } else if (minute < 10) {
    minuteStr = " oh " + numberToWords(minute);
  } else {
    minuteStr = " " + numberToWords(minute);
  }
  
  return hourStr + minuteStr + " " + period;
}

String formatDateForSpeech(String dateTimeStr) {
  // Expected format: "YY/MM/DD,HH:MM:00"
  int commaPos = dateTimeStr.indexOf(',');
  String datePart = dateTimeStr.substring(0, commaPos);
  // String timePart = commaPos != -1 ? dateTimeStr.substring(commaPos + 1) : "00:00:00";
  
  // Parse date (YY/MM/DD)
  int firstSlash = datePart.indexOf('/');
  int secondSlash = datePart.indexOf('/', firstSlash + 1);
  // String year = datePart.substring(0, firstSlash);
  int month = datePart.substring(firstSlash + 1, secondSlash).toInt();
  int day = datePart.substring(secondSlash + 1).toInt();
  
  // return monthToString(month) + " " + numberToWords(day) + " " + yearToString(year) + " at " + timeToString(timePart);
  return monthToString(month) + " " + numberToWords(day);
}

String convertNumbersInMessage(String text) {
  String result = "";
  String currentNumber = "";
  
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    
    if (isdigit(c)) {
      currentNumber += c;
    } else {
      if (currentNumber.length() > 0) {
        result += numberToWords(currentNumber.toInt());
        currentNumber = "";  // Reset the number buffer
      }
      result += c;  // Always add the non-digit character
    }
  }
  
  // Add any remaining number at the end
  if (currentNumber.length() > 0) {
    result += numberToWords(currentNumber.toInt());
  }
  
  return result; 
}

String convertSpecialCharsInMessage(String text) {
  String result = "";
  
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    
    // Map special characters to their spoken names
    switch(c) {
      case ',': result += "comma "; break;
      case '.': result += "period "; break;
      case '?': result += "question mark "; break;
      case '!': result += "exclamation mark "; break;
      case ';': result += "semicolon "; break;
      case ':': result += "colon "; break;
      case '-': result += "hyphen "; break;
      case '\'': result += "apostrophe "; break;
      case '"': result += "quotation mark "; break;
      case '@': result += "at symbol "; break;
      case '#': result += "hash "; break;
      case '$': result += "dollar sign "; break;
      case '%': result += "percent "; break;
      case '&': result += "ampersand "; break;
      case '*': result += "asterisk "; break;
      case '+': result += "plus "; break;
      case '=': result += "equals "; break;
      case '/': result += "forward slash "; break;
      case '\\': result += "backslash "; break;
      case '(': result += "open parenthesis "; break;
      case ')': result += "close parenthesis "; break;
      case '[': result += "open bracket "; break;
      case ']': result += "close bracket "; break;
      case '{': result += "open brace "; break;
      case '}': result += "close brace "; break;
      case '<': result += "less than "; break;
      case '>': result += "greater than "; break;
      default: result += c;  // For regular letters and numbers
    }
  }
  
  return result; 
}

//build a list like "1,2,3" (spoken by tts) from learningInput (which stores 0..5)
String pressedButtonsForSpeech(const String& src) {
  bool seen[6] = { false, false, false, false, false, false };
  for (unsigned i = 0; i < src.length(); i++) {
    int idx = src.charAt(i) - '0';
    if (idx >= 0 && idx < 6) seen[idx] = true;
  }
  String out;
  for (int i = 0; i < 6; i++) {
    if (seen[i]) {
      if (out.length() > 0) out += ", ";
      out += String(i + 1); //say 1..6
    }
  }
  return out;
}

//>>>>>>>>part of sleep<<<<<<<<<<<<<<<
//sleep helpers w/ TTS lines
void enterScreenSleep() {
  if (screenSleeping) return;
  speak("Sleep mode");
  u8g2.setPowerSave(1);
  screenSleeping = true;
}

void exitScreenSleep() {
  if (!screenSleeping) return;
  u8g2.setPowerSave(0);
  screenSleeping = false;
  speak("Waking up");       
  drawScreen();             //redraw exactly where device left off
}

void markActivity() {
  lastActivityMs = millis(); //refresh idle timer
}

bool anyInputActive() {
  // Mode button via mcp
  if (mcp.digitalRead(BUTTON_MODE_PIN) == LOW) return true;
  // Aux buttons A..D on mcp
  for (int i = 1; i <= 4; i++) if (mcp.digitalRead(i) == LOW) return true;
  // Braille buttons
  for (int i = 0; i < 6; i++) if (digitalRead(braillePins[i]) == LOW) return true;
  return false;
}

//helper and  TTS on sleep
void checkIdle() {
  if (!screenSleeping && millis() - lastActivityMs >= IDLE_TIMEOUT_MS) {
    enterScreenSleep();            //"Sleep mode" + OLED off
  }
}
//>>>>>>until here (sleep part)<<<<<<<<<<<<<


//====== SETUP =======
void setup() {
  SerialMon.begin(115200);
  Wire.begin(21, 22);
  mcp.begin();
  mcp.pinMode(BUTTON_MODE_PIN, INPUT);
  mcp.pullUp(BUTTON_MODE_PIN, HIGH);
  mcp.pinMode(SLEEP_PIN, INPUT);
  mcp.pullUp(SLEEP_PIN, HIGH);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);

  u8g2.setPowerSave(0); //on at boot
  lastActivityMs = millis();

  //Setup MCP23017 aux button pins
  for (int i = 1; i <= 4; i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }

  //Setup braille input pins
  for (int i = 0; i < 6; i++) {
    pinMode(braillePins[i], INPUT_PULLUP);
  }

  initModem();

  // Read initial battery level
  updateBatteryLevel();
  lastBatteryCheck = millis();

  // Debug to see which method we're using
  SerialMon.print("Battery monitoring initialized using ");
  SerialMon.println(usingModemBattery ? "modem" : "analog");

  SerialMon.println("SIM loading...");
  int retries = 0;
  while (modem.getSimStatus() != SIM_READY && retries < 50) {
    delay(100);
    retries++;
  }
  if (modem.getSimStatus() != SIM_READY) {
    SerialMon.println("SIM not ready after waiting!");
    showTemporaryMessage("SIM not ready!", 1200);
  } else {
    SerialMon.println("SIM ready...");
    updateInbox();
    showTemporaryMessage("Loading Contacts...", 1000);
    speak("inbox mode");
    waitForPhonebookReady();
    updateContacts();
  }

  learningInput = "";
  learningResult = "";
  lastLearningInputTime = 0;

  drawScreen();
}

// ============================ L O O P =================================

void loop() {
  pollTTS(); //added for tts interrupt
  checkBatteryLevel();

  //while sleeping, ignore EVERYTHING except SLEEP button (pin 0)
  if (screenSleeping) {
    if (mcp.digitalRead(SLEEP_PIN) == LOW) {
      //debounce + wake
      exitScreenSleep();                       //"Waking up" + OLED on
      while (mcp.digitalRead(SLEEP_PIN) == LOW) delay(10);
      lastActivityMs = millis();               //reset idle timer
    }
    delay(50);
    return; //stay here until sleep button is pressed
  }

  //manual sleep using sleep button
  if (mcp.digitalRead(SLEEP_PIN) == LOW) {
    enterScreenSleep();                        
    while (mcp.digitalRead(SLEEP_PIN) == LOW) delay(10); //debounce/wait release
    delay(50);
    return;                                    //next loop iteration will be in sleep branch
  }

  //Normal awake behavior
  if (anyInputActive()) markActivity();  //will not wake up when other buttons are pressed
  checkIdle();                           //auto-sleep after timeout

  if (mainMode == MESSAGING_MODE) {
    handleBrailleInputForMessage();
  } else if (mainMode == CONTACTS_MODE && contactsState == ADD_NUMBER) {
    handleBrailleInputForAddNumber();
  } else if (mainMode == CONTACTS_MODE && contactsState == ADD_NAME) {
    handleBrailleInputForAddName();
  } else if (mainMode == LEARNING_MODE) {
    handleLearningModeInput();
    resetLearningAfterDelay();
  }

  handleButtons();
  drawScreen();
  delay(100);
}

/*void speak(String text) { //k
  if (text.length() > 0) {
    while (SerialAT.available()) SerialAT.read(); // Clear the buffer
    if (!modem.textToSpeech(text, 2)) {
      Serial.println("TTS failed, reinitializing modem...");
      initModem(); // Reinitialize modem on failure
      delay(1000);
      modem.textToSpeech(text, 2); // Retry
    }
    lastSpeakTime = millis();
  }
}
*/

void speak(String text) { //p
  if (text.length() == 0) return;

  while (SerialAT.available()) SerialAT.read();   // clear any old URCs
  modem.textToSpeechStop();                       // best-effort cancel current speech
  delay(10);                                      // tiny gap to avoid merging commands

  // Non-blocking start
  String tmp = text;                              // LilyGO’s TTS uses non-const ref
  if (!modem.textToSpeechAsync(tmp, 2)) {
    Serial.println("TTS failed, reinitializing modem...");
    initModem();
    delay(200);
    modem.textToSpeechAsync(tmp, 2);
  }
  isSpeaking = true;
  lastSpeakTime = millis();
}


void speakCharacter(char c) {
  String toSpeak;
  
  // Mapping ng special characters para mabasa ni TTS
  switch(c) {
    case ',': toSpeak = "comma"; break;
    case '.': toSpeak = "period"; break;
    case '?': toSpeak = "question mark"; break;
    case '!': toSpeak = "exclamation mark"; break;
    case ';': toSpeak = "semicolon"; break;
    case ':': toSpeak = "colon"; break;
    case '-': toSpeak = "hyphen"; break;
    case '\'': toSpeak = "apostrophe"; break;
    case '"': toSpeak = "quotation mark"; break;
    case '@': toSpeak = "at symbol"; break;
    case '#': toSpeak = "hash"; break; // hash or hash tag?
    case '$': toSpeak = "dollar sign"; break;
    case '%': toSpeak = "percent"; break;
    case '&': toSpeak = "ampersand"; break;
    case '*': toSpeak = "asterisk"; break;
    case '+': toSpeak = "plus"; break;
    case '=': toSpeak = "equals"; break;
    case '/': toSpeak = "forward slash"; break;
    case '\\': toSpeak = "backslash"; break;
    case '(': toSpeak = "open parenthesis"; break;
    case ')': toSpeak = "close parenthesis"; break;
    case '[': toSpeak = "open bracket"; break;
    case ']': toSpeak = "close bracket"; break;
    case '{': toSpeak = "open brace"; break;
    case '}': toSpeak = "close brace"; break;
    case '<': toSpeak = "less than"; break;
    case '>': toSpeak = "greater than"; break;
    default: toSpeak = String(c); // For letters and numbers
  }
  
  /* //k
  modem.textToSpeech(toSpeak, 2);
  lastSpeakTime = millis();
  */
  
  //p
String tmp = toSpeak;               // make a copy, because TinyGSM expects a non-const reference
modem.textToSpeechStop();           // stop any current speech (important if user presses fast)
delay(10);                          // give the modem a tiny gap to process stop
modem.textToSpeechAsync(tmp, 2);    // start new speech, but DO NOT wait for it to finish
isSpeaking = true;                  // mark that we’re currently speaking
lastSpeakTime = millis();

}

void speakContext(String context) {
  if (millis() - lastSpeakTime > speakInterval) {
    speak(context);
  }
}

//====== BUTTONS =======

void handleButtons() {
  static unsigned long lastModePress = 0;
  static int lastMessageIndex = -1;
  static int lastContactIndex = -1;

  // Mode switching (Inbox <-> Contacts)
  if (mcp.digitalRead(BUTTON_MODE_PIN) == LOW && millis() - lastModePress > 300) {
    if (mainMode == INBOX_MODE) {
      mainMode = CONTACTS_MODE;
      contactsState = CONTACTS_LIST;
      currentContactIndex = 0;
      speak("Contacts Mode");
      delay(300); 
      if (totalContacts > 0) {
        String name = contacts[currentContactIndex].name;
        name.toLowerCase();
        speakLater(name);
      }
    } else if (mainMode == CONTACTS_MODE) {
      mainMode = LEARNING_MODE;
      learningState = LEARNING_IDLE;
      learningInput = "";
      learningResult = "";
      speak("Learning Mode. Press any braille button to start.");
      delay(300);
    } else {
      mainMode = INBOX_MODE;
      inboxState = INBOX_LIST;
      speak("Inbox Mode");
      delay(300); 
      if (totalMessages > 0) {
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        sender.toLowerCase();
        speakLater("Message from " + sender);
      }
    }
    lastModePress = millis();
    while (mcp.digitalRead(BUTTON_MODE_PIN) == LOW);
  }

  ///////////// Start of INBOX /////////////
  if (mainMode == INBOX_MODE) {
    if (inboxState == INBOX_LIST) {
      if (mcp.digitalRead(AUX_A_PIN) == LOW && totalMessages > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        inboxState = MESSAGE_SELECTED;
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        String dateTime = formatDateForSpeech(messages[currentMessageIndex].date);
        String originalMsg = messages[currentMessageIndex].message;
        String spokenMsg = convertNumbersInMessage(originalMsg);
        sender.toLowerCase();
        speak("Selected Message from " + sender + ". Sent on " + dateTime + ". Message is: " + spokenMsg);
        while (mcp.digitalRead(AUX_A_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_B_PIN) == LOW && totalMessages > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        currentMessageIndex = (currentMessageIndex - 1 + totalMessages) % totalMessages;
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        String dateTime = formatDateForSpeech(messages[currentMessageIndex].date);
        sender.toLowerCase();
        speak("Message from " + sender + ". Sent on " + dateTime);
        while (mcp.digitalRead(AUX_B_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_C_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        updateInbox();
        speak("Inbox refreshed");
        while (mcp.digitalRead(AUX_C_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_D_PIN) == LOW && totalMessages > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        currentMessageIndex = (currentMessageIndex + 1) % totalMessages;
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        String dateTime = formatDateForSpeech(messages[currentMessageIndex].date);
        sender.toLowerCase();
        speak("Message from " + sender + ". Sent on " + dateTime);
        while (mcp.digitalRead(AUX_D_PIN) == LOW);
      }
    }
    
    else if (inboxState == MESSAGE_SELECTED) {
      if (mcp.digitalRead(AUX_A_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        replyNumber = messages[currentMessageIndex].sender;
        messageText = "";
        mainMode = MESSAGING_MODE;
        messagingState = MESSAGE_COMPOSE;
        inputMode = LETTER_MODE; // para mag-reset back to letter mode
        speak("Replying to message. Directing to messaging mode.");
        while (mcp.digitalRead(AUX_A_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_B_PIN) == LOW) {
        inboxState = INBOX_LIST;
        speak("Back to Inbox Mode");
        while (mcp.digitalRead(AUX_B_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_C_PIN) == LOW) {
        deleteMessageAtIndex(currentMessageIndex);
        inboxState = INBOX_LIST;
        speak("Back to Inbox Mode");
        while (mcp.digitalRead(AUX_C_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_D_PIN) == LOW) {
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        String dateTime = formatDateForSpeech(messages[currentMessageIndex].date);
        String originalMsg = messages[currentMessageIndex].message;
        String spokenMsg = convertNumbersInMessage(originalMsg);
        sender.toLowerCase();
        speak("From " + sender + ". Sent on " + dateTime + ". Message is: " + spokenMsg);
        while (mcp.digitalRead(AUX_D_PIN) == LOW);
      }
    }
  }
  ///////////// End of INBOX /////////////

  ////////// Start of CONTACTS ///////////
  else if (mainMode == CONTACTS_MODE) {
    if (contactsState == CONTACTS_LIST) {
      if (mcp.digitalRead(AUX_A_PIN) == LOW && totalContacts > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        contactsState = CONTACT_SELECTED;
        String name = contacts[currentContactIndex].name;
        String number = contacts[currentContactIndex].number;
        name.toLowerCase();
        speak("Selected Contact is: " + name + ". Number: " + number);
        while (mcp.digitalRead(AUX_A_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_B_PIN) == LOW && totalContacts > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        currentContactIndex = (currentContactIndex - 1 + totalContacts) % totalContacts;       
        String name = contacts[currentContactIndex].name;
        //String number = contacts[currentContactIndex].number;
        name.toLowerCase();
        speak(name);
        while (mcp.digitalRead(AUX_B_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_C_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        addContactNumber = "";
        addContactName = "";
        contactsState = ADD_NUMBER;
        //speak("Enter phone number starting with 09. Press C for backspace.");
        speak("Enter 9 more digits. The first two digits, which are zero and nine, are automatically added.");
        while (mcp.digitalRead(AUX_C_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_D_PIN) == LOW && totalContacts > 0) {
        modem.textToSpeechStop();                     //tts interrupt
        currentContactIndex = (currentContactIndex + 1) % totalContacts;
        String name = contacts[currentContactIndex].name;
        //String number = contacts[currentContactIndex].number;
        name.toLowerCase();
        speak(name);
        while (mcp.digitalRead(AUX_D_PIN) == LOW);
      }
    }
    
  else if (contactsState == CONTACT_SELECTED) {
    String contactName = contacts[currentContactIndex].name;
    String contactNumber = contacts[currentContactIndex].number;
    contactName.toLowerCase();

    if (mcp.digitalRead(AUX_A_PIN) == LOW) {
      modem.textToSpeechStop();                     //tts interrupt
      replyNumber = contacts[currentContactIndex].number;
      messageText = "";
      mainMode = MESSAGING_MODE;
      inputMode = LETTER_MODE; // para mag-reset back to letter mode
      speak("Composing message to " + contactName);
      while (mcp.digitalRead(AUX_A_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_B_PIN) == LOW) {
      modem.textToSpeechStop();                     //tts interrupt
      contactsState = CONTACTS_LIST;
      speak("Back to contacts list");
      while (mcp.digitalRead(AUX_B_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_C_PIN) == LOW) {
      modem.textToSpeechStop();                     //tts interrupt
      deleteContactAtIndex(currentContactIndex);
      contactsState = CONTACTS_LIST;
      speak(contactName + " deleted. Back to contacts list.");
      while (mcp.digitalRead(AUX_C_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_D_PIN) == LOW) {
      modem.textToSpeechStop();                     //tts interrupt
      speak("Selected contact: " + contactName + ". Number: " + contactNumber);
      while (mcp.digitalRead(AUX_D_PIN) == LOW);
    }
  }
    
    else if (contactsState == ADD_NUMBER) {
      static bool wasNumberComplete = false;
      if (addContactNumber.length() == 11 && addContactNumber.startsWith("09") && !wasNumberComplete) {
          speak("Number complete. Press A to compose, B to cancel, or D to save.");
          wasNumberComplete = true;
      } else if (addContactNumber.length() < 11) {
          wasNumberComplete = false;
      }

      if (mcp.digitalRead(AUX_A_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        if (addContactNumber.length() == 11 && addContactNumber.startsWith("09")) {
          if (confirmationState == NO_CONFIRMATION) {
            modem.textToSpeechStop();                     //tts interrupt
            speak("Number is " + addContactNumber + ". Press A again to compose message.");
            confirmationState = CONFIRM_NUMBER;
          } else if (confirmationState == CONFIRM_NUMBER) {
            modem.textToSpeechStop();                     //tts interrupt
            replyNumber = addContactNumber;
            messageText = "";
            mainMode = MESSAGING_MODE;
            messagingState = MESSAGE_COMPOSE;
            inputMode = LETTER_MODE; // para mag-reset back to letter mode
            confirmationState = NO_CONFIRMATION;
            speak("Composing message to new number.");
          }
        } else {
          modem.textToSpeechStop();                     //tts interrupt
          //speak("Number incomplete. Must be 11 digits starting with 09.");
          speak("Number incomplete. Need " + numberToWords(11 - addContactNumber.length()) + " more digits.");
        }
        while (mcp.digitalRead(AUX_A_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_B_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        contactsState = CONTACTS_LIST;
        addContactNumber = "";
        confirmationState = NO_CONFIRMATION;
        speak("Cancelled adding new contact.");
        while (mcp.digitalRead(AUX_B_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_C_PIN) == LOW /*&& addContactNumber.length() > 0*/) {
        modem.textToSpeechStop();                     //tts interrupt
        /*addContactNumber.remove(addContactNumber.length() - 1);
        confirmationState = NO_CONFIRMATION;
        speak("Last digit deleted.");*/
        if (addContactNumber.length() > 2) {
          modem.textToSpeechStop();                     //tts interrupt
            addContactNumber.remove(addContactNumber.length() - 1);
            confirmationState = NO_CONFIRMATION;
            speak("Last digit deleted.");
        } else {
          modem.textToSpeechStop();                     //tts interrupt
            speak("Cannot delete zero nine prefix.");
        }
        while (mcp.digitalRead(AUX_C_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_D_PIN) == LOW) {
        if (addContactNumber.length() == 11 /*&& addContactNumber.startsWith("09")*/) {
          if (confirmationState == NO_CONFIRMATION) {
            modem.textToSpeechStop();                     //tts interrupt
            speak("Number is " + addContactNumber + ". Press D again to save.");
            confirmationState = CONFIRM_NUMBER;
          } else if (confirmationState == CONFIRM_NUMBER) {
            modem.textToSpeechStop();                     //tts interrupt
            contactsState = ADD_NAME;
            confirmationState = NO_CONFIRMATION;
            speak("Enter contact name. Maximum seven characters. Press A for space or C for backspace.");
          }
        } else {
          modem.textToSpeechStop();                     //tts interrupt
            //speak("Number incomplete. Must be 11 digits starting with 09.");
            speak("Number incomplete. Need " + String(11 - addContactNumber.length()) + " more digits.");
        }
        while (mcp.digitalRead(AUX_D_PIN) == LOW);
      }
    }
    
    else if (contactsState == ADD_NAME) {
      if (mcp.digitalRead(AUX_A_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        if (addContactName.length() < 7) {
          modem.textToSpeechStop();                     //tts interrupt
          addContactName += " ";
          confirmationState = NO_CONFIRMATION;
          speak("Space added.");
        } else {
          modem.textToSpeechStop();                     //tts interrupt
          speak("Maximum 7 characters reached. Press D to save or B to cancel.");
        }
        while (mcp.digitalRead(AUX_A_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_B_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        contactsState = CONTACTS_LIST;
        addContactName = "";
        addContactNumber = "";
        confirmationState = NO_CONFIRMATION;
        speak("Cancelled adding new contact.");
        while (mcp.digitalRead(AUX_B_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_C_PIN) == LOW) {
        modem.textToSpeechStop();                     //tts interrupt
        if (addContactName.length() > 0) {
          addContactName.remove(addContactName.length() - 1);
          confirmationState = NO_CONFIRMATION;
          speak("Last character deleted.");
        } else {
          modem.textToSpeechStop();                     //tts interrupt
          speak("No characters to delete.");
        }
        while (mcp.digitalRead(AUX_C_PIN) == LOW);
      }
      if (mcp.digitalRead(AUX_D_PIN) == LOW) {
        if (addContactName.length() > 0) {
          if (confirmationState == NO_CONFIRMATION) {
            modem.textToSpeechStop();                     //tts interrupt
            String nameToSpeak = addContactName;
            nameToSpeak.toLowerCase();
            speak("Name is " + nameToSpeak + ". Press D again to save.");
            confirmationState = CONFIRM_NAME;
          } else if (confirmationState == CONFIRM_NAME) {
            modem.textToSpeechStop();                     //tts interrupt
            saveContactToSIM(addContactName, addContactNumber);
            contactsState = CONTACTS_LIST;
            addContactName = "";
            addContactNumber = "";
            confirmationState = NO_CONFIRMATION;
            updateContacts();
            speak("Contact saved successfully.");
          }
        } else {
          modem.textToSpeechStop();                     //tts interrupt
            speak("No name entered. Please enter a name before saving.");
        }
        while (mcp.digitalRead(AUX_D_PIN) == LOW);
      }
    }
  }
  ///////////// End of CONTACTS /////////////

  /////////// Start of MESSAGING ////////////
  else if (mainMode == MESSAGING_MODE) {
    if (mcp.digitalRead(AUX_A_PIN) == LOW) {
      modem.textToSpeechStop();                     //tts interrupt
      messageText += " ";
      confirmationState = NO_CONFIRMATION;
      speak("Space added.");
      while (mcp.digitalRead(AUX_A_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_B_PIN) == LOW) {
      if (inputMode == LETTER_MODE) {
        modem.textToSpeechStop();                     //tts interrupt
        inputMode = NUMBER_MODE;
        speak("Switched to number mode");
      } else if (inputMode == NUMBER_MODE) {
        modem.textToSpeechStop();                     //tts interrupt
        inputMode = SPECIAL_CHAR_MODE;
        speak("Switched to special characters mode");
      } else {
        modem.textToSpeechStop();                     //tts interrupt
        inputMode = LETTER_MODE;
        speak("Switched to letter mode");
      }
      confirmationState = NO_CONFIRMATION;
      while (mcp.digitalRead(AUX_B_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_C_PIN) == LOW && messageText.length() > 0) {
      if (messageText.length() > 0) {
        modem.textToSpeechStop();                     //tts interrupt
          messageText.remove(messageText.length() - 1);
          confirmationState = NO_CONFIRMATION;
          speak("Last character deleted.");
      } else {
        modem.textToSpeechStop();                     //tts interrupt
          speak("No text to delete.");
      }
      while (mcp.digitalRead(AUX_C_PIN) == LOW);
    }
    if (mcp.digitalRead(AUX_D_PIN) == LOW && messageText.length() > 0 && replyNumber.length() > 0) {
      if (confirmationState == NO_CONFIRMATION) {
        modem.textToSpeechStop();                     //tts interrupt
        String numbersConverted = convertNumbersInMessage(messageText);
        String fullConverted = convertSpecialCharsInMessage(numbersConverted);
        String messageToSpeak = fullConverted;
        messageToSpeak.toLowerCase();
        speak("Message is: " + messageToSpeak + ". Press D again to send.");
        confirmationState = CONFIRM_MESSAGE;
      } else if (confirmationState == CONFIRM_MESSAGE) {
        modem.textToSpeechStop();                     //tts interrupt
        sendMessageSMS(replyNumber, messageText);
        messageText = "";
        mainMode = INBOX_MODE;
        inboxState = INBOX_LIST;
        confirmationState = NO_CONFIRMATION;
        speak("Message sent successfully. Returning to Inbox Mode.");
      }
      while (mcp.digitalRead(AUX_D_PIN) == LOW);
    }
  }
  ///////////// End of MESSAGING /////////////
}

//========== BRAILLE INPUTS ===========

void handleBrailleInputForMessage() {
  String pattern = "";
  for (int i = 0; i < 6; i++) {
    pattern += (digitalRead(braillePins[i]) == LOW ? '1' : '0');
  }
  char c = brailleToChar(pattern, inputMode);
  if (c != '?') {
    messageText += c;
    speakCharacter(c);
    delay(300);
  }
}

void handleBrailleInputForAddNumber() {
  String pattern = "";
  for (int i = 0; i < 6; i++) {
    pattern += (digitalRead(braillePins[i]) == LOW ? '1' : '0');
  }
  char c = brailleToChar(pattern, NUMBER_MODE);
  
  if (c >= '0' && c <= '9') {
    while(SerialAT.available()) SerialAT.read();
    
    //Meron ng 09
    if (addContactNumber.length() == 0) {
      addContactNumber = "09";
      speak("Zero nine"); // Announce the automatic "09"
      delay(300); // Small pause before continuing
    }
    
    if (addContactNumber.length() < 11) {
      addContactNumber += c;
      speakCharacter(c);
    } else {
      speak("Maximum digits reached");
    }
    delay(500);

    // Waley pang 09
    /*if (addContactNumber.length() == 0) {
      if (c == '0') {
        addContactNumber += c;
        speakCharacter(c);
      } else {
        speak("Invalid number. First digit must be zero.");
        return;
      }
    } 
    else if (addContactNumber.length() == 1) {
      if (c == '9') {
        addContactNumber += c;
        speakCharacter(c);
      } else {
        speak("Invalid number. Second digit must be nine.");
        return;
      }
    }
    else if (addContactNumber.length() >= 2 && addContactNumber.length() < 11) {
      addContactNumber += c;
      speakCharacter(c);
    }
    delay(500);*/
  }
}

void handleBrailleInputForAddName() {
  if (addContactName.length() >= 7) {
    return; // Simply exit if maximum length reached
  }

  String pattern = "";
  for (int i = 0; i < 6; i++) {
    pattern += (digitalRead(braillePins[i]) == LOW ? '1' : '0');
  }
  char c = brailleToChar(pattern, LETTER_MODE);

  while(SerialAT.available()) SerialAT.read();

  if (c != '?') {
    addContactName += c;
    speakCharacter(c);
    delay(500);
    
    if (addContactName.length() >= 7) {
      speak("Maximum 7 characters reached. Press D to save or B to cancel.");
    }
  }
}

char brailleToChar(String pattern, InputMode mode) {
  // Check letter mappings (A-Z)
  if (mode == LETTER_MODE) {
    for (int i = 0; i < brailleAlphabetSize; i++) {
      if (pattern == brailleAlphabet[i].pattern) {
        return brailleAlphabet[i].character;
      }
    }
  }
  // Check number mappings (0-9)
  else if (mode == NUMBER_MODE) {
    for (int i = 0; i < brailleAlphabetSize; i++) {
      if (pattern == brailleAlphabet[i].pattern) {
        if (brailleAlphabet[i].character >= 'A' && brailleAlphabet[i].character <= 'J')
          return "1234567890"[brailleAlphabet[i].character - 'A'];
        else
          return '?';
      }
    }
  }
  // Check special character mappings
  else if (mode == SPECIAL_CHAR_MODE) {
    for (int i = 0; i < brailleSpecialCharsSize; i++) {
      if (pattern == brailleSpecialChars[i].pattern) {
        return brailleSpecialChars[i].character;
      }
    }
  }
  return '?';
}

String getContactNameByNumber(String number) {
  //Normalize both to "09..." for comparison
  String numNorm = number;
  if (numNorm.startsWith("+639"))
    numNorm = "09" + numNorm.substring(4);

  for (int i = 0; i < totalContacts; i++) {
    String contactNum = contacts[i].number;
    if (contactNum.startsWith("+639"))
      contactNum = "09" + contactNum.substring(4);

    if (contactNum == numNorm)
      return contacts[i].name;
  }
  return number;  //if not found, return the number
}

//======= DISPLAY =======

void drawScreen() {
  u8g2.clearBuffer();

  drawBatteryIndicator();

  String narration = "";

  if (mainMode == INBOX_MODE) {
    if (inboxState == INBOX_LIST) {
      u8g2.setCursor(0, 10);
      u8g2.print("INBOX (");
      u8g2.print(currentMessageIndex + 1);
      u8g2.print("/");
      u8g2.print(totalMessages);
      u8g2.print(")");

      if (totalMessages > 0) {
        String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
        String date = messages[currentMessageIndex].date;
        String msg = messages[currentMessageIndex].message;

        u8g2.setCursor(0, 25);
        u8g2.print(sender);
        u8g2.setCursor(0, 40);
        u8g2.print(date);
        u8g2.setCursor(0, 55);
        u8g2.print(msg.length() > 20 ? msg.substring(0, 20) + "..." : msg);
      } else {
        u8g2.setCursor(0, 30);
        u8g2.print("No messages.");
      }
    } else if (inboxState == MESSAGE_SELECTED) {
      String sender = getContactNameByNumber(messages[currentMessageIndex].sender);
      String date = messages[currentMessageIndex].date;
      String msg = messages[currentMessageIndex].message;

      u8g2.setCursor(0, 10);
      u8g2.print("From: ");
      u8g2.print(sender);

      u8g2.setCursor(0, 25);
      u8g2.print(date);

      u8g2.setCursor(0, 40);
      u8g2.print(msg.length() > 20 ? msg.substring(0, 20) : msg);

      if (msg.length() > 20) {
        u8g2.setCursor(0, 55);
        u8g2.print(msg.substring(20, min((size_t)40, msg.length())));
      }
    }
  } else if (mainMode == CONTACTS_MODE) {
    if (contactsState == CONTACTS_LIST) {
      u8g2.setCursor(0, 10);
      u8g2.print("CONTACTS (");

      if (totalContacts == 0) {
        u8g2.print("0/0)");
        u8g2.setCursor(0, 30);
        u8g2.print("No contacts");
      } else {
        u8g2.print(currentContactIndex + 1);
        u8g2.print("/");
        u8g2.print(totalContacts);
        u8g2.print(")");

        u8g2.setCursor(0, 25);
        u8g2.print(contacts[currentContactIndex].name);
        u8g2.setCursor(0, 40);
        u8g2.print(contacts[currentContactIndex].number);
      }
    } else if (contactsState == CONTACT_SELECTED) {
      u8g2.setCursor(0, 10);
      u8g2.print("Selected:");
      u8g2.setCursor(0, 25);
      u8g2.print(contacts[currentContactIndex].name);
      u8g2.setCursor(0, 40);
      u8g2.print(contacts[currentContactIndex].number);
    } else if (contactsState == ADD_NUMBER) {
      u8g2.setCursor(0, 20);
      /*u8g2.print("Number: ");
      u8g2.print(addContactNumber);*/
      u8g2.print("Number: 09");
      u8g2.print(addContactNumber.length() > 2 ? addContactNumber.substring(2) : ""); 
      u8g2.setCursor(0, 40);
      u8g2.setFont(u8g2_font_5x8_tf);
      u8g2.print("[A] Compose  [C] <");
      u8g2.setCursor(0, 50);
      u8g2.print("[B] Cancel   [D] Save");
      u8g2.setFont(u8g2_font_6x10_tf);
    } else if (contactsState == ADD_NAME) {
      u8g2.setCursor(0, 20);
      u8g2.print("Name: ");
      u8g2.print(addContactName);
      u8g2.setCursor(0, 40);
      u8g2.print("[A] Space    [C] <");
      u8g2.setCursor(0, 50);
      u8g2.print("[B] Cancel   [D] Save");
    }
  } else if (mainMode == MESSAGING_MODE) {
    u8g2.setCursor(0, 10);
    u8g2.print("TO: ");
    u8g2.print(getContactNameByNumber(replyNumber));
    u8g2.setCursor(0, 20);
    u8g2.print(messageText.length() > 18 ? messageText.substring(messageText.length() - 18) : messageText);
    
    String modeText;
    if (inputMode == LETTER_MODE) modeText = "[Letter Mode]";
    else if (inputMode == NUMBER_MODE) modeText = "[Number Mode]";
    else modeText = "[Special Characters Mode]";
    u8g2.setCursor(0, 40);
    u8g2.print(modeText);

    u8g2.setCursor(0, 50);
    u8g2.print("[A] Space    [C] <");
    u8g2.setCursor(0, 60);
    u8g2.print("[B] Mode   [D] Send");
  } else if (mainMode == LEARNING_MODE) {
    u8g2.setCursor(0, 10);
    u8g2.print("LEARNING MODE");
    
    // Draw braille dots
    int startX = 10;
    int startY = 25;
    
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 2; col++) {
        int dotIndex = col * 3 + row; // This maps to standard braille numbering
        int x = startX + col * LEARNING_DOT_SPACING;
        int y = startY + row * LEARNING_DOT_SPACING;
        
        // Check if this dot is filled
        bool filled = (learningInput.indexOf(String(dotIndex)) != -1);
        
        if (filled) {
          u8g2.drawDisc(x, y, LEARNING_DOT_RADIUS);
        } else {
          u8g2.drawCircle(x, y, LEARNING_DOT_RADIUS);
        }
      }
    }

    // Input display at (40, 25)
    u8g2.setCursor(30, 25);
    u8g2.print("Input: ");
    for (int i = 0; i < learningInput.length(); i++) {
      char inputChar = learningInput.charAt(i);
      int buttonNum = (inputChar - '0') + 1; // Convert from 0-5 to 1-6
      u8g2.print(buttonNum);
      if (i < learningInput.length() - 1) u8g2.print(",");
    }
    
    // Result display starting at (40, 35)
    u8g2.setCursor(30, 35);
    if (learningState == LEARNING_RESULT) {
  u8g2.print("Result:");
  
  // Simple display - just show the entire result string
  // Split into multiple lines if needed
  u8g2.setCursor(30, 45);
  
  // Display the first part of the result
  String firstLine = learningResult;
  if (firstLine.length() > 20) {
    firstLine = firstLine.substring(0, 20);
  }
  u8g2.print(firstLine);
  
  // Display second line if needed
  if (learningResult.length() > 20) {
    u8g2.setCursor(30, 55);
    String secondLine = learningResult.substring(20);
    if (secondLine.length() > 20) {
      secondLine = secondLine.substring(0, 20);
    }
    u8g2.print(secondLine);
  }
} else {
  u8g2.setCursor(0, 60);
  u8g2.print("[A]Confirm [C]Cancel");
}
  }

  u8g2.sendBuffer();
}

// =================== END OF DISPLAY FUNCTION ================================

void showTemporaryMessage(String msg, int ms) {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  u8g2.print(msg);
  u8g2.sendBuffer();
}

//======= MODEM/SIM/CONTACTS UTILITIES =========

void initModem() {
#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(1000);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  pinMode(MODEM_RING_PIN, INPUT_PULLUP);
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  Serial.println("Start modem...");
  delay(3000);

  unsigned long startAttempt = millis();
  while (!modem.testAT() && millis() - startAttempt < 10000) {
    delay(100);
  }

  if (!modem.testAT()) {
    Serial.println("Failed to connect to modem.");
    return;
  }

  Serial.println("Wait SMS Done.");
  if (!modem.waitResponse(100000UL, "SMS DONE")) {
    Serial.println("Can't wait for SMS registration.");
    return;
  }
  Serial.println("Modem initialized");
  delay(1000);

  //Initialize TTS parameters
  if (modem.setTTSParameters(2, 3, 1, 1, 1, 1)) {
    Serial.println("TTS parameters set successfully");
  } else {
    Serial.println("Failed to set TTS parameters");
  }

  //new-SMS setup
  modem.sendAT("+CNMI=2,1,0,0,0");
  modem.waitResponse();

  delay(1000); 
}

void updateInbox() {
  totalMessages = 0;
  if (modem.getSimStatus() != SIM_READY) {
    Serial.println("SIM not ready");
    showTemporaryMessage("SIM Error", 900);
    return;
  }

  modem.sendAT("+CMGF=1");
  modem.waitResponse();
  modem.sendAT("+CPMS=\"SM\"");
  modem.waitResponse();

  String response;
  modem.sendAT("+CMGL=\"ALL\"");
  if (modem.waitResponse(10000L, response) != 1) {
    Serial.println("Failed to list messages");
    showTemporaryMessage("Inbox Error", 900);
    return;
  }

  int index = 0, startPos = 0;
  while ((startPos = response.indexOf("+CMGL:", startPos)) != -1 && index < MAX_MESSAGES) {
    int endPos = response.indexOf("\n", startPos);
    String header = response.substring(startPos, endPos);

    int spacePos = header.indexOf(' ');
    int comma1 = header.indexOf(',', spacePos + 1);
    int comma2 = header.indexOf(',', comma1 + 1);
    int comma3 = header.indexOf(',', comma2 + 1);
    int comma4 = header.indexOf(',', comma3 + 1);

    if (spacePos == -1 || comma4 == -1) {
      startPos = endPos + 1;
      continue;
    }

    int cmglIdx = header.substring(spacePos + 1, comma1).toInt();
    int q1 = header.indexOf('"');
    int q2 = header.indexOf('"', q1 + 1);
    int q3 = header.indexOf('"', q2 + 1);
    int q4 = header.indexOf('"', q3 + 1);

    String sender = "";
    if (q3 != -1 && q4 != -1)
      sender = header.substring(q3 + 1, q4);

    Serial.print("CMGL SENDER FIELD: ");
    Serial.println(sender);

    String date = header.substring(comma4 + 2, header.length() - 1);

    startPos = endPos + 1;
    endPos = response.indexOf("\n+CMGL:", startPos);
    if (endPos == -1) {
      endPos = response.indexOf("\nOK", startPos);
      if (endPos == -1)
        endPos = response.length();
    }
    String msg = response.substring(startPos, endPos);
    msg.trim();

    if (msg.startsWith("+CGEEV:") || msg.startsWith("+CMTI:") || msg.length() == 0) {
      startPos = endPos;
      continue;
    }

    messages[index].cmglIndex = cmglIdx;
    messages[index].sender = sender;
    messages[index].date = date;
    messages[index].message = msg;
    index++;
    startPos = endPos;
  }
  totalMessages = index;

  //=== SORT by date DESCENDING (newest first) ===
  for (int i = 0; i < totalMessages - 1; i++) {
    for (int j = i + 1; j < totalMessages; j++) {
      if (messages[i].date < messages[j].date) {
        SmsEntry temp = messages[i];
        messages[i] = messages[j];
        messages[j] = temp;
      }
    }
  }

  currentMessageIndex = 0;
}

void deleteMessageAtIndex(int idx) {
  if (totalMessages == 0)
    return;

  int simIndex = messages[idx].cmglIndex;
  modem.sendAT("+CMGD=" + String(simIndex));

  if (modem.waitResponse() == 1) {
    for (int i = idx; i < totalMessages - 1; i++) {
      messages[i] = messages[i + 1];
    }
    totalMessages--;

    if (currentMessageIndex >= totalMessages && totalMessages > 0) {
      modem.textToSpeechStop();                     //tts interrupt
      currentMessageIndex = totalMessages - 1;
    }

    speak("Message Deleted.");
    showTemporaryMessage("Message Deleted", 900);
  } else {
    speak("Delete Failed.");
    showTemporaryMessage("Delete Failed", 900);
  }
}

void updateContacts() {
  totalContacts = 0;
  for (int i = 0; i < MAX_CONTACTS; i++) {
    contacts[i].name = "";
    contacts[i].number = "";
  }
  modem.sendAT("+CPBS=\"SM\"");
  modem.waitResponse();
  modem.sendAT("+CPBR=1,250");

  String all = "";
  unsigned long start = millis();
  while (millis() - start < 2000) {
    while (modem.stream.available())
      all += modem.stream.readStringUntil('\n') + "\n";
    if (all.indexOf("OK") != -1)
      break;
  }

  Serial.println("CPBR Response:\n" + all);  //DEBUGGING

  int pos = 0;
  while ((pos = all.indexOf("+CPBR:", pos)) != -1 && totalContacts < MAX_CONTACTS) {
    int end = all.indexOf('\n', pos);
    if (end == -1)
      break;
    String entry = all.substring(pos, end);

    //Find the number and name using the positions of the quotation marks
    int q1 = entry.indexOf('"');
    int q2 = entry.indexOf('"', q1 + 1);
    int q3 = entry.indexOf('"', q2 + 1);
    int q4 = entry.indexOf('"', q3 + 1);

    //if not enough quotes, skip this entry
    if (q1 == -1 || q2 == -1 || q3 == -1 || q4 == -1) {
      pos = end + 1;
      continue;
    }

    String number = entry.substring(q1 + 1, q2);
    String name = entry.substring(q3 + 1, q4);

    //only store valid numbers (must be at least 11, starts with +639 or 09)
    if (number.length() < 11) {
      pos = end + 1;
      continue;
    }
    if (number.startsWith("+639"))
      number = "09" + number.substring(4);
    if (name.length() > 7)
      name = name.substring(0, 7);

    contacts[totalContacts].name = name;
    contacts[totalContacts].number = number;
    totalContacts++;
    pos = end + 1;
  }
  sortContactsByName();

  //Reset currentContactIndex if out of bounds
  if (totalContacts == 0)
    currentContactIndex = 0;
  else if (currentContactIndex >= totalContacts)
    currentContactIndex = totalContacts - 1;

  Serial.print("Contacts loaded: ");
  Serial.println(totalContacts);  //DEBUGGING
}

void sortContactsByName() {
  for (int i = 0; i < totalContacts - 1; i++) {
    for (int j = 0; j < totalContacts - i - 1; j++) {
      String nameA = contacts[j].name;
      String nameB = contacts[j + 1].name;
      nameA.toLowerCase();
      nameB.toLowerCase();
      if (nameA > nameB) {
        Contact temp = contacts[j];
        contacts[j] = contacts[j + 1];
        contacts[j + 1] = temp;
      }
    }
  }
}

void deleteContactAtIndex(int idx) {
  if (idx < 0 || idx >= totalContacts)
    return;
  String number = contacts[idx].number;
  if (number.startsWith("09"))
    number = "+639" + number.substring(2);

  int simIndex = -1;
  modem.sendAT("+CPBR=1,250");
  String all = "";
  unsigned long start = millis();
  while (millis() - start < 2000) {
    while (modem.stream.available())
      all += modem.stream.readStringUntil('\n') + "\n";
    if (all.indexOf("OK") != -1)
      break;
  }
  int pos = 0;
  while ((pos = all.indexOf("+CPBR:", pos)) != -1) {
    int end = all.indexOf('\n', pos);
    if (end == -1)
      break;
    String entry = all.substring(pos, end);
    pos = end + 1;
    int colon = entry.indexOf(':');
    int comma = entry.indexOf(',', colon);
    int idxSIM = entry.substring(colon + 1, comma).toInt();
    int q1 = entry.indexOf('"'), q2 = entry.indexOf('"', q1 + 1);
    if (q1 > 0 && q2 > q1) {
      String foundNumber = entry.substring(q1 + 1, q2);
      if (foundNumber == number) {
        simIndex = idxSIM;
        break;
      }
    }
  }
  if (simIndex == -1) {
    showTemporaryMessage("Not found", 900);
    return;
  }
  modem.sendAT("+CPBW=" + String(simIndex));
  if (modem.waitResponse(5000) == 1) {
    for (int i = idx; i < totalContacts - 1; i++)
      contacts[i] = contacts[i + 1];
    totalContacts--;
    currentContactIndex = min(idx, totalContacts - 1);
    showTemporaryMessage("Deleted", 900);
  } else
    showTemporaryMessage("Delete failed", 900);

  updateContacts();
  sortContactsByName();
  currentContactIndex = min(idx, totalContacts - 1);
  drawScreen();
}

void saveContactToSIM(String name, String number) {
  if (name.length() == 0 || number.length() != 11 || !number.startsWith("09")) {
    showTemporaryMessage("Invalid!", 900);
    return;
  }
  String numberToStore = number.substring(2);
  modem.sendAT("+CPBW=,\"+639" + numberToStore + "\",145,\"" + name + "\"");
  if (modem.waitResponse(10000L) == 1)
    showTemporaryMessage("Saved!", 900);
  else
    showTemporaryMessage("Save failed", 900);

  updateContacts();
  sortContactsByName();
  currentContactIndex = 0;
  drawScreen();
}

void sendMessageSMS(String number, String text) {
  String formatted = number;
  if (number.startsWith("09"))
    formatted = "+639" + number.substring(2);
  if (text.length() > 0) {
    //showTemporaryMessage("Sending...", 900);
    bool ok = modem.sendSMS(formatted, text);
    showTemporaryMessage(ok ? "Sent!" : "Failed", 900);
  }
}

/*String filterSMSCharacters(String input) {
  String output = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    // Only allow standard ASCII characters (32-126)
    if (c >= 32 && c <= 126) {
      output += c;
    } else {
      // Replace problematic characters with space
      output += ' ';
    }
  }
  return output;
}

void sendMessageSMS(String number, String text) {
  String formatted = number;
  if (number.startsWith("09"))
    formatted = "+639" + number.substring(2);
  
  // Filter the message text
  String filteredText = filterSMSCharacters(text);
  
  if (filteredText.length() > 0) {
    bool ok = modem.sendSMS(formatted, filteredText);
    showTemporaryMessage(ok ? "Sent!" : "Failed", 900);
  }
}*/

void waitForPhonebookReady() {
  //Wait for PB DONE (phonebook ready) or valid +CPBR response
  unsigned long start = millis();
  String modemLine;
  bool pbDone = false;
  SerialMon.println("Waiting for phonebook...");

  //Wait 8 seconds (max)
  while (millis() - start < 8000) {
    //Send empty AT just to keep responses flowing
    modem.sendAT("");
    delay(100);

    while (modem.stream.available()) {
      modemLine = modem.stream.readStringUntil('\n');
      modemLine.trim();
      if (modemLine.indexOf("PB DONE") != -1) {
        pbDone = true;
        SerialMon.println("PB DONE detected!");
        break;
      }
    }
    if (pbDone)
      break;
  }

  //As fallback, check if a +CPBR yields contacts
  if (!pbDone) {
    modem.sendAT("+CPBR=1,1");
    String resp;
    if (modem.waitResponse(2000, resp) == 1 && resp.indexOf("+CPBR:") != -1) {
      SerialMon.println("Phonebook access OK!");
    } else {
      SerialMon.println("No phonebook access after waiting!");
    }
  }
}

//====== BATTERY FUNCTIONS =======
void updateBatteryLevel() {
  int rawValue = analogRead(BATTERY_PIN);
  
  // Convert yung ADC reading sa actual battery voltage
  float adcVoltage = (rawValue * REFERENCE_VOLTAGE) / 4095.0;
  float actualVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
  
  batteryVoltage = actualVoltage; // mV
  batteryPercentage = map(constrain(actualVoltage, MIN_BATTERY_VOLTAGE, MAX_BATTERY_VOLTAGE), 
                         MIN_BATTERY_VOLTAGE, MAX_BATTERY_VOLTAGE, 0, 100);
  
  SerialMon.print("Battery - Raw: ");
  SerialMon.print(rawValue);
  SerialMon.print(", Voltage: ");
  SerialMon.print(actualVoltage / 1000.0, 2);
  SerialMon.print("V, Percentage: ");
  SerialMon.print(batteryPercentage);
  SerialMon.println("%");
}

void checkBatteryLevel() {
  unsigned long currentTime = millis();
  if (currentTime - lastBatteryCheck > BATTERY_CHECK_INTERVAL) {
    updateBatteryLevel();
    lastBatteryCheck = currentTime;
    
    if (batteryPercentage >= 0 && batteryPercentage <= 100) {
      if (batteryPercentage <= 20 && batteryPercentage > 10 && !lowBatteryWarningGiven) {
        speak("Warning: Low battery. " + String(batteryPercentage) + " percent remaining.");
        lowBatteryWarningGiven = true;
      }
      else if (batteryPercentage <= 10 && !criticalBatteryWarningGiven) {
        speak("Critical: Battery very low. " + String(batteryPercentage) + " percent remaining.");
        criticalBatteryWarningGiven = true;
      }
      
      if (batteryPercentage > 20) {
        lowBatteryWarningGiven = false;
        criticalBatteryWarningGiven = false;
      }
    }
  }
}

void drawBatteryIndicator() {
  int batteryX = 88;
  int batteryY = 2;
  int batteryWidth = 18;
  int batteryHeight = 8;
  
  // Batt outline
  u8g2.drawFrame(batteryX, batteryY, batteryWidth, batteryHeight);
  u8g2.drawBox(batteryX + batteryWidth, batteryY + 2, 2, 4);  // Tip ng batt
  
  if (batteryPercentage >= 0 && batteryPercentage <= 100) {
    // Battery fill based sa percentage
    int fillWidth = map(batteryPercentage, 0, 100, 0, batteryWidth - 2);
    if (fillWidth > 0) {
      u8g2.drawBox(batteryX + 1, batteryY + 1, fillWidth, batteryHeight - 2);
    }
    
    // Percentage sa right ng battery icon
    u8g2.setCursor(batteryX + batteryWidth + 4, batteryY + 8);
    u8g2.print(String(batteryPercentage) + "%");
  } else {
    u8g2.setCursor(batteryX + batteryWidth + 4, batteryY + 8);
    u8g2.print("ERR");
  }
}

void handleLearningModeInput() {
  static bool processingInput = false;
  
  // Check for button presses
  bool anyButtonPressed = false;
  for (int i = 0; i < 6; i++) {
    if (digitalRead(braillePins[i]) == LOW) {
      anyButtonPressed = true;
      
      // Add to learning input if not already present
      if (learningInput.indexOf(String(i)) == -1) {
        learningInput += String(i);
        lastLearningInputTime = millis();
        String buttonName = "button " + String(i + 1);
        speak(buttonName);
        
        drawScreen();
      }
      delay(300);
    }
  }
  
  // Check for AUX button presses
  if (mcp.digitalRead(AUX_A_PIN) == LOW && learningInput.length() > 0 && !processingInput) {
    processingInput = true;
    processLearningInput();
    while (mcp.digitalRead(AUX_A_PIN) == LOW);
    processingInput = false;
    return;
  }
  
  if (mcp.digitalRead(AUX_C_PIN) == LOW && learningInput.length() > 0) {
    learningInput = "";
    learningResult = "";
    learningState = LEARNING_IDLE;
    speak("Input cancelled");
    drawScreen();
    while (mcp.digitalRead(AUX_C_PIN) == LOW);
    return;
  }
}

void processLearningInput() {
  // Convert input numbers to braille pattern
  String pattern = "000000";
  for (int i = 0; i < learningInput.length(); i++) {
    int index = learningInput.charAt(i) - '0';
    if (index >= 0 && index < 6) {
      pattern.setCharAt(index, '1');
    }
  }

  // Find all matching characters
  String letterMatches = "";
  String numberMatches = "";
  String specialMatches = "";

  String displayLetters = "";
  String displayNumbers = "";
  String displaySpecials = "";

  // Check letter mappings
  for (int i = 0; i < brailleAlphabetSize; i++) {
    if (pattern == brailleAlphabet[i].pattern) {
      letterMatches += brailleAlphabet[i].character;
      letterMatches += " ";
      displayLetters += brailleAlphabet[i].character;
      displayLetters += " ";
    }
  }

  // Check number mappings
  for (int i = 0; i < brailleAlphabetSize; i++) {
    if (pattern == brailleAlphabet[i].pattern) {
      if (brailleAlphabet[i].character >= 'A' && brailleAlphabet[i].character <= 'J') {
        char numChar = "1234567890"[brailleAlphabet[i].character - 'A'];
        numberMatches += numChar;
        numberMatches += " ";
        displayNumbers += numChar;
        displayNumbers += " ";
      }
    }
  }

  // Check special character mappings
  for (int i = 0; i < brailleSpecialCharsSize; i++) {
    if (pattern == brailleSpecialChars[i].pattern) {
      specialMatches += brailleSpecialChars[i].character;
      specialMatches += " ";
      displaySpecials += brailleSpecialChars[i].character;
      displaySpecials += " ";
    }
  }

  // Build result string for speech/display
  learningResult = "";
  if (letterMatches.length() > 0) learningResult += "L: " + letterMatches;
  if (numberMatches.length() > 0) {
    if (learningResult.length() > 0) learningResult += " ";
    learningResult += "N: " + numberMatches;
  }
  if (specialMatches.length() > 0) {
    if (learningResult.length() > 0) learningResult += " ";
    learningResult += "SC: " + specialMatches;
  }

  //speaks button # not pattern
  String pressed = pressedButtonsForSpeech(learningInput);

  if (learningResult.length() == 0) {
    learningResult = "No matches found";
    if (pressed.length() > 0) {
      speak("Buttons " + pressed + " are not recognized");
    } else {
      speak("No buttons pressed");
    }
  } else {
    String toSpeak = "Buttons " + pressed + " represent ";
    bool needAnd = false;

    if (letterMatches.length() > 0) {
      String spokenLetters = convertSpecialCharsInMessage(letterMatches);
      toSpeak += "letter " + spokenLetters;
      needAnd = true;
    }

    if (numberMatches.length() > 0) {
      if (needAnd) toSpeak += " and ";
      String spokenNumbers = convertSpecialCharsInMessage(numberMatches);
      toSpeak += "number " + spokenNumbers;
      needAnd = true;
    }

    if (specialMatches.length() > 0) {
      if (needAnd) toSpeak += " and ";
      String spokenSpecials = convertSpecialCharsInMessage(specialMatches);
      toSpeak += "special character " + spokenSpecials;
    }

    speak(toSpeak);
  }

  learningState = LEARNING_RESULT;
  drawScreen();
}

void resetLearningAfterDelay() {
    static unsigned long resultDisplayTime = 0;
    
    if (learningState == LEARNING_RESULT) {
        if (resultDisplayTime == 0) {
            resultDisplayTime = millis();
        } else if (millis() - resultDisplayTime > 4000) { // 4 seconds delay
            learningInput = "";
            learningResult = "";
            learningState = LEARNING_IDLE;
            resultDisplayTime = 0;
            drawScreen();
        }
    } else {
        resultDisplayTime = 0;
    }
}

void onNewSms(int simIndex) {
  //Stop any current speech so the alert is heard immediately
  modem.textToSpeechStop(); //tts interrupt

  //refresh inbox
  updateInbox();               
  currentMessageIndex = 0;     //jump to newest

  //Announce via TTS
  if (totalMessages > 0) {
    String who = getContactNameByNumber(messages[0].sender);
    who.toLowerCase();
    speak("New message from " + who);
  } else {
    speak("New message received");
  }

  //Flash a quick banner
  showTemporaryMessage("New SMS!", 900);
}
