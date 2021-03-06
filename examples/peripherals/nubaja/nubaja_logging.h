#ifndef NUBAJA_LOGGING
#define NUBAJA_LOGGING

/* 
* includes
*/ 

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

//kernel
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

//esp
#include "esp_types.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"
#include "esp_spi_flash.h"

//SD CARD
#define PIN_NUM_MISO                        18
#define PIN_NUM_MOSI                        19
#define PIN_NUM_CLK                         14
#define PIN_NUM_CS                          15

//errors
#define SUCCESS                             0
#define I2C_READ_FAILED                     1
#define FILE_DUMP_ERROR                     2
#define FILE_CREATE_ERROR                   3

//buffer config
#define SIZE                                2000

//vars
static const char *NUBAJA_LOGGING_TAG = "NUBAJA_LOGGING";
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;
extern int LOGGING_ENABLE; 

/*
 * writes data buffer and error buffer to respective file on SD card
 */
int dump_to_file(char buffer[],char err_buffer[],int unmount) {
    FILE *fp;
    if(LOGGING_ENABLE == 1) {
        fp = fopen("/sdcard/data.txt", "a");
        if (fp == NULL)
        {
            ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to open data file for writing");
            return FILE_DUMP_ERROR;
        }   
        fputs(buffer, fp);        
        fclose(fp);
        memset(buffer,0,strlen(buffer)); 

        fp = fopen("/sdcard/error.txt", "a");
        if (fp == NULL)
        {
            ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to open error file for writing");
            return FILE_DUMP_ERROR;
        }   
        fputs(err_buffer, fp);        
        fclose(fp);  
        memset(err_buf,0,strlen(err_buf)); 
    }  

    if (unmount == 1) {
        fp = fopen("/sdcard/data.txt", "a");
        fputs("ded\n", fp);  
        fclose(fp);

        fp = fopen("/sdcard/error.txt", "a");
        fputs("ded\n", fp);  
        fclose(fp);

        esp_vfs_fat_sdmmc_unmount();
        ESP_LOGI(NUBAJA_LOGGING_TAG, "umounted");
        return SUCCESS;
    }

    
    ESP_LOGI(NUBAJA_LOGGING_TAG, "buffers dumped");    
    return SUCCESS;
}

/*
* 
*/
void record_error(char err_buffer[], char err_msg[]) {
    int length = strlen(err_msg);
    strcat(err_buf,err_msg);
    strcat(err_buf," \n");
    err_buffer_idx+=length;
    if (err_buffer_idx >= SIZE) {
        err_buffer_idx = 0;
        char null_buf[1];
        dump_to_file(null_buf,err_buf,0);  
    }     
}

/*
* 
*/
void ERROR_HANDLE_ME(int err_num) {
    char msg[50];
    switch (err_num) {
        case 0: //no error
            break;  
        case 1: //i2c read error
            strcpy(msg,"i2c read error\n");
            record_error(err_buf,msg);
            break;    
        case 2: //file dump error
            strcpy(msg, "file dump error\n");
            record_error(err_buf,msg);
            break; 
        case 3: //file dump error
            strcpy(msg, "file create error\n");
            record_error(err_buf,msg);
            break;             
        default: 
            NULL;
    }
}

/*
 * appends 12b integer to the end of the buffer
 * adds 3 hex digits to the end of the buffer
 * designed with use case of adc read in mind (12b resolution)
 */
void add_12b_to_buffer (char buf[],uint16_t i_to_add) {
    char formatted_string [13]; //number of bits + 1
    sprintf(formatted_string,"%03x",i_to_add);
    strcat(buf,formatted_string);
    strcat(buf," ");
    buffer_idx+=4;
    if (buffer_idx >= SIZE) {
        buffer_idx = 0;
        ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
    }   
}

/*
 * appends 16b integer to the end of the buffer
 * adds 4 hex digits to the end of the buffer
 * designed for use with I2C reads of the itg-3200
 * which has 16b registers
 */
void add_16b_to_buffer (char buf[],uint16_t i_to_add) {
    char formatted_string [17]; //number of bits + 1
    sprintf(formatted_string,"%04x",i_to_add);
    strcat(buf,formatted_string);
    strcat(buf," ");
    buffer_idx+=5;
    if (buffer_idx >= SIZE) {
       buffer_idx = 0;
       ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
    }    
}

/*
 * appends 32b float to the end of the buffer
 * adds 8 hex digits to the end of the buffer
 */
void add_32b_to_buffer (char buf[],float f_to_add) {
    char formatted_string [33]; //number of bits + 1
    uint32_t i_to_add = (uint32_t) f_to_add;
    sprintf(formatted_string,"%08x",i_to_add);
    strcat(buf,formatted_string);
    strcat(buf," ");
    buffer_idx+=9;
    if (buffer_idx >= SIZE) {
       buffer_idx = 0;
       ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
    }    
}

/*
 * mounts SD card
 * configures SPI bus for SD card comms
 * SPI lines need 10k pull-ups 
 * creates two files, one for data and one for errors
 * suspends task if it fails - no point in running if no data can be recorded
 */
int sd_config() 
{
    ESP_LOGI(NUBAJA_LOGGING_TAG, "sd_config");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = PIN_NUM_MISO;
    slot_config.gpio_mosi = PIN_NUM_MOSI;
    slot_config.gpio_sck  = PIN_NUM_CLK;
    slot_config.gpio_cs   = PIN_NUM_CS;    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };

    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    
    FILE *fp;
    fp = fopen("/sdcard/data.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to create file");
        return FILE_CREATE_ERROR;
    }   
    fputs("ALIVE\n", fp); 
    fclose(fp);

    fp = fopen("/sdcard/error.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to create file");
        return FILE_CREATE_ERROR;
    }   
    fputs("ALIVE\n", fp); 
    fclose(fp);    

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to mount filesystem; suspending task");
            vTaskSuspend(NULL);
            // ESP_LOGE(TAG, "Failed to mount filesystem. "
            //     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(NUBAJA_LOGGING_TAG, "Failed to initialize the card");
            vTaskSuspend(NULL);
        }
    }

    return SUCCESS;
}



#endif