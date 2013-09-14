
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "main.h"
#include "conf.h"
#include "net.h"
#include "log.h"
#include "forwardings.h"
#ifdef FWD_NATPMP
#include "natpmp.h"
#endif
#ifdef FWD_UPNP
#include "upnp.h"
#endif


struct forwarding_t {
	int port; /* the port to be forwarded on the router */
	time_t lifetime; /* keep entry until lifetime expires */
	time_t refreshed; /* last time the entry was refreshed */
	struct forwarding_t *next;
};

struct forwardings_t {
	time_t retry;
	struct forwarding_t *beg;
	struct forwarding_t *cur;
};

#ifdef FWD_NATPMP
struct natpmp_handle_t *natpmp = NULL;
#endif

#ifdef FWD_UPNP
struct upnp_handle_t *upnp = NULL;
#endif

struct forwardings_t forwardings = { .retry = 0, .beg = NULL, .cur = NULL };

int forwardings_count( void ) {
	struct forwarding_t *item;
	int count;

	count = 0;
	item = forwardings.beg;
	while( item ) {
		item = item->next;
		count++;
	}

	return count;
}

void forwardings_debug( int fd ) {
	struct forwarding_t *item;
	time_t refreshed;
	time_t lifetime;
	time_t now;

	now = gstate->time_now.tv_sec;
	item = forwardings.beg;
	while( item ) {
		refreshed = (now - item->refreshed) / 60;
		lifetime = (item->lifetime -  now) / 60;
		dprintf( fd, " port: %hu, refreshed: %ld min. ago, lifetime: %ld min. remaining\n",
			item->port, refreshed, (lifetime == LONG_MAX) ? -1 : lifetime );
		item = item->next;
	}
}

void forwardings_add( USHORT port, time_t lifetime ) {
	struct forwarding_t *cur;
	struct forwarding_t *new;

	if( port <= 1 ) {
		return;
	}

	cur = forwardings.beg;
	while( cur ) {
		if( cur->port == port ) {
			cur->lifetime = lifetime;
			return;
		}
		cur = cur->next;
	}

	new = (struct forwarding_t*) malloc( sizeof(struct forwarding_t) );
	new->port = port;
	new->lifetime = lifetime;
	new->refreshed = 0;
	new->next = forwardings.beg;

	forwardings.beg = new;
	forwardings.retry = 0; /* Trigger quick handling */
}

/* Remove a port from the list - internal use only */
void forwardings_remove( struct forwarding_t *item ) {
	struct forwarding_t *pre;
	struct forwarding_t *cur;

	if( forwardings.cur == item ) {
		forwardings.cur = NULL;
	}

	pre = NULL;
	cur = forwardings.beg;
	while( cur ) {
		if( cur == item ) {
			if( pre ) {
				pre->next = cur->next;
			} else {
				forwardings.beg = cur->next;
			}
			free( cur );
			return;
		}
		pre = cur;
		cur = cur->next;
	}
}

/*
* Try to add a port forwarding to a router.
* We do not actually check if we are in a private network.
* This function is called in intervals.
*/
void forwardings_handle( int __rc, int __sock ) {
	struct forwarding_t *item;
	int rc;
	time_t lifespan;
	time_t now;

	now = gstate->time_now.tv_sec;
	item = forwardings.cur;

	/* Handle current forwarding entry or wait 60 seconds to select a new one to process */
	if( item == NULL ) {
		if( forwardings.retry > now ) {
			return;
		} else {
			item = forwardings.beg;
			forwardings.retry = now + (1 * 60);
		}
	}

	while( item ) {
		if( (item->refreshed + (30 * 60)) < now ) {
			break;
		}
		item = item->next;
	}

	if( item == NULL ) {
		forwardings.cur = NULL;
		return;
	} else {
		forwardings.cur = item;
	}

	if( item->lifetime < now ) {
		lifespan = 0;
	} else {
		lifespan = (32 * 60);
	}

#ifdef FWD_NATPMP
	if( natpmp ) {
		rc = natpmp_handler( natpmp, item->port, lifespan, now );

		if( rc == PF_DONE ) {
			if( lifespan == 0 ) {
				log_debug( "FWD: Remove NAT-PMP forwarding for port %hu.", item->port );
				forwardings_remove( item );
			} else {
				log_debug( "FWD: Add NAT-PMP forwarding for port %hu.", item->port );
				item->refreshed = now;
			}
			return;
		} else if( rc == PF_ERROR ) {
			log_info("FWD: Disable NAT-PMP - not available.");
			natpmp_uninit( &natpmp );
		} else if( rc == PF_RETRY ) {
			//return;
		} else {
			log_err( "FWD: Unhandled NAT-PMP reply." );
		}
	} else {
		natpmp_init( &natpmp );
	}
#endif

#ifdef FWD_UPNP
	if( upnp ) {
		rc = upnp_handler( upnp, item->port, lifespan, now );

		if( rc == PF_DONE ) {
			if( lifespan == 0 ) {
				log_debug( "FWD: Remove UPnP forwarding for port %hu.", item->port );
				forwardings_remove( item );
			} else {
				log_debug( "FWD: Add UPnP forwarding for port %hu.", item->port );
				item->refreshed = now;
			}
			return;
		} else if( rc == PF_ERROR ) {
			log_info("FWD: Disable UPnP - not available.");
			upnp_uninit( &upnp );
		} else if( rc == PF_RETRY ) {
			//return;
		} else {
			log_err( "FWD: Unhandled UPnP reply." );
		}
	} else {
		upnp_init( &upnp );
	}
#endif
}

void forwardings_setup( void ) {
#ifdef FWD_NATPMP
	log_info("FWD: Enable NAT-PMP");
	natpmp_init( &natpmp );
#endif
#ifdef FWD_UPNP
	log_info("FWD: Enable UPnP");
	upnp_init( &upnp );
#endif

	/* Add a port forwarding for the DHT for the entire run time */
	USHORT port = atoi( gstate->dht_port );
	forwardings_add( port, LONG_MAX );

	/* Cause the callback to be called in intervals */
	net_add_handler( -1, &forwardings_handle );
}