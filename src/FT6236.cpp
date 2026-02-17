/*
This is a library for the FT6236 touchscreen controller by FocalTech.

The FT6236 and FT6236u work the same way.

A lot of this library is originally written by Limor Fried/Ladyada.

Because Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!

@section author Author
Written by Limor Fried/Ladyada for Adafruit Industries.

@section license License
MIT license, all text above must be included in any redistribution
*/

#include "FT6236.h"
#include "pico/stdlib.h"
#include <print>

/* New class. */
FT6236::FT6236() { touches = 0; }

/* Start I2C and check if the FT6236 is found. */
bool FT6236::begin(uint8_t thresh, i2c_inst_t *i2c, int8_t sda, int8_t scl)
{
    this->i2c = i2c;
    i2c_init(i2c, 400 * 1000);
    if (sda != -1) {
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_pull_up(sda);
    }
    if (scl != -1) {
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(scl);
    }

    // Adjust threshold
    writeRegister8(FT6236_REG_THRESHHOLD, thresh);

    //Check if our chip has the correct Vendor ID
    if (readRegister8(FT6236_REG_VENDID) != FT6236_VENDID)
    {
        return false;
    }
    //Check if our chip has the correct Chip ID.
    uint8_t id = readRegister8(FT6236_REG_CHIPID);
    if ((id != FT6236_CHIPID) && (id != FT6236U_CHIPID) &&
      (id != FT6206_CHIPID))
    {
        return false;
    }

    return true;
}

/* Returns the number of touches */
uint8_t FT6236::touched(void)
{
    uint8_t n = readRegister8(FT6236_REG_NUMTOUCHES);
    if (n > 2)
    {
        n = 0;
    }
    return n;
}

/* Get a touch point */
TS_Point FT6236::getPoint(uint8_t n)
{
    readData();
    if ((touches == 0) || (n > 1))
    {
        return TS_Point(0, 0, 0);
    }
    else
    {
        return TS_Point(touchX[n], touchY[n], 1);
    }
}

void FT6236::readData(void)
{
    uint8_t i2cdat[16];
    uint8_t reg = 0;
    i2c_write_blocking(i2c, FT6236_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c, FT6236_ADDR, i2cdat, 16, false);

    touches = i2cdat[0x02];
    if ((touches > 2) || (touches == 0))
    {
        touches = 0;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        touchX[i] = i2cdat[0x03 + i * 6] & 0x0F;
        touchX[i] <<= 8;
        touchX[i] |= i2cdat[0x04 + i * 6];
        touchY[i] = i2cdat[0x05 + i * 6] & 0x0F;
        touchY[i] <<= 8;
        touchY[i] |= i2cdat[0x06 + i * 6];
        touchID[i] = i2cdat[0x05 + i * 6] >> 4;
    }
}

/* Reading a byte from a register */
uint8_t FT6236::readRegister8(uint8_t reg)
{
    uint8_t value;
    i2c_write_blocking(i2c, FT6236_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c, FT6236_ADDR, &value, 1, false);
    return value;
}

/* Writing a byte to a register */
void FT6236::writeRegister8(uint8_t reg, uint8_t val)
{
    i2c_write_blocking(i2c, FT6236_ADDR, &reg, 1, false);
}

/* Debug */
void FT6236::debug(void)
{
    std::print("Vend ID: 0x{:x}", (int)readRegister8(FT6236_REG_VENDID));
    std::print("Chip ID: 0x{:X}", (int)readRegister8(FT6236_REG_CHIPID));
    std::print("Firm V : {}", (int)readRegister8(FT6236_REG_FIRMVERS));
    std::print("Point Rate Hz: {}", (int)readRegister8(FT6236_REG_POINTRATE));
    std::print("Thresh: {}", (int)readRegister8(FT6236_REG_THRESHHOLD));
}

TS_Point::TS_Point(void) { x = y = z = 0; }

TS_Point::TS_Point(int16_t _x, int16_t _y, int16_t _z)
{
    x = _x;
    y = _y;
    z = _z;
}

/* == comparator between two points */
bool TS_Point::operator==(TS_Point p1)
{
    return ((p1.x == x) && (p1.y == y) && (p1.z == z));
}

/* != comparator netween two points */
bool TS_Point::operator!=(TS_Point p1)
{
    return ((p1.x != x) || (p1.y != y) || (p1.z != z));
}
