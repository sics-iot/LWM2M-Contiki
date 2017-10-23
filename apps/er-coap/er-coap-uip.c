/*
 * Copyright (c) 2016, SICS, Swedish ICT AB.
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 *         CoAP transport implementation for uIPv6
 * \author
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "contiki.h"
#include "sys/cc.h"
#include "net/ip/uip-udp-packet.h"
#include "net/ip/uiplib.h"
#include "er-coap.h"
#include "er-coap-engine.h"
#include "er-coap-endpoint.h"
#include "er-coap-transport.h"
#include "er-coap-transactions.h"
#include "er-coap-constants.h"
#include "coap-keystore.h"


#if UIP_CONF_IPV6_RPL
#include "net/rpl/rpl.h"
#endif /* UIP_CONF_IPV6_RPL */

#define DEBUG DEBUG_FULL
#include "net/ip/uip-debug.h"

#if DEBUG
#define PRINTEP(X) coap_endpoint_print(X)
#else
#define PRINTEP(X)
#endif

#if WITH_DTLS
#include "tinydtls.h"
#include "dtls.h"
#include "dtls_debug.h"
#endif /* WITH_DTLS */


/* sanity check for configured values */
#if COAP_MAX_PACKET_SIZE > (UIP_BUFSIZE - UIP_IPH_LEN - UIP_UDPH_LEN)
#error "UIP_CONF_BUFFER_SIZE too small for REST_MAX_CHUNK_SIZE"
#endif

#define SERVER_LISTEN_PORT        UIP_HTONS(COAP_DEFAULT_PORT)
#define SERVER_LISTEN_SECURE_PORT UIP_HTONS(COAP_DEFAULT_SECURE_PORT)

/* direct access into the buffer */
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#if NETSTACK_CONF_WITH_IPV6
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#else
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[UIP_LLH_LEN + UIP_IPH_LEN])
#endif

#if WITH_DTLS
#define PSK_DEFAULT_IDENTITY "Client_identity"
#define PSK_DEFAULT_KEY      "secretPSK"

static dtls_handler_t cb;
static dtls_context_t *dtls_context = NULL;

static const coap_keystore_t *dtls_keystore = NULL;
static struct uip_udp_conn *dtls_conn = NULL;
#endif /* WITH_DTLS */

PROCESS(coap_engine, "CoAP Engine");

static struct uip_udp_conn *udp_conn = NULL;

/*---------------------------------------------------------------------------*/
void
coap_endpoint_print(const coap_endpoint_t *ep)
{
  if(ep->secure) {
    printf("coaps:");
  } else {
    printf("coap:");
  }
  printf("//[");
  uip_debug_ipaddr_print(&ep->ipaddr);
  printf("]:%u", uip_ntohs(ep->port));
}
/*---------------------------------------------------------------------------*/
void
coap_endpoint_copy(coap_endpoint_t *destination,
                   const coap_endpoint_t *from)
{
  uip_ipaddr_copy(&destination->ipaddr, &from->ipaddr);
  destination->port = from->port;
  destination->secure = from->secure;

  PRINTF("EP copy: from sec:%d to sec:%d\n", from->secure,
         destination->secure);
}
/*---------------------------------------------------------------------------*/
int
coap_endpoint_cmp(const coap_endpoint_t *e1, const coap_endpoint_t *e2)
{
  if(!uip_ipaddr_cmp(&e1->ipaddr, &e2->ipaddr)) {
    return 0;
  }
  return e1->port == e2->port && e1->secure == e2->secure;
}
/*---------------------------------------------------------------------------*/
static int
index_of(const char *data, int offset, int len, uint8_t c)
{
  if(offset < 0) {
    return offset;
  }
  for(; offset < len; offset++) {
    if(data[offset] == c) {
      return offset;
    }
  }
  return -1;
}
/*---------------------------------------------------------------------------*/
static int
get_port(const char *inbuf, size_t len, uint32_t *value)
{
  int i;
  *value = 0;
  for(i = 0; i < len; i++) {
    if(inbuf[i] >= '0' && inbuf[i] <= '9') {
      *value = *value * 10 + (inbuf[i] - '0');
    } else {
      break;
    }
  }
  return i;
}
/*---------------------------------------------------------------------------*/
int
coap_endpoint_parse(const char *text, size_t size, coap_endpoint_t *ep)
{
  /* Only IPv6 supported */
  int start = index_of(text, 0, size, '[');
  int end = index_of(text, start, size, ']');
  int secure = strncmp((const char *)text, "coaps:", 6) == 0;
  uint32_t port;
  if(start > 0 && end > start &&
     uiplib_ipaddrconv((const char *)&text[start], &ep->ipaddr)) {
    if(text[end + 1] == ':' &&
       get_port(text + end + 2, size - end - 2, &port)) {
      ep->port = UIP_HTONS(port);
    } else if(secure) {
      /**
       * Secure CoAP should use a different port but for now
       * the same port is used.
       */
      PRINTF("Using secure port (coaps)\n");
      ep->port = SERVER_LISTEN_SECURE_PORT;
      ep->secure = 1;
    } else {
      ep->port = SERVER_LISTEN_PORT;
      ep->secure = 0;
    }
    return 1;
  } else {
    if(uiplib_ipaddrconv((const char *)&text, &ep->ipaddr)) {
      ep->port = SERVER_LISTEN_PORT;
      ep->secure = 0;
      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
static const coap_endpoint_t *
get_src_endpoint(uint8_t secure)
{
  static coap_endpoint_t src;
  uip_ipaddr_copy(&src.ipaddr, &UIP_IP_BUF->srcipaddr);
  src.port = UIP_UDP_BUF->srcport;
  src.secure = secure;
  return &src;
}
/*---------------------------------------------------------------------------*/
int
coap_endpoint_is_secure(const coap_endpoint_t *ep)
{
  return ep->secure;
}
/*---------------------------------------------------------------------------*/
int
coap_endpoint_is_connected(const coap_endpoint_t *ep)
{
#if UIP_CONF_IPV6_RPL
  if(rpl_get_any_dag() == NULL) {
    return 0;
  }
#endif /* UIP_CONF_IPV6_RPL */

#if WITH_DTLS
  if(ep != NULL && ep->secure != 0 && dtls_context != NULL) {
    dtls_peer_t *peer;
    peer = dtls_get_peer(dtls_context, ep);
    if(peer != NULL) {
      /* only if handshake is done! */
      PRINTF("peer state for ");
      PRINTEP(ep);
      PRINTF(" is %d %d\n", peer->state, dtls_peer_is_connected(peer));
      return dtls_peer_is_connected(peer);
    } else {
      PRINTF("Did not find peer ");
      PRINTEP(ep);
      PRINTF("\n");
    }
  }
#endif /* WITH_DTLS */

  /* Assume connected */
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_endpoint_connect(coap_endpoint_t *ep)
{
  if(ep->secure == 0) {
    PRINTF("Connect - Non secure EP:");
    PRINTEP(ep);
    PRINTF("\n");
    return 1;
  }

#if WITH_DTLS
  PRINTF("Connect - DTLS EP:");
  PRINTEP(ep);
  PRINTF(" len:%d\n", sizeof(ep));

  /* setup all address info here... should be done to connect */
  if(dtls_context) {
    dtls_connect(dtls_context, ep);
  }
#endif /* WITH_DTLS */

  return 1;
}
/*---------------------------------------------------------------------------*/
void
coap_endpoint_disconnect(coap_endpoint_t *ep)
{
#if WITH_DTLS
  if(ep && ep->secure && dtls_context) {
    dtls_close(dtls_context, ep);
  }
#endif /* WITH_DTLS */
}
/*---------------------------------------------------------------------------*/
uint8_t *
coap_databuf(void)
{
  return uip_appdata;
}
/*---------------------------------------------------------------------------*/
uint16_t
coap_datalen()
{
  return uip_datalen();
}
/*---------------------------------------------------------------------------*/
void
coap_transport_init(void)
{
  process_start(&coap_engine, NULL);
#if WITH_DTLS
  dtls_init();
  dtls_set_log_level(8);
#endif

}
/*---------------------------------------------------------------------------*/
#if WITH_DTLS
static void
process_secure_data(void)
{
  PRINTF("receiving secure UDP datagram from [");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("]:%u\n  Length: %u\n", uip_ntohs(UIP_UDP_BUF->srcport),
         uip_datalen());

  if(dtls_context) {
    dtls_handle_message(dtls_context, (coap_endpoint_t *)get_src_endpoint(1),
                        uip_appdata, uip_datalen());
  }
}
#endif /* WITH_DTLS */
/*---------------------------------------------------------------------------*/
static void
process_data(void)
{
  PRINTF("receiving UDP datagram from [");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("]:%u\n  Length: %u\n", uip_ntohs(UIP_UDP_BUF->srcport),
         uip_datalen());

  coap_receive(get_src_endpoint(0), uip_appdata, uip_datalen());
}
/*---------------------------------------------------------------------------*/
void
coap_send_message(const coap_endpoint_t *ep, const uint8_t *data,
                  uint16_t length)
{
  if(ep == NULL) {
    PRINTF("failed to send - no endpoint\n");
    return;
  }

#if WITH_DTLS
  if(coap_endpoint_is_secure(ep)) {
    if(dtls_context) {
      dtls_write(dtls_context, (session_t *)ep, (uint8_t *)data, length);
      PRINTF("-sent secure UDP datagram (%u)-\n", length);
    }
    return;
  }
#endif /* WITH_DTLS */

  uip_udp_packet_sendto(udp_conn, data, length, &ep->ipaddr, ep->port);
  PRINTF("-sent UDP datagram (%u)-\n", length);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(coap_engine, ev, data)
{
  PROCESS_BEGIN();

  /* new connection with remote host */
  udp_conn = udp_new(NULL, 0, NULL);
  udp_bind(udp_conn, SERVER_LISTEN_PORT);
  PRINTF("Listening on port %u\n", uip_ntohs(udp_conn->lport));

#if WITH_DTLS
  /* create new context with app-data */
  dtls_conn = udp_new(NULL, 0, NULL);
  if(dtls_conn != NULL) {
    udp_bind(dtls_conn, SERVER_LISTEN_SECURE_PORT);
    PRINTF("DTLS listening on port %u\n", uip_ntohs(dtls_conn->lport));
    dtls_context = dtls_new_context(dtls_conn);
  }
  if(!dtls_context) {
    PRINTF("DTLS: cannot create context\n");
  } else {
    dtls_set_handler(dtls_context, &cb);
  }
#endif /* WITH_DTLS */

  while(1) {
    PROCESS_YIELD();

    if(ev == tcpip_event) {
      if(uip_newdata()) {
#if WITH_DTLS
        if(uip_udp_conn == dtls_conn) {
          process_secure_data();
          continue;
        }
#endif /* WITH_DTLS */
        process_data();
      }
    }
  } /* while (1) */

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

/* DTLS */
#if WITH_DTLS

/* This is input coming from the DTLS code - e.g. de-crypted input from
   the other side - peer */
static int
input_from_peer(struct dtls_context_t *ctx,
                session_t *session, uint8_t *data, size_t len)
{
  size_t i;
  dtls_peer_t *peer;

  PRINTF("received DTLS data:");
  for (i = 0; i < len; i++) {
    PRINTF("%c", data[i]);
  }
  PRINTF("\nHex:");
  for (i = 0; i < len; i++) {
    PRINTF("%02x", data[i]);
  }
  PRINTF("\n");

  peer = dtls_get_peer(ctx, session);
  /* If we have a peer then ensure that the endpoint is tagged as secure */
  if(peer) {
    session->secure = 1;
  }

  coap_receive(session, data, len);

  return 0;
}

/* This is output from the DTLS code to be sent to peer (encrypted) */
static int
output_to_peer(struct dtls_context_t *ctx,
               session_t *session, uint8_t *data, size_t len)
{
  struct uip_udp_conn *udp_connection = dtls_get_app_data(ctx);
  PRINTF("output_to DTLS peer [");
  PRINT6ADDR(&session->ipaddr);
  PRINTF("]:%u len:%d\n", session->port,(int)len);
  uip_udp_packet_sendto(udp_connection, data, len,
                        &session->ipaddr, session->port);
  return len;
}

/* This defines the key-store set API since we hookup DTLS here */
void
coap_set_keystore(const coap_keystore_t *keystore)
{
  dtls_keystore = keystore;
}

#if defined(PSK_DEFAULT_IDENTITY) && defined(PSK_DEFAULT_KEY)
static int
get_default_psk_info(const coap_endpoint_t *address_info,
                     coap_keystore_psk_entry_t *info)
{
  if(info != NULL) {
    if(info->identity == NULL || info->identity_len == 0) {
      /* Identity requested */
      info->identity = (uint8_t *)PSK_DEFAULT_IDENTITY;
      info->identity_len = strlen(PSK_DEFAULT_IDENTITY);
      return 1;
    }
    if(info->identity_len != strlen(PSK_DEFAULT_IDENTITY) ||
       memcmp(info->identity, PSK_DEFAULT_IDENTITY, info->identity_len) != 0) {
      /* Identity not matching */
      return 0;
    }
    info->key = (uint8_t *)PSK_DEFAULT_KEY;
    info->key_len = strlen(PSK_DEFAULT_KEY);
    return 1;
  }
  return 0;
}
static const coap_keystore_t default_key_store = {
  .coap_get_psk_info = get_default_psk_info
};
#endif /* defined(PSK_DEFAULT_IDENTITY) && defined(PSK_DEFAULT_KEY) */

/* This function is the "key store" for tinyDTLS. It is called to
 * retrieve a key for the given identity within this particular
 * session. */
static int
get_psk_info(struct dtls_context_t *ctx,
             const session_t *session,
             dtls_credentials_type_t type,
             const unsigned char *id, size_t id_len,
             unsigned char *result, size_t result_length)
{
  const coap_keystore_t *keystore;
  coap_keystore_psk_entry_t ks;

  keystore = dtls_keystore;

#if defined(PSK_DEFAULT_IDENTITY) && defined(PSK_DEFAULT_KEY)
  if(keystore == NULL) {
    keystore = &default_key_store;
  }
#endif /* defined(PSK_DEFAULT_IDENTITY) && defined(PSK_DEFAULT_KEY) */

  if(keystore == NULL) {
    PRINTF("--- No key store available ---\n");
    return 0;
  }

  memset(&ks, 0, sizeof(ks));
  PRINTF("---===>>> Getting the Key or ID <<<===---\n");
  switch(type) {
  case DTLS_PSK_IDENTITY:
    if(id && id_len) {
      ks.identity_hint = id;
      ks.identity_hint_len = id_len;
      PRINTF("got psk_identity_hint: '%.*s'\n", id_len, id);
    }

    if(keystore->coap_get_psk_info) {
      /* we know that session is a coap endpoint */
      keystore->coap_get_psk_info((coap_endpoint_t *)session, &ks);
    }
    if(ks.identity == NULL || ks.identity_len == 0) {
      return 0;
    }

    if(result_length < ks.identity_len) {
      PRINTF("cannot set psk_identity -- buffer too small\n");
      return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    }
    memcpy(result, ks.identity, ks.identity_len);
    return ks.identity_len;

  case DTLS_PSK_KEY:
    if(keystore->coap_get_psk_info) {
      ks.identity = id;
      ks.identity_len = id_len;
      /* we know that session is a coap endpoint */
      keystore->coap_get_psk_info((coap_endpoint_t *)session, &ks);
    }
    if(ks.key == NULL || ks.key_len == 0) {
      PRINTF("PSK for unknown id requested, exiting\n");
      return dtls_alert_fatal_create(DTLS_ALERT_ILLEGAL_PARAMETER);
    }

    if(result_length < ks.key_len) {
      PRINTF("cannot set psk -- buffer too small\n");
      return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    }
    memcpy(result, ks.key, ks.key_len);
    return ks.key_len;

  default:
    PRINTF("unsupported request type: %d\n", type);
  }

  return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
}


static dtls_handler_t cb = {
  .write = output_to_peer,
  .read  = input_from_peer,
  .event = NULL,
#ifdef DTLS_PSK
  .get_psk_info = get_psk_info,
#endif /* DTLS_PSK */
#ifdef DTLS_ECC
  /* .get_ecdsa_key = get_ecdsa_key, */
  /* .verify_ecdsa_key = verify_ecdsa_key */
#endif /* DTLS_ECC */
};

#endif /* WITH_DTLS */

/*---------------------------------------------------------------------------*/
