/*
  This sketch stores the SI4735 SSB patch content on an EEPROM. 
  It might useful for Board or MCU with few memory.

  Under construction....
  
  Ricardo Lima Caratti 2020    
*/

// under construction ...

#include <SI4735.h>
#include "patch_init.h" // SSB patch for whole SSBRX initialization string

const uint16_t size_content = sizeof ssb_patch_content; // see ssb_patch_content in patch_full.h or patch_init.h

//defines the EEPROM I2C addresss.
#define EEPROM_I2C_ADDR 0x50 // You might need to change this value

// EEPROM address type
typedef union {
  struct
  {
    uint8_t lowByte;
    uint8_t highByte;
  } raw;
  uint16_t offset;
} eeprom_offset;

const uint8_t content_id[] = "SI4735-D60-init";
// const uint8_t content_id[] = "SI4735-D60-full";
const uint16_t size_id = sizeof content_id;

void setup()
{
  Serial.begin(9600);
  while(!Serial);

  Serial.println("Storing the patch file..");
  eepromWritePatch();
  Serial.println("Finish.");

}



/**
 * Writes an identification that will be check by the load SSB patch process 
 * 
 */
void eepromWriteId() {
     eepromWriteBlock(EEPROM_I2C_ADDR,0,content_id,size_id);
}

/**
 * Writes the patch 
 */
void eepromWritePatch() {
  eepromWriteId();
  eepromWriteBlock(EEPROM_I2C_ADDR, size_id, ssb_patch_content, size_content);
} 


/*
 * Writes a data (byte) to the eepro at a given position (offset).
 * @param i2c_address I2C bus address of the eeprom device 
 * @param offset address (position) where the data will be saved;
 * @param data  data to be saved 
 */
void eepromWrite(uint8_t i2c_address, uint16_t offset, uint8_t data)
{
  eeprom_offset eeprom;
  eeprom.offset = offset;
  Wire.beginTransmission(i2c_address);
  // First, you have to tell where you want to save the data (offset is the position).
  Wire.write(eeprom.raw.highByte); // Most significant Byte
  Wire.write(eeprom.raw.lowByte);  // Less significant Byte
  Wire.write(data);                // Writes the data at the right position (offset)
  Wire.endTransmission();
  delay(5);
}

uint8_t * eepromWriteBlock(uint8_t i2c_address, uint16_t offset, uint8_t const * pData, uint8_t blockSize)
{
  eeprom_offset eeprom;
  eeprom.offset = offset;

  Wire.beginTransmission(i2c_address);
  Wire.write(eeprom.raw.highByte); // Most significant Byte
  Wire.write(eeprom.raw.lowByte);  // Less significant Byte

  for (int i = 0; i < blockSize; i++)
    Wire.write(*pData++); //dispatch the data bytes
 
  Wire.endTransmission();
  delay(5); 
  return pData;
}

/*
 * Reads a data (byte) from the eepro at a given position (offset).
 * @param i2c_address I2C bus address of the eeprom device 
 * @param offset address (position) where the data will be saved;
 * @return data (byte)
 */
byte readEEPROM(uint8_t i2c_address, uint16_t offset)
{
  eeprom_offset eeprom;
  eeprom.offset = offset;
  Wire.beginTransmission(i2c_address);
  Wire.write(eeprom.raw.highByte); // Most significant Byte
  Wire.write(eeprom.raw.lowByte);  // Less significant Byte
  Wire.endTransmission();
  Wire.requestFrom(i2c_address, 1);
  if (Wire.available())
    return Wire.read();
  else
    return 0xFF;
}

// Erase all eeprom content
void eepromClear()
{
}

void loop()
{
}
