#include <stdio.h>
#include <rtt_stdio.h>
#include "shell.h"
#include "thread.h"
#include "xtimer.h"
#include <string.h>

#include "msg.h"
#include "net/gnrc.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netreg.h"

#include <at30ts74.h>
#include <mma7660.h>
#include <periph/gpio.h>
#include <periph/i2c.h>

// 1 second, defined in us
#define INTERVAL (1000000U)
//#define INTERVAL (10000000U)

#define BOOST_ENABLE       GPIO_PIN(PA, 27)
#define I2C_RESET          GPIO_PIN(PA, 18)
#define LOW_BATT_INDICATOR GPIO_PIN(PA, 19)

#define PE_ADDR 24
#define PE_IN_REG 0x0
#define PE_OUT_REG 0x1
#define PE_POL_REG 0x2
#define PE_CFG_REG 0x3

// Insole points
#define JP1 (0x1)
#define JP2 (0x1 << 1)
#define JP3 (0x1 << 2)
#define JP4 (0x1 << 3)
#define JP5 (0x1 << 4)
#define JP6 (0x1 << 5)

extern int _netif_config(int argc, char **argv);
extern void send(char *addr_str, char *port_str, char *data, uint16_t datalen);
extern void start_server(char *port_str);
#define MAIN_QUEUE_SIZE     (8)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];
extern void handle_input_line(const shell_command_t *command_list, char *line);

// method to read state of the device and report it out
void monitoring(void)
{
  // var declarations for monitoring
  int low_battery_indicator = 0;
  int8_t acc_x,acc_y,acc_z;
  int32_t temperature;

  int rv;
  uint8_t led_state = 0x00;

  // TODO: get this from some struct
  // 100,000 us = .1s
  uint32_t period = 10000;
  uint8_t duty_cycle = 50;
  uint8_t sleep_amount = 100 - duty_cycle;
  uint8_t to_actuate = (JP2 | JP3);

  // initialize sensors
  // temperature sensor
  at30ts74_t tmp;
  at30ts74_init(&tmp, I2C_0, AT30TS74_ADDR, AT30TS74_12BIT);
  // accelerometer
  mma7660_t acc;
  if (mma7660_init(&acc, I2C_0, MMA7660_ADDR) != 0) {
    printf("Failed to init ACC\n");
  } else {
    printf("Init acc ok\n");
  }
  if (mma7660_set_mode(&acc, 1, 0, 0, 0) != 0) {
    printf("Failed to set mode\n");
  } else {
    printf("Set mode ok\n");
  }
  if (mma7660_config_samplerate(&acc, MMA7660_SR_AM64, MMA7660_SR_AW32, 1) != 0) {
    printf("Failed to config SR\n");
  }

  // initialize timer
  xtimer_ticks32_t last_wakeup = xtimer_now();
  while (1) {
        // read low battery indicator
        low_battery_indicator = gpio_read(LOW_BATT_INDICATOR);

        // read temperature sensor
        rv = at30ts74_read(&tmp, &temperature);
        if (rv != 0 ) {
            printf("Failed to read temp sensor (%d)\n", rv);
        }

        // read accelerometer
        rv = mma7660_read(&acc, &acc_x, &acc_y, &acc_z);
        if (rv != 0 ) {
            printf("Failed to read accelerometer (%d)\n", rv);
        }

        printf("STATUS:\n");
        printf(" LBI: %d\n", low_battery_indicator);
        printf(" Temperature %" PRId32 "\n", temperature);
        printf(" Accel x/y/z %d/%d/%d\n",acc_x, acc_y, acc_z);
        printf("-------\n");

        if (i2c_acquire(I2C_0)) {
            printf("I2C acquire fail\n");
        }

        led_state = to_actuate - led_state;
        rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, led_state);
        printf("Wrote %d bytes (%x)\n", rv, led_state);
        if (i2c_release(I2C_0)) {
            printf("I2C release fail\n");
        }
        sleep_amount = 100 - sleep_amount;
        xtimer_periodic_wakeup(&last_wakeup, sleep_amount * period);
  }
}

// method to read state of the device and report it out
void cbe_demo(void)
{
  // var declarations for monitoring
  int rv;
  uint8_t led_state = 0x01;

  // initialize timer
  xtimer_ticks32_t last_wakeup = xtimer_now();
  while (1) {
        led_state = JP2 | JP3;
        rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, led_state);
        printf("Wrote %d bytes (%x)\n", rv, led_state);
        if (i2c_release(I2C_0)) {
            printf("I2C release fail\n");
        }
        xtimer_periodic_wakeup(&last_wakeup, 10000000);
        led_state = JP4 | JP5;
        rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, led_state);
        printf("Wrote %d bytes (%x)\n", rv, led_state);
        if (i2c_release(I2C_0)) {
            printf("I2C release fail\n");
        }
        xtimer_periodic_wakeup(&last_wakeup, 10000000);
        led_state = JP2 | JP4;
        rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, led_state);
        printf("Wrote %d bytes (%x)\n", rv, led_state);
        if (i2c_release(I2C_0)) {
            printf("I2C release fail\n");
        }
        xtimer_periodic_wakeup(&last_wakeup, 10000000);
        led_state = JP3 | JP5;
        rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, led_state);
        printf("Wrote %d bytes (%x)\n", rv, led_state);
        if (i2c_release(I2C_0)) {
            printf("I2C release fail\n");
        }
        xtimer_periodic_wakeup(&last_wakeup, 10000000);
  }
}


int main(void)
{
    int rv, state;
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);
    printf("Started!\n");

    rv = gpio_init(BOOST_ENABLE, GPIO_OUT);
    if (rv != 0) {
        printf("Could not init PA27 as output (%d)\n", rv);
        return 1;
    }
    printf("Initialized PA27 as OUTPUT\n");
    // TODO: need to fix the BOOST chip enable: we can't use 3V from the hamilton to pull the
    //       ENABLE pin high, becasue the ENABLE pin is only considered HIGH when it is at least
    //       80% of the input to the boost chip, which is going to be ~5V when we are on Witricity power;
    //       Thus, we need to have the hamilton pin enable connecting the ENABLE pin to some higher voltage
    //       source, which will probably be the input
    //rv = gpio_read(BOOST_ENABLE);
    //printf("PA27 state: %d\n", rv);
    //gpio_write(BOOST_ENABLE, 1);
    //printf("Enabled BOOST chip");

    rv = gpio_init(I2C_RESET, GPIO_OUT);
    if (rv != 0) {
        printf("Could not init PA18 as output (%d)\n", rv);
        return 1;
    }
    printf("Initialized PA18 as OUTPUT\n");
    gpio_write(I2C_RESET, 1);
    state = gpio_read(I2C_RESET);
    printf("PA18 state: %d\n", state);

    rv = gpio_init(LOW_BATT_INDICATOR, GPIO_IN);
    if (rv != 0) {
        printf("Could not init PA19 as output (%d)\n", rv);
        return 1;
    }

    // initialize I2C port expander
    if (i2c_acquire(I2C_0)) {
        printf("I2C acquire fail\n");
    }
    if (i2c_init_master(I2C_0, I2C_SPEED_NORMAL)) {
        printf("I2C init fail\n");
    }

    // set the OUTPUT setting to 0
    rv = i2c_write_reg(I2C_0, PE_ADDR, PE_OUT_REG, 0x00);
    printf("Wrote %d bytes (%x)\n", rv, 0x00);
    // configure all pins as OUTPUT
    rv = i2c_write_reg(I2C_0, PE_ADDR, PE_CFG_REG, 0x00);
    printf("Wrote %d bytes \n", rv);

    if (i2c_release(I2C_0)) {
        printf("I2C release fail\n");
    }

    // TODO: put monitoring in one thread, put the actuation in another thread
    //monitoring();
    cbe_demo();

    return 0;
}
