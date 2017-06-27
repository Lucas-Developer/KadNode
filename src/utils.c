
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include "main.h"
#include "log.h"
#include "conf.h"
#include "sha1.h"
#include "utils.h"


// Also matches on equality
int is_suffix( const char str[], const char suffix[] ) {
	size_t suffix_len;
	size_t str_len;

	suffix_len = strlen( suffix );
	str_len = strlen( str );
	if( suffix_len > str_len ) {
		return 0;
	} else {
		return (memcmp( str + str_len - suffix_len, suffix, suffix_len ) == 0);
	}
}

void* memdup(const void* src, size_t size) {
	void* out = malloc(size);
	memcpy(out, src, size);
	return out;
}

// Remove .p2p suffix and convert to lowercase.
int query_sanitize( char buf[], size_t buflen, const char query[] ) {
	size_t len;
	size_t i;

	len = strlen( query );

	// Remove .p2p suffix
	if( is_suffix( query, gconf->query_tld ) ) {
		len -= strlen( gconf->query_tld );
	}

	// Buffer too small
	if( (len+1) >= buflen ) {
		return 1;
	}

	// Convert to lower case
	for( i = 0; i < len; ++i ) {
		buf[i] = tolower( query[i] );
	}
	buf[len] = '\0';

	return 0;
}

// Create a random port != 0
int port_random( void ) {
	uint16_t port;

	do {
		bytes_random( (uint8_t*) &port, sizeof(port) );
	} while( port == 0 );

	return port;
}

// Parse a port - treats 0 as valid port
int port_parse( const char pstr[], int err ) {
	int port;

	if( sscanf( pstr, "%d", &port ) == 1 && port >= 0 && port <= 65535 ) {
		return port;
	} else {
		return err;
	}
}

int port_set( IP *addr, uint16_t port ) {
	switch( addr->ss_family ) {
		case AF_INET:
			((IP4 *)addr)->sin_port = htons( port );
			return 0;
		case AF_INET6:
			((IP6 *)addr)->sin6_port = htons( port );
			return 0;
		default:
			return 1;
	}
}

// Fill buffer with random bytes
int bytes_random( uint8_t buffer[], size_t size ) {
	int fd;
	int rc;

	fd = open( "/dev/urandom", O_RDONLY );
	if( fd < 0 ) {
		log_err( "Failed to open /dev/urandom" );
		exit( 1 );
	}

	rc = read( fd, buffer, size );

	close( fd );

	return rc;
}

void bytes_from_hex( uint8_t bin[], const char hex[], size_t length ) {
	size_t i;
	size_t xv = 0;

	for( i = 0; i < length; ++i ) {
		const char c = hex[i];
		if( c >= 'a' ) {
			xv += (c - 'a') + 10;
		} else if ( c >= 'A') {
			xv += (c - 'A') + 10;
		} else {
			xv += c - '0';
		}

		if( i % 2 ) {
			bin[i/2] = xv;
			xv = 0;
		} else {
			xv *= 16;
		}
	}
}

char *bytes_to_hex( char hex[], const uint8_t bin[], size_t length ) {
	size_t i;
	uint8_t *p0 = (uint8_t *)bin;
	char *p1 = hex;

	for( i = 0; i < length; i++ ) {
		snprintf( p1, 3, "%02x", *p0 );
		p0 += 1;
		p1 += 2;
	}

	return hex;
}

// TODO: remove?
int id_equal( const uint8_t id1[], const uint8_t id2[] ) {
	return (memcmp( id1, id2, SHA1_BIN_LENGTH ) == 0);
}

// Check if string consist of hexdecimal characters
int str_isHex( const char str[], size_t size ) {
	size_t i = 0;

	for( i = 0; i < size; i++ ) {
		const char c = str[i];
		if( (c >= '0' && c <= '9')
				|| (c >= 'A' && c <= 'F')
				|| (c >= 'a' && c <= 'f') ) {
			continue;
		} else {
			return 0;
		}
	}

	return 1;
}

// Matches [0-9a-zA-Z._-]*
int str_isValidHostname( const char hostname[] ) {
	size_t size;
	size_t i;

	size = strlen( hostname );
	for( i = 0; i < size; i++ ) {
		const char c = hostname[i];
		if( (c >= '0' && c <= '9')
				|| (c >= 'A' && c <= 'Z')
				|| (c >= 'a' && c <= 'z')
				|| (c == '-')
				|| (c == '.')
				|| (c == '_') ) {
			continue;
		} else {
			return 0;
		}
	}

	return 1;
}

int str_isZero( const char str[] ) {
	return (str == NULL) || (strcmp( str, "0" ) == 0);
}

char *str_id( const uint8_t id[] ) {
	static char hexbuf[SHA1_HEX_LENGTH+1];
	return bytes_to_hex( hexbuf, id, SHA1_BIN_LENGTH );
}

char *str_addr( const IP *addr ) {
	static char addrbuf[FULL_ADDSTRLEN + 1];
	char buf[INET6_ADDRSTRLEN + 1];
	uint16_t port;

	switch( addr->ss_family ) {
		case AF_INET6:
			port = ntohs( ((IP6 *)addr)->sin6_port );
			inet_ntop( AF_INET6, &((IP6 *)addr)->sin6_addr, buf, sizeof(buf) );
			sprintf( addrbuf, "[%s]:%hu", buf, port );
			break;
		case AF_INET:
			port = ntohs( ((IP4 *)addr)->sin_port );
			inet_ntop( AF_INET, &((IP4 *)addr)->sin_addr, buf, sizeof(buf) );
			sprintf( addrbuf, "%s:%hu", buf, port );
			break;
		default:
			sprintf( addrbuf, "<invalid address>" );
	}

	return addrbuf;
}

int addr_is_localhost( const IP *addr )
{
	// 127.0.0.1
	unsigned int inaddr_loopback = htonl( INADDR_LOOPBACK );

	switch( addr->ss_family ) {
		case AF_INET:
			return (memcmp( &((IP4 *)addr)->sin_addr, &inaddr_loopback, 4 ) == 0);
		case AF_INET6:
			return (memcmp( &((IP6 *)addr)->sin6_addr, &in6addr_loopback, 16 ) == 0);
		default:
			return 0;
	}
}

int addr_is_multicast( const IP *addr )
{
	switch( addr->ss_family ) {
		case AF_INET:
			return IN_MULTICAST(ntohl(((IP4*) addr)->sin_addr.s_addr));
		case AF_INET6:
			return IN6_IS_ADDR_MULTICAST(&((IP6*) addr)->sin6_addr);
		default:
			return 0;
	}
}

int addr_port( const IP *addr ) {
	switch( addr->ss_family ) {
		case AF_INET:
			return ntohs( ((IP4 *)addr)->sin_port );
		case AF_INET6:
			return ntohs( ((IP6 *)addr)->sin6_port );
		default:
			return 0;
	}
}

int addr_len( const IP *addr ) {
	switch( addr->ss_family ) {
		case AF_INET:
			return sizeof(IP4);
		case AF_INET6:
			return sizeof(IP6);
		default:
			return 0;
	}
}

/*
* Parse/Resolve an IP address.
* The port must be specified separately.
*/
int addr_parse( IP *addr, const char addr_str[], const char port_str[], int af ) {
	struct addrinfo hints;
	struct addrinfo *info = NULL;
	struct addrinfo *p = NULL;

	memset( &hints, '\0', sizeof(struct addrinfo) );
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = af;

	if( getaddrinfo( addr_str, port_str, &hints, &info ) != 0 ) {
		return -2;
	}

	p = info;
	while( p != NULL ) {
		if( p->ai_family == AF_INET6 ) {
			memcpy( addr, p->ai_addr, sizeof(IP6) );
			freeaddrinfo( info );
			return 0;
		}
		if( p->ai_family == AF_INET ) {
			memcpy( addr, p->ai_addr, sizeof(IP4) );
			freeaddrinfo( info );
			return 0;
		}
	}

	freeaddrinfo( info );
	return -3;
}

/*
* Parse/Resolve various string representations of
* IPv4/IPv6 addresses and optional port.
* An address can also be a domain name.
* A port can also be a service  (e.g. 'www').
*
* "<address>"
* "<ipv4_address>:<port>"
* "[<address>]"
* "[<address>]:<port>"
*/
int addr_parse_full( IP *addr, const char full_addr_str[], const char default_port[], int af ) {
	char addr_buf[256];

	char *addr_beg, *addr_tmp;
	char *last_colon;
	const char *addr_str = NULL;
	const char *port_str = NULL;
	size_t len;

	len = strlen( full_addr_str );
	if( len >= (sizeof(addr_buf) - 1) ) {
		// address too long
		return -1;
	} else {
		addr_beg = addr_buf;
	}

	memset( addr_buf, '\0', sizeof(addr_buf) );
	memcpy( addr_buf, full_addr_str, len );

	last_colon = strrchr( addr_buf, ':' );

	if( addr_beg[0] == '[' ) {
		// [<addr>] or [<addr>]:<port>
		addr_tmp = strrchr( addr_beg, ']' );

		if( addr_tmp == NULL ) {
			// broken format
			return -1;
		}

		*addr_tmp = '\0';
		addr_str = addr_beg + 1;

		if( *(addr_tmp+1) == '\0' ) {
			port_str = default_port;
		} else if( *(addr_tmp+1) == ':' ) {
			port_str = addr_tmp + 2;
		} else {
			// port expected
			return -1;
		}
	} else if( last_colon && last_colon == strchr( addr_buf, ':' ) ) {
		// <non-ipv6-addr>:<port>
		addr_tmp = last_colon;
		if( addr_tmp ) {
			*addr_tmp = '\0';
			addr_str = addr_buf;
			port_str = addr_tmp+1;
		} else {
			addr_str = addr_buf;
			port_str = default_port;
		}
	} else {
		// <addr>
		addr_str = addr_buf;
		port_str = default_port;
	}

	return addr_parse( addr, addr_str, port_str, af );
}

// Compare two ip addresses, ignore port
int addr_equal( const IP *addr1, const IP *addr2 ) {
	if( addr1->ss_family != addr2->ss_family ) {
		return 0;
	} else if( addr1->ss_family == AF_INET ) {
		return memcmp( &((IP4 *)addr1)->sin_addr, &((IP4 *)addr2)->sin_addr, 4 ) == 0;
	} else if( addr1->ss_family == AF_INET6 ) {
		return memcmp( &((IP6 *)addr1)->sin6_addr, &((IP6 *)addr2)->sin6_addr, 16 ) == 0;
	} else {
		return 0;
	}
}

time_t time_now_sec( void ) {
	return gconf->time_now;
}

time_t time_add_min( uint32_t minutes ) {
	return time_now_sec() + (60 * minutes);
}

time_t time_add_hour( uint32_t hours ) {
	return time_now_sec() + (60 * 60 * hours);
}
