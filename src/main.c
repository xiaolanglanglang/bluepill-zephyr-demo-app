/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <stdio.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/sensor.h>
#include <display/cfb.h>

/* 1000 msec = 1 sec */
#define LED_SLEEP_TIME_MS 1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0 DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN DT_GPIO_PIN(LED0_NODE, gpios)
#if DT_PHA_HAS_CELL(LED0_NODE, gpios, flags)
#define FLAGS DT_GPIO_FLAGS(LED0_NODE, gpios)
#endif
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0 ""
#define PIN 0
#endif

#ifndef FLAGS
#define FLAGS 0
#endif

#define DISPLAY_DRIVER "SSD1306"

void main(void)
{
    struct device *dev;
    bool led_is_on = true;
    int ret = 0;

    dev = device_get_binding(LED0);
    if (dev == NULL)
    {
        return;
    }

    ret = gpio_pin_configure(dev, PIN, GPIO_OUTPUT_ACTIVE | FLAGS);
    if (ret < 0)
    {
        return;
    }

    while (1)
    {
        gpio_pin_set(dev, PIN, (int)led_is_on);
        printk("led status: %d\r\n", led_is_on);
        led_is_on = !led_is_on;
        k_msleep(LED_SLEEP_TIME_MS);
    }
}

static const char *now_str(void)
{
    static char buf[16]; /* ...HH:MM:SS.MMM */
    uint32_t now = k_uptime_get_32();
    unsigned int ms = now % MSEC_PER_SEC;
    unsigned int s;
    unsigned int min;
    unsigned int h;

    now /= MSEC_PER_SEC;
    s = now % 60U;
    now /= 60U;
    min = now % 60U;
    now /= 60U;
    h = now;

    snprintf(buf, sizeof(buf), "%u:%02u:%02u.%03u",
             h, min, s, ms);
    return buf;
}

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

double temperature_value;
double humidity_value;

void dht11(void)
{
    const char *const label = DT_LABEL(DT_INST(0, aosong_dht));
    struct device *dht11 = device_get_binding(label);

    if (!dht11)
    {
        printk("Failed to find sensor %s\n", label);
        return;
    }

    while (true)
    {
        k_sleep(K_SECONDS(2));
        int rc = sensor_sample_fetch(dht11);

        if (rc != 0)
        {
            printk("Sensor fetch failed: %d\n", rc);
            continue;
        }

        struct sensor_value temperature;
        struct sensor_value humidity;

        rc = sensor_channel_get(dht11, SENSOR_CHAN_AMBIENT_TEMP,
                                &temperature);
        if (rc == 0)
        {
            rc = sensor_channel_get(dht11, SENSOR_CHAN_HUMIDITY,
                                    &humidity);
        }
        if (rc != 0)
        {
            printk("get failed: %d\n", rc);
            continue;
        }
        temperature_value = sensor_value_to_double(&temperature);
        humidity_value = sensor_value_to_double(&humidity);

        printf("[%s]: %.1f Cel ; %.1f %%RH\n",
               now_str(),
               temperature_value,
               humidity_value);
    }
}

K_THREAD_DEFINE(dht11_id, STACKSIZE, dht11, NULL, NULL, NULL, PRIORITY, 0, 0);

void display(void)
{
    struct device *dev;
    uint16_t rows;
    uint8_t ppt;
    uint8_t font_width;
    uint8_t font_height;

    dev = device_get_binding(DISPLAY_DRIVER);

    if (dev == NULL)
    {
        printf("Device not found\n");
        return;
    }

    if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO10) != 0)
    {
        printf("Failed to set required pixel format\n");
        return;
    }

    printf("initialized %s\n", DISPLAY_DRIVER);

    if (cfb_framebuffer_init(dev))
    {
        printf("Framebuffer initialization failed!\n");
        return;
    }

    cfb_framebuffer_clear(dev, true);

    display_blanking_off(dev);

    rows = cfb_get_display_parameter(dev, CFB_DISPLAY_ROWS);
    ppt = cfb_get_display_parameter(dev, CFB_DISPLAY_PPT);

    for (int idx = 0; idx < 42; idx++)
    {
        if (cfb_get_font_size(dev, idx, &font_width, &font_height))
        {
            break;
        }
        cfb_framebuffer_set_font(dev, idx);
        printf("font width %d, font height %d\n",
               font_width, font_height);
        break;
    }

    printf("x_res %d, y_res %d, ppt %d, rows %d, cols %d\n",
           cfb_get_display_parameter(dev, CFB_DISPLAY_WIDTH),
           cfb_get_display_parameter(dev, CFB_DISPLAY_HEIGH),
           ppt,
           rows,
           cfb_get_display_parameter(dev, CFB_DISPLAY_COLS));

    static char buf[2][12];
    while (1)
    {
        sprintf(buf[0], "%.1f Cel", temperature_value);
        sprintf(buf[1], "%.1f %%RH", humidity_value);
        cfb_framebuffer_clear(dev, false);
        if (cfb_print(dev, buf[0], 0, 0))
        {
            printf("Failed to print a string\n");
            continue;
        }
        if (cfb_print(dev, buf[1], 0, 1 * font_height))
        {
            printf("Failed to print a string\n");
            continue;
        }
        cfb_framebuffer_finalize(dev);
        k_msleep(1000);
    }
}

K_THREAD_DEFINE(display_id, STACKSIZE, display, NULL, NULL, NULL, PRIORITY, 0, 0);