#ifndef AS1115_DRIVER
#define AS1115_DRIVER

#include "nubaja_i2c_driver.h"

//DISPLAY 
#define DIGIT_0                             0x1 
#define DIGIT_1                             0x2
#define DIGIT_2                             0x3
#define DIGIT_3                             0x4
#define AS1115_SLAVE_ADDR                   0x3

static const char *AS1115_DRIVER_TAG = "AS1115_DRIVER";

/*
* configures the AS1115 7-segment display driver to work with HDSP-D03E common anode 4 digit display
*/
void AS1115_config () {
    i2c_write_byte(0x0,0x2d,0x1); //enable self addressing setting the slave-addr to 0x03   
    i2c_write_byte(0x0,0xc,0x81); //sets shutdown register for normal operation
    i2c_write_byte(0x0,0xe,0x04); //sets features as desired with hex-code font
    i2c_write_byte(AS1115_SLAVE_ADDR,0x9,0xff); //decode mode enabled for all digits
    i2c_write_byte(AS1115_SLAVE_ADDR,0xa,0xee); //global intensity set to 15/16
    i2c_write_byte(AS1115_SLAVE_ADDR,0xb,0x3); //scan limit set to only display 4 digits 
    i2c_write_byte(AS1115_SLAVE_ADDR,DIGIT_3,0xb);
    i2c_write_byte(AS1115_SLAVE_ADDR,DIGIT_2,0x0);
    i2c_write_byte(AS1115_SLAVE_ADDR,DIGIT_1,0x0);
    i2c_write_byte(AS1115_SLAVE_ADDR,DIGIT_0,0xb);
}

/*
* writes to one of the 8 digit registers of the AS1115 with the desired value. 
* the device is configured to use BCD encoding, meaning values of 0-9 and -,E,H,L,P are 
* possible 
*/
void AS1115_display_write(uint8_t slave_addr, uint8_t digit, uint8_t value) {
    i2c_write_byte(slave_addr, digit, value);
}

void display_speed (float speed) {
	uint8_t v_car_l = (uint32_t) speed % 10; 
	uint8_t v_car_h = ( (uint32_t) speed / 10) % 10; 
	AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_3,v_car_l);
	AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_2,v_car_h); 
}

void display_RPM (float RPM) {
    uint8_t rpm_3 = (uint32_t) RPM % 10; 
    uint8_t rpm_2 = ( (uint32_t) RPM / 10) % 10; 
    uint8_t rpm_1 = ( (uint32_t) RPM / 100) % 10; 
    uint8_t rpm_0 = ( (uint32_t) RPM / 1000) % 10; 
    AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_3,rpm_3);
    AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_2,rpm_2);                  
    AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_1,rpm_1);
    AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_0,rpm_0);	
}

#endif