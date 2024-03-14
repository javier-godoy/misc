/*
 * ldnsd. Light-weight DNS daemon
 *
 * Tiny dns server to show how a real one could be built.
 *
 * (c) NLnet Labs, 2005
 * (c) Roberto Javier Godoy, 2020, 2024
 *
 * Copyright (c) 2005,2006, NLnetLabs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of NLnetLabs nor the names of its
 *     contributors may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "dns_server.h"
#include <pthread.h>

#define INBUF_SIZE 4096

void handle_dns_pkt(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt);
static pthread_mutex_t mutex;

static int udp_bind(int sock, int port, in_addr_t maddr)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = (in_port_t) htons((uint16_t)port);
    addr.sin_addr.s_addr = maddr;
    return bind(sock, (struct sockaddr *)&addr, (socklen_t) sizeof(addr));
}

/* this will probably be moved to a better place in the library itself */
static ldns_rr_list *
get_rrset(const ldns_zone *zone, const ldns_rdf *owner_name, const ldns_rr_type qtype, const ldns_rr_class qclass)
{
    uint16_t i;
    ldns_rr_list *rrlist = ldns_rr_list_new();
    ldns_rr *cur_rr;
    if (!zone || !owner_name) {
        fprintf(stderr, "Warning: get_rrset called with NULL zone or owner name\n");
        return rrlist;
    }
    
    for (i = 0; i < ldns_zone_rr_count(zone); i++) {
        cur_rr = ldns_rr_list_rr(ldns_zone_rrs(zone), i);
        if (ldns_dname_compare(ldns_rr_owner(cur_rr), owner_name) == 0 &&
            (ldns_rr_get_class(cur_rr) == qclass ||  LDNS_RR_CLASS_ANY == qclass) &&
            (ldns_rr_get_type(cur_rr) == qtype || LDNS_RR_TYPE_ANY == qtype)
           ) {
            ldns_rr_list_push_rr(rrlist, ldns_rr_clone(cur_rr));
        }
    }
    
    return rrlist;
}

void start_dns_server(struct in_addr my_address, int port) {

    /* network */
    int sock;
    ssize_t nb;
    struct sockaddr addr_me;
    struct sockaddr addr_him;
    socklen_t hislen = (socklen_t) sizeof(addr_him);
    uint8_t inbuf[INBUF_SIZE];
    uint8_t *outbuf;

    /* dns */
    ldns_status status;
    ldns_pkt *query_pkt;
    ldns_pkt *answer_pkt;
    size_t answer_size;
            
    printf("Listening on port %d\n", port);
    sock =  socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "socket(): %s\n", strerror(errno));
        exit(1);
    }
    memset(&addr_me, 0, sizeof(addr_me));

    /* bind: try all ports in that range */
    if (udp_bind(sock, port, my_address.s_addr)) {
        fprintf(stderr, "cannot bind(): %s\n", strerror(errno));
        exit(errno);
    }

    /* Done. Now receive */
    while (1) {
        nb = recvfrom(sock, (void*)inbuf, INBUF_SIZE, 0, &addr_him, &hislen);
		
        if (nb < 1) {
            fprintf(stderr, "recvfrom(): %s\n",
            strerror(errno));
            exit(1);
        }

        
        
        
        printf("Got query of %u bytes\n", (unsigned int) nb);
        status = ldns_wire2pkt(&query_pkt, inbuf, (size_t) nb);
        if (status != LDNS_STATUS_OK) {
            printf("Got bad packet: %s\n", ldns_get_errorstr_by_id(status));            
            continue;
        } 
		ldns_pkt_print(stdout, query_pkt);
        

        if (ldns_pkt_qr(query_pkt)) {
            printf("Received DNS response\n");
            ldns_pkt_free(query_pkt);
            continue;
        }
        
        answer_pkt = ldns_pkt_new();
        
        ldns_pkt_set_qr(answer_pkt, 1);
        ldns_pkt_set_id(answer_pkt, ldns_pkt_id(query_pkt));
        
        if (ldns_pkt_edns(query_pkt)) {
          ldns_pkt_set_edns_udp_size(answer_pkt, 4096);
          if (ldns_pkt_edns_version(query_pkt)>0) {
            ldns_pkt_set_edns_extended_rcode(answer_pkt, 1);
          }
        }            
        
        if (!ldns_pkt_get_rcode(answer_pkt) && !ldns_pkt_edns_extended_rcode(answer_pkt)) {
          ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_NOTIMPL);
          pthread_mutex_lock(&mutex);
          handle_dns_pkt(query_pkt, answer_pkt);
          pthread_mutex_unlock(&mutex);
        }
        
        status = ldns_pkt2wire(&outbuf, answer_pkt, &answer_size);
        
        
        
        printf("Answer packet size: %u bytes.\n", (unsigned int) answer_size);        
        if (status != LDNS_STATUS_OK) {
            printf("Error creating answer: %s\n", ldns_get_errorstr_by_id(status));
        } else {
            ldns_pkt_print(stdout, answer_pkt);            
            (void) sendto(sock, (void*)outbuf, answer_size, 0, &addr_him, hislen);
        }
        
        ldns_pkt_free(query_pkt);
        ldns_pkt_free(answer_pkt);
        LDNS_FREE(outbuf);        
    }

}

struct adv_origin_struct {
    struct adv_origin_struct* next;
    struct in_addr router;
    uint32_t id;
};

struct dns_struct_entry {
    struct dns_struct_entry* next;
    struct adv_origin_struct *origin;
    ldns_rr *rr;
};

typedef struct dns_struct_entry dns_entry;
typedef struct adv_origin_struct adv_origin;

static dns_entry *database = NULL;


dns_entry* dns_entry_get(ldns_rr* rr) {
    for (dns_entry* e = database; e; e=e->next) {
        if (ldns_rr_compare(e->rr, rr)!=0) continue;
        return e;
    }    
    return NULL;
}

dns_entry* dns_entry_new(ldns_rr* rr) {
    dns_entry* e = malloc(sizeof(dns_entry));
    e->next=database;
    e->rr=rr;
    e->origin=NULL;
    database=e;
}

void dns_entry_add(dns_entry* dns, struct in_addr adv_router, uint32_t adv_id) {
    for (adv_origin* adv = dns->origin; adv; adv=adv->next) {
        if (adv->router.s_addr==adv_router.s_addr && adv->id==adv_id) return;
    }
    
    adv_origin* adv = malloc(sizeof(adv_origin));
    adv->next=dns->origin;
    adv->router=adv_router;
    adv->id=adv_id;    
    dns->origin=adv->next;
}

bool dns_entry_del(dns_entry* dns, struct in_addr adv_router, uint32_t adv_id) {
    for (adv_origin *prev=(adv_origin*)&dns->origin, *curr=prev->next; curr; prev=curr, curr=curr->next) {    
        if (curr->router.s_addr==adv_router.s_addr && curr->id==adv_id) {
            prev->next=curr->next;
            free(curr);            
            return true;
        }
    }
    return false;
}
    
static ldns_zone *_the_zone = NULL;

void dns_learn(struct in_addr router, uint32_t id, ldns_rr_list *rr_list) {
    if (!_the_zone) _the_zone=ldns_zone_new();

    ldns_rr_list2canonical(rr_list);
    
    size_t count = ldns_rr_list_rr_count(rr_list);    
    for (size_t i=0; i<count; ++i) {
        ldns_rr *rr = ldns_rr_list_rr(rr_list, i);
        dns_entry* dns = dns_entry_get(rr);
        if (!dns) {
            pthread_mutex_lock(&mutex);
            ldns_zone *zone = _the_zone;
            ldns_zone_push_rr(zone, rr);
            pthread_mutex_unlock(&mutex);
            
            dns = dns_entry_new(rr);
            rr=NULL;
        }
        dns_entry_add(dns,router,id);
        ldns_rr_free(rr);
    }
    ldns_rr_list_free(rr_list);    
}

void dns_delete(struct in_addr router, uint32_t id) {
  for (dns_entry *prev=(dns_entry*)&database, *curr=prev->next; curr; prev=curr, curr=curr->next) {
    if (dns_entry_del(curr,router,id) && !curr->origin) {
      prev->next=curr->next;

      pthread_mutex_lock(&mutex);
            
      ldns_zone *zone = _the_zone;
      ldns_rr_list* rr_list = ldns_zone_rrs(zone);
      ldns_rr **rrs = rr_list->_rrs;
            
      for (size_t i = 0; i < rr_list->_rr_count; i++, rrs++) {
        if (curr->rr == rr_list->_rrs[i]) {                      
        memmove(rrs, rrs+1, ((rr_list->_rr_count-1)-i) * sizeof(ldns_rr));
        rrs[rr_list->_rr_count-1]=NULL;
        rr_list->_rr_capacity--;                      
          break;
        }
      }
  
      pthread_mutex_unlock(&mutex);
      ldns_rr_free(curr->rr);
      free(curr);
      curr=prev;
    }
  }    
}

void dns_print() {
  if (!_the_zone) return;
  printf("--DNS--\n");
  ldns_zone *zone = _the_zone;
  ldns_rr_print(stdout, ldns_zone_soa(zone));
  ldns_rr_list_print(stdout, ldns_zone_rrs(zone));
}

void handle_dns_pkt(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt) {
  if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_QUERY) {
    ldns_zone *zone = _the_zone;

    ldns_rr_list *answer_qr;
    ldns_rr_list *answer_an;
    ldns_rr_list *answer_ns;
    ldns_rr_list *answer_ad;

    size_t qdcount = ldns_pkt_qdcount(query_pkt);
    if (qdcount!=1) {
      ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_FORMERR);
      return;
    }
    
    ldns_rr *query_rr = ldns_rr_list_rr(ldns_pkt_question(query_pkt), 0);
    
    answer_qr = ldns_rr_list_new();
    ldns_rr_list_push_rr(answer_qr, ldns_rr_clone(query_rr));

    if (zone) {        
      answer_an = get_rrset(zone, ldns_rr_owner(query_rr), ldns_rr_get_type(query_rr), ldns_rr_get_class(query_rr));
    } else {
      ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_REFUSED);
      return;
    }
    answer_ns = ldns_rr_list_new();
    answer_ad = ldns_rr_list_new();

    ldns_pkt_set_aa(answer_pkt, 1);
        
    ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_QUESTION, answer_qr);
    ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_ANSWER, answer_an);
    ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_AUTHORITY, answer_ns);
    ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_ADDITIONAL, answer_ad);
                
    ldns_rr_list_free(answer_qr);
    ldns_rr_list_free(answer_an);
    ldns_rr_list_free(answer_ns);
    ldns_rr_list_free(answer_ad);

    ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_NOERROR);
  }
    
}
