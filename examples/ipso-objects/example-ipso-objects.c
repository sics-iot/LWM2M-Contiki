/*
 * Copyright (c) 2015, Yanzi Networks AB.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/**
 * \file
 *      OMA LWM2M and IPSO Objects example.
 * \author
 *      Joakim Eriksson, joakime@sics.se
 *      Niclas Finne, nfi@sics.se
 */

#include "contiki.h"
#include "lwm2m-engine.h"
#include "lwm2m-rd-client.h"
#include "ipso-objects.h"
#include "ipso-sensor-template.h"
#include "ipso-control-template.h"
#include "lwm2m-server.h"
#include "lwm2m-security.h"
#include "lwm2m-device.h"
#include "dev/leds.h"

#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"

#ifndef REGISTER_WITH_LWM2M_BOOTSTRAP_SERVER
#define REGISTER_WITH_LWM2M_BOOTSTRAP_SERVER 0
#endif

#ifndef REGISTER_WITH_LWM2M_SERVER
#define REGISTER_WITH_LWM2M_SERVER 1
#endif

#if BOARD_SENSORTAG
#include "board-peripherals.h"


/* Temperature reading */
static lwm2m_status_t
read_temp_value(const ipso_sensor_t *s, int32_t *value)
{
  *value = 10 * hdc_1000_sensor.value(HDC_1000_SENSOR_TYPE_TEMP);
  return LWM2M_STATUS_OK;
}
/* Humitidy reading */
static lwm2m_status_t
read_hum_value(const ipso_sensor_t *s, int32_t *value)
{
  *value = 10 * hdc_1000_sensor.value(HDC_1000_SENSOR_TYPE_HUMIDITY);
  return LWM2M_STATUS_OK;
}
/* Lux reading */
static lwm2m_status_t
read_lux_value(const ipso_sensor_t *s, int32_t *value)
{
  *value = 10 * opt_3001_sensor.value(0);
  return LWM2M_STATUS_OK;
}
/* Barometer reading */
static lwm2m_status_t
read_bar_value(const ipso_sensor_t *s, int32_t *value)
{
  *value = 10 * bmp_280_sensor.value(BMP_280_SENSOR_TYPE_PRESS);
  return LWM2M_STATUS_OK;
}
/* LED control */
static lwm2m_status_t
leds_set_val(uint8_t value)
{
  if(value > 0) {
    leds_on(LEDS_YELLOW);
  } else {
    leds_off(LEDS_YELLOW);
  }
  return LWM2M_STATUS_OK;
}
/*---------------------------------------------------------------------------*/
static ipso_sensor_value_t temp_value;
static ipso_sensor_value_t hum_value;
static ipso_sensor_value_t lux_value;
static ipso_sensor_value_t bar_value;

static ipso_control_t led_control = {
   .reg_object.object_id = 3311,
   .reg_object.instance_id = 0,
   .set_value = leds_set_val
};

static const ipso_sensor_t temp_sensor = {
  .object_id = 3303,
  .sensor_value = &temp_value,
  .max_range = 100000, /* 100 cel milli celcius */
  .min_range = -10000, /* -10 cel milli celcius */
  .get_value_in_millis = read_temp_value,
  .unit = "Cel",
  .update_interval = 30
};

static const ipso_sensor_t hum_sensor = {
  .object_id = 3304,
  .sensor_value = &hum_value,
  .max_range = 100000, /* 100 % RH */
  .min_range = 0,
  .get_value_in_millis = read_hum_value,
  .unit = "% RH",
  .update_interval = 30
};
static const ipso_sensor_t lux_sensor = {
  .object_id = 3301,
  .sensor_value = &lux_value,
  .max_range = 100000,
  .min_range = -10000,
  .get_value_in_millis = read_lux_value,
  .unit = "LUX",
  .update_interval = 30
};
static const ipso_sensor_t bar_sensor = {
  .object_id = 3315,
  .sensor_value = &bar_value,
  .max_range = 100000, /* 100 cel milli celcius */
  .min_range = -10000, /* -10 cel milli celcius */
  .get_value_in_millis = read_bar_value,
  .unit = "hPa",
  .update_interval = 30
};

#endif


#ifndef LWM2M_SERVER_ADDRESS
#define LWM2M_SERVER_ADDRESS "coap://[fd02::1]"
#endif

PROCESS(example_ipso_objects, "IPSO object example");
AUTOSTART_PROCESSES(&example_ipso_objects);
/*---------------------------------------------------------------------------*/
static void
setup_lwm2m_servers(void)
{
#ifdef LWM2M_SERVER_ADDRESS
  coap_endpoint_t server_ep;
  if(coap_endpoint_parse(LWM2M_SERVER_ADDRESS, strlen(LWM2M_SERVER_ADDRESS),
                         &server_ep) != 0) {
    lwm2m_rd_client_register_with_bootstrap_server(&server_ep);
    lwm2m_rd_client_register_with_server(&server_ep);
  }
#endif /* LWM2M_SERVER_ADDRESS */

  lwm2m_rd_client_use_bootstrap_server(REGISTER_WITH_LWM2M_BOOTSTRAP_SERVER);
  lwm2m_rd_client_use_registration_server(REGISTER_WITH_LWM2M_SERVER);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_ipso_objects, ev, data)
{
  static struct etimer periodic;
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  PRINTF("Starting IPSO objects example Bootstrap:%d\n",
         REGISTER_WITH_LWM2M_BOOTSTRAP_SERVER);

  /* Initialize the OMA LWM2M engine */
  lwm2m_engine_init();

  /* Register default LWM2M objects */
  lwm2m_device_init();
  lwm2m_security_init();
  lwm2m_server_init();

#if BOARD_SENSORTAG
  ipso_sensor_add(&temp_sensor);
  ipso_sensor_add(&hum_sensor);
  ipso_sensor_add(&lux_sensor);
  ipso_sensor_add(&bar_sensor);
  ipso_control_add(&led_control);
  ipso_button_init();

  SENSORS_ACTIVATE(hdc_1000_sensor);
  SENSORS_ACTIVATE(opt_3001_sensor);
  SENSORS_ACTIVATE(bmp_280_sensor);

#else
  /* Register default IPSO objects - such as button..*/
  ipso_objects_init();
#endif

  setup_lwm2m_servers();
  /* Tick loop each 5 seconds */
  etimer_set(&periodic, CLOCK_SECOND * 5);

  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&periodic)) {
#if BOARD_SENSORTAG
      /* deactive / activate to do a new reading */
      SENSORS_DEACTIVATE(hdc_1000_sensor);
      SENSORS_DEACTIVATE(opt_3001_sensor);
      SENSORS_DEACTIVATE(bmp_280_sensor);

      SENSORS_ACTIVATE(hdc_1000_sensor);
      SENSORS_ACTIVATE(opt_3001_sensor);
      SENSORS_ACTIVATE(bmp_280_sensor);
#endif
      etimer_reset(&periodic);
    }
  }
  PROCESS_END();
}
