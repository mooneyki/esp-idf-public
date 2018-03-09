#ifndef HUD_HELPER
#define HUD_HELPER

/* 
* includes
*/ 

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

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
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"

/* 
* defines
*/ 

//I2C MODULE
#define I2C_MASTER_SDA_IO                   23               /*!< gpio number for I2C master data  */
#define I2C_MASTER_SCL_IO                   22               /*!< gpio number for I2C master clock */
#define I2C_NUM                             I2C_NUM_0        /*!< I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE           0                /*!< I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE           0                /*!< I2C master do not need buffer */
#define I2C_CLK_HZ                          400000           /*!< I2C master clock frequency */
#define WRITE_BIT                           I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                            I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                        0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                       0x0              /*!< I2C master will not check ack from slave */
#define ACK                                 0x0              /*!< I2C ack value */
#define NACK                                0x1              /*!< I2C nack value */
#define DATA_LENGTH                         1                //in bytes
#define I2C_TASK_LENGTH                     1              //in ms

//GPIO 
#define GPIO_INPUT_0                        4 //wheel spd hall effect in
#define GPIO_INPUT_PIN_SEL                  (1ULL<<GPIO_INPUT_0)
#define ESP_INTR_FLAG_DEFAULT               0

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

//ADC 
#define V_REF                               1000
#define V_FS                                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE                           (V_FS / 4096)
#define ATTENUATION                         ADC_ATTEN_11db

//THERMISTOR CONFIGS 
#define THERM_M                             0.024                    
#define THERM_B                             -0.5371 //(y=mx + b, linear fit to Vout vs. temperature of thermistor circuit)
//thermistor pn: NTCALUG02A103F800

//DISPLAY 
#define DIGIT_0                             0x1 
#define DIGIT_1                             0x2
#define DIGIT_2                             0x3
#define DIGIT_3                             0x4
#define AS1115_SLAVE_ADDR                   0x3

//buffer config
#define SIZE                                2000

//TIMER CONFIGS
#define TIMER_DIVIDER                       16  //  Hardware timer clock divider
#define TIMER_SCALE                         (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_PERIOD                 .01   // control loop period for timer group 0 timer 0 in secondss
#define PROGRAM_LENGTH                      60 // program length for timer group 0 timer 1 in seconds

//CONTROL FLOW
#define SENSOR_ENABLE                       1 
#define LOGGING_ENABLE                      0     

//WIFI
#define PORT_NUMBER                         6789
#define BUFLEN                              512


/*****************************************************/


/* 
* globals
*/ 
extern const char *TAG;
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;
extern xQueueHandle timer_queue;
extern xQueueHandle gpio_queue;
extern SemaphoreHandle_t killSemaphore;
extern const char* ssid;
extern const char* password;
extern int comms_en;


//interrupt flag container
typedef struct {
    uint64_t timer_counts;
} timer_event_t;

/*****************************************************/



/* HELPER FUNCTIONS
* these functions do not interface with sensors
* but rather they perform certain operations 
* necessary to streamline and simplify operations 
*
*/

int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    if(getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1) {
        ESP_LOGE(TAG, "getsockopt failed");
        return -1;
    }
    return result;
    
}

int show_socket_error_reason(int socket)
{
    int err = get_socket_error_code(socket);
    ESP_LOGW(TAG, "socket error %d %s", err, strerror(err));
    return err;
}

void close_socket(int socket)
{
    close(socket);
}

// UDP Listener
//expects packets delivered via the following, or equivalent: 
//echo -n "start" | nc -4u -q1 localhost 6789
esp_err_t udp_server()
{
    static char WIFI_tag[]="udpserver";
    int mysocket;
    struct sockaddr_in si_other;
    
    unsigned int slen = sizeof(si_other),recv_len;
    char buf[BUFLEN];
    
    // bind to socket
    ESP_LOGI(WIFI_tag, "bind_udp_server port:%d", PORT_NUMBER);
    mysocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mysocket < 0) {
        show_socket_error_reason(mysocket);
        return ESP_FAIL;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUMBER);
    // server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_addr.s_addr = inet_addr("192.168.78.2"); //IP address

    if (bind(mysocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        show_socket_error_reason(mysocket);
        close(mysocket);
        
    } else {
        ESP_LOGI(WIFI_tag,"socket created without errors");
        while(comms_en == 1)
        {
            ESP_LOGI(WIFI_tag,"Waiting for incoming data");
            memset(buf,0,BUFLEN);
            
            if ((recv_len = recvfrom(mysocket, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1)
            {
                ESP_LOGE(WIFI_tag,"recvfrom");
                break;
            }
            
            ESP_LOGI(WIFI_tag,"Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
            ESP_LOGI(WIFI_tag,"Data: %s -- %d\n" , buf, recv_len);
            // Set the NULL byte to avoid garbage in the read buffer
            if ((recv_len + 1) < BUFLEN)
                buf[recv_len + 1] = '\0';
                        
            // Note: speed is inverse polarity
            if ( memcmp( buf, "start", recv_len) == 0) {
                ESP_LOGI(WIFI_tag,"Start Case\n");
                comms_en = 0; //exits while loop and program proceeds to task creation and normal operation
                
            } else if ( memcmp( buf, "stop", recv_len) == 0) {
                ESP_LOGI(WIFI_tag,"Stop Case\n");
                //not sure how to do this yet...
                xSemaphoreGive(killSemaphore); //GIVE SEMAPHORE
            } 
            else {
                ESP_LOGE(WIFI_tag,"Command: %s\n", buf);
            }
        }
        
        close(mysocket);
        return ESP_FAIL;
    }
    return ESP_OK;
}



/*
 * writes data buffer and error buffer to respective file on SD card
 */
int dump_to_file(char buffer[],char err_buffer[],int unmount) {
    FILE *fp;
    fp = fopen("/sdcard/data.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open data file for writing");
        return FILE_DUMP_ERROR;
    }   
    fputs(buffer, fp);        
    fclose(fp);

    fp = fopen("/sdcard/error.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open error file for writing");
        return FILE_DUMP_ERROR;
    }   
    fputs(err_buffer, fp);        
    fclose(fp);    

    if (unmount == 1) {
        fp = fopen("/sdcard/data.txt", "a");
        fputs("ded\n", fp);  
        fclose(fp);

        fp = fopen("/sdcard/error.txt", "a");
        fputs("ded\n", fp);  
        fclose(fp);

        esp_vfs_fat_sdmmc_unmount();
        ESP_LOGI(TAG, "umounted");
        return SUCCESS;
    }

    
    ESP_LOGI(TAG, "buffers dumped");
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
        memset(err_buf,0,strlen(err_buf)); 
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
    if ((buffer_idx >= SIZE) && (LOGGING_ENABLE == 1)) {
        buffer_idx = 0;
        ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
        memset(buf,0,strlen(buf)); 
    }   
}

/*
 * appends 16b integer to the end of the buffer
 * adds 2 hex digits to the end of the buffer
 * designed for use with I2C reads of the itg-3200
 * which has 16b registers
 */
void add_16b_to_buffer (char buf[],uint16_t i_to_add) {
    char formatted_string [17]; //number of bits + 1
    sprintf(formatted_string,"%04x",i_to_add);
    strcat(buf,formatted_string);
    strcat(buf," ");
    buffer_idx+=5;
    if ((buffer_idx >= SIZE) && (LOGGING_ENABLE == 1)) {
       buffer_idx = 0;
       ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
       memset(buf,0,strlen(buf));
    }    
}

/*****************************************************/

/* SENSOR INTERFACE FUNCTIONS
* these functions interface with sensors  
* in order to read and record data 
*  
*
*/

/*
 * writes a single byte of data to a register using I2C protocol 
 */
int i2c_write_byte(uint8_t slave_address, uint8_t reg, uint8_t data) {
    int ret; 
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_write_byte(cmd, data, ACK); 
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"i2c write failed");
        return I2C_READ_FAILED; //dead sensor
    } else { 
        return SUCCESS;
    }
}   

/*
 * reads a register from an I2C device
 * can be configured to read an 8bit or 16bit register 
 * automatically adds result to the data buffer
 */
int i2c_read_2_byte(int reg) 
{
    int ret;
    uint8_t* data_h = (uint8_t*) malloc(DATA_LENGTH); //comment out for one byte read
    uint8_t* data_l = (uint8_t*) malloc(DATA_LENGTH);
    uint8_t gyro_slave_address = 0x69; 

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data_h, ACK);    
    i2c_master_read_byte(cmd, data_l, NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    uint16_t data = (*data_h << 8 | *data_l); //comment out for one byte read
    //uint16_t data = *data_l; //uncomment for one byte read
    add_16b_to_buffer (f_buf, data);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"i2c read failed");
        return I2C_READ_FAILED; //dead sensor
        free(data_h); //comment out for one byte read
        free(data_l);
        vTaskSuspend(NULL);
    } else {
        free(data_h); //comment out for one byte read
        free(data_l);
        return SUCCESS;
    }
}


/*
* writes to one of the 8 digit registers of the AS1115 with the desired value. 
* the device is configured to use BCD encoding, meaning values of 0-9 and -,E,H,L,P are 
* possible 
*/
void AS1115_display_write(uint8_t slave_addr, uint8_t digit, uint8_t BCD_value) {
    ERROR_HANDLE_ME(i2c_write_byte(slave_addr, digit, BCD_value));
}

/*
* function designed with variable number of arguments
* Turns off all GPIO pins passed in as arguments
*/
void gpio_kill(int num,...)
{
    va_list valist;
    va_start(valist, num);

    for (int i=0;i<num;i++) {
        gpio_set_level(va_arg(valist, int), 0);
        gpio_set_direction(va_arg(valist, int), GPIO_MODE_INPUT);          
    }
    va_end(valist);   
}

/*
* function designed with variable number of arguments
* allows for adc reads of multiple channels without
* several repetitive function calls
* function reads a single channel from the adc
* the raw value is from 0-4095 (12b resolution)
* the raw value is then added to the data buffer appropriately
*/
void read_adc(int num,...) 
{  
    va_list valist;
    uint16_t val_0;

    /* initialize valist for num number of arguments */
    va_start(valist, num);

    /* access all the arguments assigned to valist */
    for (int i = 0; i < num; i++) {
        val_0 = adc1_get_raw(va_arg(valist, int));
        add_12b_to_buffer(f_buf,val_0);        
    }

    /* clean memory reserved for valist */
    va_end(valist);    
}


/*****************************************************/


/* CONFIGURATION (SENSOR OR MODULE) FUNCTIONS
* these functions take care of the configuration of either
* ESP32 hardware modules or sensors the ESP32 is interfacing with
* for example the I2C modules and subsequent I2C slave devices 
*
*/

/*
 * mounts SD card
 * configures SPI bus for SD card comms
 * SPI lines need 10k pull-ups 
 * creates two files, one for data and one for errors
 * suspends task if it fails - no point in running if no data can be recorded
 */
int sd_config() 
{
    ESP_LOGI(TAG, "sd_config");
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
        ESP_LOGE(TAG, "Failed to create file");
        return FILE_CREATE_ERROR;
    }   
    fputs("ALIVE\n", fp); 
    fclose(fp);

    fp = fopen("/sdcard/error.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to create file");
        return FILE_CREATE_ERROR;
    }   
    fputs("ALIVE\n", fp); 
    fclose(fp);    

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem; suspending task");
            vTaskSuspend(NULL);
            // ESP_LOGE(TAG, "Failed to mount filesystem. "
            //     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card");
            vTaskSuspend(NULL);
        }
    }

    return SUCCESS;
}

/*
 * configures one i2c module for operation as an i2c master with internal pullups disabled
 */
 void i2c_master_config() {
    // ESP_LOGI(TAG, "i2c_master_config");
    int i2c_master_port = I2C_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.master.clk_speed = I2C_CLK_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE,
                       I2C_MASTER_TX_BUF_DISABLE, 0);
}

/*
* configures the AS1115 7-segment display driver 
*/
void AS1115_config () {
    ERROR_HANDLE_ME(i2c_write_byte(0x0,0x2d,0x01)); //enable self addressing setting the slave-addr to 0x03
    uint8_t slave_address = 0x03;    
    ERROR_HANDLE_ME(i2c_write_byte(slave_address,0x9,0xff)); //decode mode enabled for all digits
    ERROR_HANDLE_ME(i2c_write_byte(slave_address,0xa,0x07)); //global intensity set to 50%
    ERROR_HANDLE_ME(i2c_write_byte(slave_address,0xb,0xff)); //scan limit set to only display 3 digits 
    ERROR_HANDLE_ME(i2c_write_byte(slave_address,0xe,0x0)); //sets features as desired
}

/*
* ISR for GPIO based interrupt. interrupt is configured via config_gpio
*/
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_queue, &gpio_num, NULL);
}

/*
* configures a GPIO pin for an interrupt on a rising edge
*/
void config_gpio() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE; //interrupt of rising edge
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL; //bit mask of the pins
    io_conf.mode = GPIO_MODE_INPUT;//set as input mode    
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);


    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT); //install gpio isr service
    gpio_isr_handler_add(GPIO_INPUT_0, gpio_isr_handler, (void*) GPIO_INPUT_0); //hook isr handler for specific gpio pin
}

/*
 * Timer group0 ISR handler
 * sets ctrl_intr flag high each time alarm occurs, re-enables alarm and sends data to main program task
 * also unblocks end_program task at appropriate time decided by PROGRAM_LENGTH
 */
void IRAM_ATTR timer_group0_isr(void *para) {
    timer_event_t evt;
    uint32_t intr_status = TIMERG0.int_st_timers.val;

    TIMERG0.hw_timer[1].update = 1;
    uint64_t timer_counter_value = ((uint64_t) TIMERG0.hw_timer[1].cnt_high) 
                                   << 32 | TIMERG0.hw_timer[1].cnt_low;
    
    if ((intr_status & BIT(0))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t0 = 1; //clear timer interrupt bit
        evt.timer_counts = timer_counter_value; 
    } 
    if ((intr_status & BIT(1))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t1 = 1; //clear timer interrupt bit
        xSemaphoreGiveFromISR(killSemaphore,NULL); //GIVE SEMAPHORE
    }
    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN; //re-enable timer for timer 0 which is timing control loop

    // send the event data back to the main program task
    xQueueSendFromISR(timer_queue, &evt, NULL);
}


/*
 * sets up timer group 0 timers 0 and 1
 * timer 0 times the control loop, set up for auto reload upon alarm
 * timer 1 times the entire program, does not reload on alarm
 * timer 1 also used to calculate vehicle speed via measurement of time between GPIO interrupts 
 */
void timer_setup(int timer_idx,bool auto_reload, double timer_interval_sec)
{
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = auto_reload;
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x0);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, 
        (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, timer_idx);
}

// Event Loop Handler
esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "event_handler:SYSTEM_EVENT_STA_GOT_IP!");
            ESP_LOGI(TAG, "got ip:%s\n",
                     ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            // IP is availiable, start the UDP server
            udp_server();
            break;
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR " join,AID=%d\n",
                     MAC2STR(event->event_info.sta_connected.mac),
                     event->event_info.sta_connected.aid);
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR "leave,AID=%d\n",
                     MAC2STR(event->event_info.sta_disconnected.mac),
                     event->event_info.sta_disconnected.aid);
            break;
        default:
            break;
    }
    return ESP_OK;
}

void wifi_config () {
    // Connect to the AP in STA mode
    tcpip_adapter_init();
    
    // Set Event Handler
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t sta_config = { };
    strcpy((char*)sta_config.sta.ssid, ssid);
    strcpy((char*)sta_config.sta.password, password);
    sta_config.sta.bssid_set = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s \n",
             ssid,password);    
}
/*****************************************************/

#endif