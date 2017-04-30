#include <SPI.h>

#include "font.h"

const int latchPin = 8;
const int oePin = 7;
const int iDelay = 250;
unsigned int iSecond = 1000;

const int rows = 10;
const int columns = 20; // should be less than 33


unsigned long data[rows]; // 32-bit
unsigned long data2[rows]; // 32-bit
unsigned int row;         // 16-bit

void updatebuffer(void);
void clearbuffer(void);
int findchar(char c);
void echobuffer(void);

const unsigned int pending_size = 50;
unsigned char pending[pending_size];
unsigned int pending_head = 0;
unsigned int pending_tail = 0;


unsigned long last_time = 0;



unsigned long ui_seconds = 0 ;
unsigned long last_seconds = 0;

#include <SoftwareSerial.h>

SoftwareSerial mySerial(10, 5); // RX, TX

//#define DEBUG

#if defined(DEBUG)
#define println(...) Serial.println(__VA_ARGS__)
#else
#define println(...)
#endif

void inline refresh(void);

void setup() {
  // put your setup code here, to run once:
  pinMode(latchPin, OUTPUT);
  digitalWrite(oePin,HIGH);
  pinMode(oePin, OUTPUT);

  clearbuffer();

  Serial.begin(9600);

  
  
  SPI.begin();
  SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));

  refresh();
  digitalWrite(oePin,LOW);
}

void loop() {
  // put your main code here, to run repeatedly:

  unsigned long now = millis();

  if (now - last_seconds > iSecond) {
    last_seconds = now;
    ui_seconds++;
    ui_seconds %= 86400; // seconds in day
  }

  if (now - last_time > iDelay) {
    last_time = now;
    updatebuffer();
    echobuffer();
  }

  refresh();
}


void inline refresh() {
  for (int i = 0; i < rows; i++) {
    digitalWrite(latchPin, LOW);
    
    unsigned int high = data[i] >> (16 - rows); // 16 - rows (= 6) bits
    unsigned int low = data[i] << rows;
    low |= (1 << i);

    
    SPI.transfer16(high);
    SPI.transfer16(low);

    //SPI.endTransaction();
    digitalWrite(latchPin, HIGH);
    //delay(1);
  }
  digitalWrite(latchPin, LOW);
    
  SPI.transfer16(0x0000);
  SPI.transfer16(0x0000);

  digitalWrite(latchPin, HIGH);
}

void clearbuffer() {
  for (int i = 0; i < rows; i++) {
    data[i] = 0x00055555;
  }
}



int inline findchar(char c) {
  for (int i = 0; i < sizeof(font_key); i++) {
    if (font_key[i] == c) {
      return i;
    }
  }
  return -1;
}

char inline mapdigit(uint8_t d) {
  switch (d) {
    case 0:
      return '0';
    case 1:
      return '1';
    case 2:
      return '2';
    case 3:
      return '3';
    case 4:
      return '4';
    case 5:
      return '5';
    case 6:
      return '6';
    case 7:
      return '7';
    case 8:
      return '8';
    case 9:
      return '9';
    default:
      return 'Z';
  }
}

unsigned char inline nextbit(unsigned char state_char_index, unsigned char state_column_index, int i) {
  if (i == 0 || i == 9) {
    return 0x00;
  }
  i--;
  if (state_char_index >= 0) {
    if (state_column_index == font_width[state_char_index]) {
      return 0x00;
    } else {
      if (font[state_char_index][state_column_index] & (1 << i)) {
        return 0xFF;
      } else {
        return 0x00;
      }
    }
  } else {
    return 0xFF;
  }
}


typedef enum {
  eSearching = 0,
  eCommandId = 1,
  eCommandSpeed1 = 2,
  eCommandSpeed2 = 3,
  eCommandSetTime1 = 4,
  eCommandSetTime2 = 5,
  eCommandSetTime3 = 6,
  eCommandSetTime4 = 7,  
} EParseState;

EParseState e_parse_state = eSearching;

unsigned int uiOffset = 0;
bool b_enable_scroll = true;

unsigned int ui_new_speed = 0;
unsigned long ui_new_seconds = 0;

void processCommand(void) {
  while (pending_head != pending_tail) {
    refresh();
    switch (e_parse_state) {
    case eSearching:
      if (pending[pending_tail] == 0xFF) {
        e_parse_state = eCommandId;
      }
      break;
    case eCommandId:
      switch (pending[pending_tail]) {
      case 0x00:
        // reset counter
        ui_seconds = 0;
        e_parse_state = eSearching;
        break;
      case 0x01:
        // enable scroll
        b_enable_scroll = true;
        e_parse_state = eSearching;
        break;
      case 0x02:
        // disable scroll
        b_enable_scroll = false;
        uiOffset = 0;
        e_parse_state = eSearching;
        break;
      case 0x03:
        e_parse_state = eCommandSpeed1;
        break;
      case 0x04:
        e_parse_state = eCommandSetTime1;
        break;
      default:
        e_parse_state = eSearching;
      }
      break;
    case eCommandSpeed1:
      ui_new_speed = pending[pending_tail] << 8;
      e_parse_state = eCommandSpeed2;
      break;
    case eCommandSpeed2:
      ui_new_speed |= pending[pending_tail];
      iSecond = ui_new_speed;
      e_parse_state = eSearching;
      break;
    case eCommandSetTime1:
      ui_new_seconds = ((unsigned long)pending[pending_tail]) << 24;
      e_parse_state = eCommandSetTime2;
      break;
    case eCommandSetTime2:
      ui_new_seconds |= ((unsigned long)pending[pending_tail]) << 16;
      e_parse_state = eCommandSetTime3;
      break;
    case eCommandSetTime3:
      ui_new_seconds |= ((unsigned long)pending[pending_tail]) << 8;
      e_parse_state = eCommandSetTime4;
      break;
    case eCommandSetTime4:
      ui_new_seconds |= pending[pending_tail];
      ui_seconds = ui_new_seconds;
      e_parse_state = eSearching;
      break;
    }
    pending_tail++;
    pending_tail %= pending_size;
  }
}


void updatebuffer(void) {

  processCommand();

  refresh();

  char digits[6] = {'0', '0', ':', '0', '0', ' '};
  uint8_t num_digits = 0;
  unsigned int s = ui_seconds % 60;
  unsigned int m = (ui_seconds % 3600) / 60;
  unsigned int h = ui_seconds / 3600;

  while (s && num_digits < 2) {
    digits[4 - num_digits++] = mapdigit(s % 10);
    s = s/10;
  }

  num_digits = 0;
  while (m && num_digits < 2) {
    digits[1 - num_digits++] = mapdigit(m % 10);
    m = m/10;
  }

  refresh();

  for (int i = 0; i < rows; i++) {
    data2[i] = 0x00000000;
  }

  refresh();

  unsigned char state_text_index = 0;
  unsigned char state_char_index = findchar(digits[state_text_index]);
  unsigned char state_column_index = 0;

  unsigned int position = 0;

  while (position < uiOffset + columns) {      
    if (position >= uiOffset) {
      uint32_t column = position - uiOffset;
      for (int i = 0; i < rows; i++) {
        refresh();
        data2[i] |= (uint32_t)(nextbit(state_char_index, state_column_index, i) & 1) << column;
      }
    }

    refresh();

    if (state_column_index >= 0 && 
        ++state_column_index > font_width[state_char_index]) {
      // move to next char
      state_text_index++;
      state_text_index %= sizeof(digits);
      state_char_index = findchar(digits[state_text_index]);
      state_column_index = 0;
    }

    position++;    
  }

  refresh();
  memcpy(data, data2, sizeof(uint32_t) * rows);
  if (b_enable_scroll) {
    uiOffset++;
  }
  uiOffset %= 4 + 4 + 4 + 4 + 3 + 1 + 6;
}


void echobuffer() {
  Serial.write(0xFF);
  for (int i = 0; i < rows; i++) {
    refresh();
    unsigned char a = data[i] >> 24;
    unsigned char b = (data[i] >> 16) & 0xFF;
    unsigned char c = (data[i] >> 8) & 0xFF;
    unsigned char d = data[i] & 0xFF;
    Serial.write(0xFF);
    Serial.write(a);
    Serial.write(b);
    Serial.write(c);
    Serial.write(d);
  }
}

void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    unsigned char inChar = Serial.read();
    // add it to the inputString:
    unsigned int new_head = (pending_head + 1) % pending_size;
    if (new_head != pending_tail) { // drop characters over size
      pending[pending_head] = inChar;
      pending_head = new_head;
    }
  }
}
