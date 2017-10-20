/*
 * Copyright (c) 2015, Yanzi Networks AB.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \addtogroup oma-lwm2m
 * @{
 *
 */

/**
 * \file
 *         Implementation of the Contiki OMA LWM2M security
 * \author
 *         Joakim Eriksson <joakime@sics.se>
 *         Niclas Finne <nfi@sics.se>
 */

#include <stdint.h>
#include <string.h>
#include "lwm2m-object.h"
#include "lwm2m-engine.h"
#include "lwm2m-server.h"
#include "lwm2m-security.h"
#include "coap-keystore.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTS(l,s,f) do { int i;					\
    for(i = 0; i < l; i++) printf(f, s[i]); \
    } while(0)
#define PRINTPRE(p,l,s) do { PRINTF(p);PRINTS(l,s,"%c"); } while(0);
#define PRINTEP(ep) coap_endpoint_print(ep)
#else
#define PRINTF(...)
#define PRINTS(l,s,f)
#define PRINTPRE(p,l,s);
#define PRINTEP(ep)
#endif

#define MAX_COUNT LWM2M_SERVER_MAX_COUNT

static lwm2m_status_t lwm2m_callback(lwm2m_object_instance_t *object,
                                     lwm2m_context_t *ctx);

static lwm2m_object_instance_t *get_by_id(uint16_t instance_id,
                                          lwm2m_status_t *status);

static const lwm2m_resource_id_t resources[] = {
  LWM2M_SECURITY_SERVER_URI_ID, LWM2M_SECURITY_BOOTSTRAP_SERVER_ID,
  LWM2M_SECURITY_MODE_ID, LWM2M_SECURITY_CLIENT_PKI_ID,
  LWM2M_SECURITY_SERVER_PKI_ID, LWM2M_SECURITY_KEY_ID,
  LWM2M_SECURITY_SHORT_SERVER_ID
};

LIST(instances_list);
static lwm2m_security_value_t instances[MAX_COUNT];
/*---------------------------------------------------------------------------*/
static lwm2m_object_instance_t *
create_instance(uint16_t instance_id, lwm2m_status_t *status)
{
  lwm2m_object_instance_t *instance;
  int i;

  instance = get_by_id(instance_id, NULL);
  if(instance != NULL) {
    /* An instance with this id is already registered */
    if(status) {
      *status = LWM2M_STATUS_OPERATION_NOT_ALLOWED;
    }
    return NULL;
  }

  for(i = 0; i < MAX_COUNT; i++) {
    if(instances[i].instance.instance_id == LWM2M_OBJECT_INSTANCE_NONE) {
      memset(&instances[i], 0, sizeof(instances[i]));
      instances[i].instance.callback = lwm2m_callback;
      instances[i].instance.object_id = LWM2M_OBJECT_SECURITY_ID;
      instances[i].instance.instance_id = instance_id;
      instances[i].instance.resource_ids = resources;
      instances[i].instance.resource_count =
        sizeof(resources) / sizeof(lwm2m_resource_id_t);
      list_add(instances_list, &instances[i].instance);

      PRINTF("SEC: Create new security instance\n");
      return &instances[i].instance;
    }
  }

  if(status) {
    *status = LWM2M_STATUS_SERVICE_UNAVAILABLE;
  }

  return NULL;
}
/*---------------------------------------------------------------------------*/
static int
delete_instance(uint16_t instance_id, lwm2m_status_t *status)
{
  lwm2m_object_instance_t *instance;

  if(instance_id == LWM2M_OBJECT_INSTANCE_NONE) {
    /* Remove all instances */
    while((instance = list_pop(instances_list)) != NULL) {
      instance->instance_id = LWM2M_OBJECT_INSTANCE_NONE;
    }
    return 1;
  }

  instance = get_by_id(instance_id, NULL);
  if(instance != NULL) {
    instance->instance_id = LWM2M_OBJECT_INSTANCE_NONE;
    list_remove(instances_list, instance);
    return 1;
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
static lwm2m_object_instance_t *
get_first(lwm2m_status_t *status)
{
  return list_head(instances_list);
}
/*---------------------------------------------------------------------------*/
static lwm2m_object_instance_t *
get_next(lwm2m_object_instance_t *instance, lwm2m_status_t *status)
{
  return instance == NULL ? NULL : instance->next;
}
/*---------------------------------------------------------------------------*/
static lwm2m_object_instance_t *
get_by_id(uint16_t instance_id, lwm2m_status_t *status)
{
  lwm2m_object_instance_t *instance;
  for(instance = list_head(instances_list);
      instance != NULL;
      instance = instance->next) {
    if(instance->instance_id == instance_id) {
      return instance;
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
static lwm2m_status_t
lwm2m_callback(lwm2m_object_instance_t *object,
               lwm2m_context_t *ctx)
{
  /* NOTE: the create operation will only create an instance and should
     avoid reading out data */
  int32_t value;
  int iv;
  lwm2m_security_value_t *security;
  security = (lwm2m_security_value_t *) object;

  if(ctx->operation == LWM2M_OP_WRITE) {
    /* Handle the writes */
    switch(ctx->resource_id) {
    case LWM2M_SECURITY_SERVER_URI_ID:
      PRINTF("Writing security URI value: len: %d\n", (int)ctx->inbuf->size);
      value = lwm2m_object_read_string(ctx, ctx->inbuf->buffer, ctx->inbuf->size, security->server_uri, URI_SIZE);
      /* This is string... */
      security->server_uri_len = ctx->last_value_len;
      break;
    case LWM2M_SECURITY_BOOTSTRAP_SERVER_ID:
      value = lwm2m_object_read_boolean(ctx, ctx->inbuf->buffer, ctx->inbuf->size, &iv);
      if(value > 0) {
        PRINTF("Set Bootstrap: %d\n", iv);
        security->bootstrap = (uint8_t) iv;
      } else {
        PRINTF("Failed to set bootstrap\n");
      }
      break;
    case LWM2M_SECURITY_MODE_ID:
      {
        int32_t v2;
        value = lwm2m_object_read_int(ctx, ctx->inbuf->buffer, ctx->inbuf->size, &v2);
        PRINTF("Writing security MODE value: %d len: %d\n", v2,
               (int)ctx->inbuf->size);
        security->security_mode = v2;
      }
      break;
    case LWM2M_SECURITY_CLIENT_PKI_ID:
      value = lwm2m_object_read_string(ctx, ctx->inbuf->buffer, ctx->inbuf->size, security->public_key, KEY_SIZE);
      security->public_key_len = ctx->last_value_len;

      PRINTF("Writing client PKI: len: %d '", (int)ctx->last_value_len);
      PRINTS(ctx->last_value_len, security->public_key, "%c");
      PRINTF("'\n");
      break;
    case LWM2M_SECURITY_KEY_ID:
      value = lwm2m_object_read_string(ctx, ctx->inbuf->buffer, ctx->inbuf->size, security->secret_key, URI_SIZE);
      security->secret_key_len = ctx->last_value_len;

      PRINTF("Writing secret key: len: %d '", (int)ctx->last_value_len);
      PRINTS(ctx->last_value_len, security->secret_key, "%c");
      PRINTF("'\n");

      break;
    }
  } else if(ctx->operation == LWM2M_OP_READ) {
    switch(ctx->resource_id) {
    case LWM2M_SECURITY_SERVER_URI_ID:
      lwm2m_object_write_string(ctx, (const char *) security->server_uri,
                                security->server_uri_len);
      break;
    default:
      return LWM2M_STATUS_ERROR;
    }
  }
  return LWM2M_STATUS_OK;
}

/*---------------------------------------------------------------------------*/
lwm2m_security_value_t *
lwm2m_security_get_first(void)
{
  return list_head(instances_list);
}
/*---------------------------------------------------------------------------*/
lwm2m_security_value_t *
lwm2m_security_get_next(lwm2m_security_value_t *last)
{
  return last == NULL ? NULL : (lwm2m_security_value_t *)last->instance.next;
}
/*---------------------------------------------------------------------------*/
static const lwm2m_object_impl_t impl = {
  .object_id = LWM2M_OBJECT_SECURITY_ID,
  .get_first = get_first,
  .get_next = get_next,
  .get_by_id = get_by_id,
  .create_instance = create_instance,
  .delete_instance = delete_instance,
};
static lwm2m_object_t reg_object = {
  .impl = &impl,
};
/*---------------------------------------------------------------------------*/
#if WITH_DTLS
static int
get_psk_info(const coap_endpoint_t *address_info,
             coap_keystore_psk_entry_t *info)
{
  /* Find matching security object based on address */
  lwm2m_security_value_t *e;
  coap_endpoint_t ep;

  if(info == NULL || address_info == NULL) {
    return 0;
  }

  for(e = lwm2m_security_get_first();
      e != NULL;
      e = lwm2m_security_get_next(e)) {
    if(e->server_uri_len == 0) {
      continue;
    }
    if(e->security_mode != LWM2M_SECURITY_MODE_PSK) {
      /* Only PSK supported for now */
      continue;
    }
    if(!coap_endpoint_parse((char *)e->server_uri, e->server_uri_len, &ep)) {
      /* Failed to parse URI to endpoint */
      PRINTF("lwm2m-sec: failed to parse server URI ");
      PRINTS(e->server_uri_len, e->server_uri, "%c");
      PRINTF("\n");
      continue;
    }
    if(!coap_endpoint_cmp(address_info, &ep)) {
      /* Wrong server */
      PRINTF("lwm2m-sec: wrong server ");
      PRINTEP(address_info);
      PRINTF(" != ");
      PRINTEP(&ep);
      PRINTF("\n");
      continue;
    }
    if(info->identity_len > 0 && info->identity != NULL) {
      /* Searching for a specific identity */
      if(info->identity_len != e->public_key_len ||
         memcmp(info->identity, e->public_key, info->identity_len)) {
        /* Identity not matching */
        PRINTF("lwm2m-sec: identity not matching\n");
        continue;
      }
    }
    /* Found security information for this server */
    PRINTF("lwm2m-sec: found security match!\n");
    break;
  }

  if(e == NULL) {
    /* No matching security object found */
    return 0;
  }

  if(info->identity == NULL || info->identity_len == 0) {
    /* Identity requested */
    info->identity = e->public_key;
    info->identity_len = e->public_key_len;
    return 1;
  }

  if(e->secret_key_len == 0) {
    /* No secret key / password */
    return 0;
  }

  info->key = e->secret_key;
  info->key_len = e->secret_key_len;
  return 1;
}
#endif /* WITH_DTLS */
/*---------------------------------------------------------------------------*/
#if WITH_DTLS
static const coap_keystore_t key_store = {
  .coap_get_psk_info = get_psk_info
};
#endif /* WITH_DTLS */
/*---------------------------------------------------------------------------*/
void
lwm2m_security_init(void)
{
  int i;

  PRINTF("*** Init lwm2m-security\n");

  list_init(instances_list);

  for(i = 0; i < MAX_COUNT; i++) {
    instances[i].instance.instance_id = LWM2M_OBJECT_INSTANCE_NONE;
  }
  if(lwm2m_engine_add_generic_object(&reg_object)) {
#if WITH_DTLS
    /* Security object handler added - register keystore */
    coap_set_keystore(&key_store);
#endif /* WITH_DTLS */
  }
}
/*---------------------------------------------------------------------------*/
/** @} */
