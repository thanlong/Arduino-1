//
//    FILE: AD985X.cpp
//  AUTHOR: Rob Tillaart
// VERSION: 0.3.1
//    DATE: 2019-02-08
// PURPOSE: Class for AD9850 and AD9851 function generator
//
//  HISTORY:
//  0.1.0   2019-03-19  initial version
//  0.1.1   2020-12-09  add arduino-ci
//  0.1.2   2020-12-27  add setAutoMode() + offset
//  0.2.0   2020-12-28  major refactor class hierarchy + float frequency
//  0.2.1   2021-01-10  add get- and setARCCutOffFreq()
//  0.2.2   2021-01-24  add manual updating frequency 
//                      get- setManualFQ_UD(), update()
//                      inverted SELECT line as preparation for multidevice.
//  0.3.0   2021-06-06  fix factory bit mask + new examples + some refactor
//                      added multi device document
//  0.3.1   2021-08-25  VSPI / HSPI support for ESP32
//                      faster software SPI
//                      minor optimizations / refactor


#include "AD985X.h"


// UNO HARDWARE SPI           PINS
#define SPI_CLOCK             13
#define SPI_MISO              12
#define SPI_MOSI              11


#define AD985X_POWERDOWN      0x04


////////////////////////////////////////////////////////
//
// AD9850
//

AD9850::AD9850()
{
}


void AD9850::begin(uint8_t select, uint8_t resetPin, uint8_t FQUDPin, uint8_t dataOut , uint8_t clock)
{
  _select  = select;
  _reset   = resetPin;
  _fqud    = FQUDPin;
  _dataOut = dataOut;
  _clock   = clock;
  // following 3 are always set.
  pinMode(_select, OUTPUT);
  pinMode(_reset,  OUTPUT);
  pinMode(_fqud,   OUTPUT);
  // device select = HIGH  See - https://github.com/RobTillaart/AD985X/issues/13
  digitalWrite(_select, LOW);  
  digitalWrite(_reset,  LOW);
  digitalWrite(_fqud,   LOW);

  _hwSPI = ((dataOut == 0) || (clock == 0));

  _spi_settings = SPISettings(2000000, LSBFIRST, SPI_MODE0);

  if (_hwSPI)
  {
    #if defined(ESP32)
    if (_useHSPI)      // HSPI
    {
      mySPI = new SPIClass(HSPI);
      mySPI->end();
      mySPI->begin(14, 12, 13, select);   // CLK=14 MISO=12 MOSI=13
    }
    else               // VSPI
    {
      mySPI = new SPIClass(VSPI);
      mySPI->end();
      mySPI->begin(18, 19, 23, select);   // CLK=18 MISO=19 MOSI=23
    }
    #else              // generic hardware SPI
    mySPI = &SPI;
    mySPI->end();
    mySPI->begin();
    #endif
  }
  else                 // software SPI
  {
    pinMode(_dataOut, OUTPUT);
    pinMode(_clock,   OUTPUT);
    digitalWrite(_dataOut, LOW);
    digitalWrite(_clock,   LOW);
  }

  reset();
}


#if defined(ESP32)
void AD9850::setGPIOpins(uint8_t clk, uint8_t miso, uint8_t mosi, uint8_t select)
{
  _clock   = clk;
  _dataOut = mosi;
  _select  = select;
  pinMode(_select, OUTPUT);
  digitalWrite(_select, LOW);

  mySPI->end();  // disable SPI 
  mySPI->begin(clk, miso, mosi, select);
}
#endif


void AD9850::reset()
{
  // be sure to select the correct device 
  digitalWrite(_select, HIGH);
  pulsePin(_reset);
  if (_hwSPI) pulsePin(SPI_CLOCK);
  else pulsePin(_clock);
  digitalWrite(_select, LOW);

  _config = 0;    // 0 phase   no power down
  _freq   = 0;
  _factor = 0;
  _offset = 0;
  _autoUpdate = true;
  writeData();
}


void AD9850::powerDown()
{
  _config |= AD985X_POWERDOWN;      // keep phase and REFCLK as is.
  writeData();
}


void AD9850::powerUp()
{
  _config &= ~AD985X_POWERDOWN;
  writeData();
}


void AD9850::setPhase(uint8_t phase)
{
  if (phase > 31) return;
  _config &= 0x07;
  _config |= (phase << 3);
  writeData();
}


void AD9850::pulsePin(uint8_t pin)
{
  digitalWrite(pin, HIGH);
  digitalWrite(pin, LOW);
}


void AD9850::setSPIspeed(uint32_t speed)
{
  _SPIspeed = speed;
  _spi_settings = SPISettings(_SPIspeed, LSBFIRST, SPI_MODE0);
};


void AD9850::writeData()
{
  // Serial.println(_factor, HEX);
  // Serial.println(_config, HEX);
  uint32_t data = _factor;

  // used for multidevice config only - https://github.com/RobTillaart/AD985X/issues/13
  digitalWrite(_select, HIGH);  
  if (_hwSPI)
  {
    mySPI->beginTransaction(_spi_settings);
    mySPI->transfer(data & 0xFF);
    data >>= 8;
    mySPI->transfer(data & 0xFF);
    data >>= 8;
    mySPI->transfer(data & 0xFF);
    mySPI->transfer(data >> 8);
    mySPI->transfer(_config & 0xFC);  // mask factory test bit
    mySPI->endTransaction();
  }
  else
  {
    swSPI_transfer(data & 0xFF);
    data >>= 8;
    swSPI_transfer(data & 0xFF);
    data >>= 8;
    swSPI_transfer(data & 0xFF);
    swSPI_transfer(data >> 8);
    swSPI_transfer(_config & 0xFC);  // mask factory test bit
  }
  digitalWrite(_select, LOW);

  // update frequency + phase + control bits.
  // should at least be 4 ns delay - P14 datasheet
  if (_autoUpdate) update();
}


// simple one mode version
void AD9850::swSPI_transfer(uint8_t val)
{
  uint8_t clk = _clock;
  uint8_t dao = _dataOut;
  // for (uint8_t mask = 0x80; mask; mask >>= 1)   // MSBFIRST
  for (uint8_t mask = 0x01; mask; mask <<= 1)   // LSBFIRST
  {
    digitalWrite(dao, (val & mask));
    digitalWrite(clk, HIGH);
    digitalWrite(clk, LOW);
  }
}


void AD9850::setFrequency(uint32_t freq)
{
  // freq OUT = (Δ Phase × CLKIN)/2^32
  // 64 bit math to keep precision to the max
  if (freq > AD9850_MAX_FREQ) freq = AD9850_MAX_FREQ;
  // _factor = round(freq * 34.359738368);             // 4294967296 / 125000000
  _factor = (147573952589ULL * freq) >> 32;
  _freq = freq;
  _factor += _offset;
  writeData();
}


// especially for lower frequencies (with decimals)
void AD9850::setFrequencyF(float freq)
{
  // freq OUT = (Δ Phase × CLKIN)/2^32
  // 64 bit math to keep precision to the max
  if (freq > AD9850_MAX_FREQ) freq = AD9850_MAX_FREQ;
  _factor = round(freq * 34.359738368);                // 4294967296 / 125000000
  _freq = freq;
  _factor += _offset;
  writeData();
}


void AD9850::update()
{
  digitalWrite(_select, HIGH);  
  pulsePin(_fqud);
  digitalWrite(_select, LOW);  
}


////////////////////////////////////////////////////////
//
// AD9851
//

#define AD9851_REFCLK        0x01    // bit is a 6x multiplier bit P.14 datasheet

void AD9851::setFrequency(uint32_t freq)
{
  // PREVENT OVERFLOW
  if (freq > AD9851_MAX_FREQ) freq = AD9851_MAX_FREQ;

  // AUTO SWITCH REFERENCE FREQUENCY
  if (_autoRefClock)
  {
    if (freq > _ARCCutOffFreq)
    {
      _config |= AD9851_REFCLK;
    }
    else
    {
      _config &= ~AD9851_REFCLK;
    }
  }

  if (_config & AD9851_REFCLK)  // 6x 30 = 180 MHz
  {
    _factor = (102481911520ULL * freq) >> 32;  //  (1 << 64) / 180000000
  }
  else                          // 1x 30 = 30 MHz
  {
    _factor = (614891469123ULL * freq) >> 32;  //  (1 << 64) / 30000000
  }
  _freq = freq;
  _factor += _offset;
  writeData();
}


// especially for lower frequencies (with decimals)
void AD9851::setFrequencyF(float freq)
{
  // PREVENT OVERFLOW
  if (freq > AD9851_MAX_FREQ) freq = AD9851_MAX_FREQ;

  // AUTO SWITCH REFERENCE FREQUENCY
  if (_autoRefClock)
  {
    if (freq > _ARCCutOffFreq)
    {
      _config |= AD9851_REFCLK;
    }
    else
    {
      _config &= ~AD9851_REFCLK;
    }
  }

  if (_config & AD9851_REFCLK)  // 6x 30 = 180 MHz
  {
    _factor = uint64_t(102481911520ULL * freq) >> 32;  //  (1 << 64) / 180000000
  }
  else                          // 1x 30 = 30 MHz
  {
    _factor = (6148914691ULL * uint64_t (100 * freq)) >> 32;
  }

  _freq = freq;
  _factor += _offset;
  writeData();
}


////////////////////////////////////////////////////////
//
// AD9851 - AUTO REFERENCE CLOCK
//
void AD9851::setAutoRefClock(bool arc)
{
  _autoRefClock = arc;
  setFrequency(_freq);
};


void AD9851::setRefClockHigh()
{
  _config |= AD9851_REFCLK;
  setFrequency(_freq);
}


void AD9851::setRefClockLow()
{
  _config &= ~AD9851_REFCLK;
  setFrequency(_freq);
}


uint8_t AD9851::getRefClock()
{
  return (_config & AD9851_REFCLK) ? 180 : 30;
}

void AD9851::setARCCutOffFreq(uint32_t Hz)
{
  if (Hz > 30000000UL) Hz = 30000000;
  _ARCCutOffFreq = Hz;
};


// -- END OF FILE --
