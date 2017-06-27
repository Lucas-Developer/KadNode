
#define _WITH_DPRINTF
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/socket.h>

#include "main.h"
#include "conf.h"
#include "utils.h"
#include "log.h"
#include "kad.h"
#include "net.h"
#include "announces.h"
#include "searches.h"
#ifdef AUTH
#include "ext-auth.h"
#endif
#ifdef FWD
#include "ext-fwd.h"
#endif
#include "ext-cmd.h"


static const char* cmd_usage =
	"Usage:\n"
	"	status\n"
	"	lookup <query>\n"
#if 0
	"	lookup_node <id>\n"
#endif
	"	announce [<query>[:<port>] [<minutes>]]\n"
	"	import <addr>\n"
	"	export\n"
	"	blacklist <addr>\n";

const char* cmd_usage_debug =
	"	list [blacklist|buckets|constants|"
#ifdef FWD
	"forwardings|"
#endif
#ifdef AUTH
	"skeys|pkeys|"
#endif
	"results|searches|storage|values]\n";

#define REPLY_DATA_SIZE 1472

// A UDP packet sized reply
struct reply_t {
	char data[REPLY_DATA_SIZE];
	ssize_t size;
	// Prevent secret keys to be shown to other users
	bool allow_debug;
};

void r_init( struct reply_t *r, bool allow_debug ) {
	r->data[0] = '\0';
	r->size = 0;
	r->allow_debug = allow_debug;
}

// Append a formatted string to the packet buffer
void r_printf( struct reply_t *r, const char *format, ... ) {
	va_list vlist;
	int written;

	va_start( vlist, format );
	written = vsnprintf( r->data + r->size, REPLY_DATA_SIZE - 1 , format, vlist );
	va_end( vlist );

	// Ignore characters that do not fit into packet
	if( written > 0 ) {
		r->size += written;
	} else {
		r->data[r->size] = '\0';
	}
}

// Partition a string to the common argc/argv arguments
void cmd_to_args( char *str, int *argc, char **argv, int max_argv ) {
	size_t len, i;

	len = strlen( str );
	*argc = 0;

	// Zero out white/control characters
	for( i = 0; i <= len; i++ ) {
		if( str[i] <= ' ') {
			str[i] = '\0';
		}
	}

	// Record strings
	for( i = 0; i <= len; i++ ) {

		if( str[i] == '\0') {
			continue;
		}

		if( *argc >= max_argv - 1 ) {
			break;
		}

		argv[*argc] = &str[i];
		*argc += 1;
		i += strlen( &str[i] );
	}

	argv[*argc] = NULL;
}

int cmd_import( struct reply_t *r, const char *addr_str) {
	IP addr;
	int rc;

	// If the address contains no port - use the default port
	if( (rc = addr_parse_full( &addr, addr_str, DHT_PORT, gconf->af )) == 0 ) {
		if( kad_ping( &addr ) == 0 ) {
			r_printf( r, "Send ping to: %s\n", str_addr( &addr ) );
			return 0;
		} else {
			r_printf( r, "Failed to send ping.\n" );
			return 1;
		}
	} else if( rc == -1 ) {
		r_printf( r, "Failed to parse address.\n" );
		return 1;
	} else {
		r_printf( r, "Failed to resolve address.\n" );
		return 1;
	}
}

void cmd_print_status( struct reply_t *r ) {
	r->size += kad_status( r->data + r->size, REPLY_DATA_SIZE - r->size );
}

int cmd_blacklist( struct reply_t *r, const char *addr_str ) {
	IP addr;

	if( addr_parse( &addr, addr_str, NULL, gconf->af ) == 0 ) {
		kad_blacklist( &addr );
		r_printf( r, "Added to blacklist: %s\n", str_addr( &addr ) );
		return 0;
	} else {
		r_printf( r, "Invalid address.\n" );
		return 1;
	}
}

// Export up to 32 peer addresses - more would not fit into one UDP packet
int cmd_export( struct reply_t *r ) {
	IP addr_array[32];
	size_t addr_num;
	size_t i;

	addr_num = N_ELEMS(addr_array);
	if( kad_export_nodes( addr_array, &addr_num ) != 0 ) {
		return 1;
	}

	for( i = 0; i < addr_num; ++i ) {
		r_printf( r, "%s\n", str_addr( &addr_array[i] ) );
	}

	if( i == 0 ) {
		r_printf( r, "No good nodes found.\n" );
		return 1;
	}

	return 0;
}

int cmd_exec( struct reply_t *r, int argc, char **argv ) {
	time_t lifetime;
	int minutes;
	IP addrs[16];
	char hostname[256];
	char dummy[4];
	int count;
	int port;
	struct value_t *value;
	int rc = 0;

	if( argc == 0 ) {

		// Print usage
		r_printf( r, cmd_usage );
		if( r->allow_debug ) {
			r_printf( r, cmd_usage_debug );
		}
		rc = 1;

	} else if( strcmp( argv[0], "import" ) == 0 && argc == 2 ) {

		rc = cmd_import( r, argv[1] );
#if 0
	} else if( strcmp( argv[0], "lookup_node" ) == 0 && argc == 2 ) {

		// Check searches for node
		rc = kad_lookup_node( argv[1], &addrs[0] );
		if( rc == 0 ) {
			r_printf( r, "%s\n", str_addr( &addrs[0] ) );
		} else if( rc == 1 ) {
			r_printf( r ,"No search found.\n" );
			rc = 1;
		} else if( rc == 2 ) {
			r_printf( r ,"Invalid id format. 20 digit hex string expected.\n" );
			rc = 1;
		} else {
			rc = 1;
		}
#endif
	} else if( strcmp( argv[0], "lookup" ) == 0 && argc == 2 ) {

		size_t num = N_ELEMS(addrs);
		size_t i;

		// Check searches for node
		rc = kad_lookup( argv[1], addrs, &num );

		if( rc >= 0 && num > 0 ) {
			for( i = 0; i < num; ++i ) {
				r_printf( r, "%s\n", str_addr( &addrs[i] ) );
			}
			rc = 0;
		} else if( rc < 0 ) {
			r_printf( r ,"Some error occured.\n" );
			rc = 1;
		} else if( rc == 0 ) {
			r_printf( r ,"Search in progress.\n" );
			rc = 1;
		} else {
			r_printf( r ,"Search started.\n" );
			rc = 1;
		}
	} else if( strcmp( argv[0], "status" ) == 0 && argc == 1 ) {

		// Print node id and statistics
		cmd_print_status( r );

	} else if( strcmp( argv[0], "announce" ) == 0 && (argc == 1 || argc == 2 || argc == 3) ) {

		if( argc == 1 ) {
			// Announce all values
			count = 0;
			value = announces_get();
			while( value ) {
				kad_announce_once( value->id, value->port );
				count++;
				value = value->next;
			}
			r_printf( r ,"%d announcements started.\n", count );
			rc = 0;
			goto end;
		} else if( argc == 2 ) {
			minutes = 0;
			lifetime = 0;
		} else if( argc == 3 ) {
			minutes = atoi( argv[2] );
			if( minutes < 0 ) {
				minutes = 0;
				lifetime = LONG_MAX;
			} else {
				// Round up to multiple of 30 minutes
				minutes = (30 * (minutes / 30 + 1));
				lifetime = (time_now_sec() + (minutes * 60));
			}
		} else {
			// Make compilers happy
			exit( 1 );
		}

		// Parse <hostname>:[<port>]
		port = 0;
		rc = sscanf(argv[1], "%255[^:]:%d%4s", hostname, &port, dummy);

		if( (rc == 1 || rc == 2) && kad_announce( hostname, port, lifetime ) >= 0 ) {
#ifdef FWD
			// Add port forwarding
			if( port != 0 ) {
				fwd_add( port, lifetime );
			}
#endif
			if( lifetime == 0 ) {
				r_printf( r ,"Start single announcement now.\n" );
			} else if( lifetime == LONG_MAX ) {
				r_printf( r ,"Start regular announcements for the entire run time (port %d).\n", port );
			} else {
				r_printf( r ,"Start regular announcements for %d minutes (port %d).\n", minutes, port );
			}
		} else {
			r_printf( r ,"Invalid port or query too long.\n" );
			rc = 1;
		}

	} else if( argc == 2 && strcmp( argv[0], "blacklist" ) == 0 ) {

		rc = cmd_blacklist( r, argv[1] );

	} else if( argc == 1 && strcmp( argv[0], "export" ) == 0 ) {

		rc = cmd_export( r );

	} else if( argc == 2 && strcmp( argv[0], "list" ) == 0 && r->allow_debug ) {

		if( gconf->is_daemon == 1 ) {
			r_printf( r ,"The 'list' command is not available while KadNode runs as daemon.\n" );
			rc = 1;
			goto end;
		} else if( strcmp( argv[1], "blacklist" ) == 0 ) {
			kad_debug_blacklist( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "buckets" ) == 0 ) {
			kad_debug_buckets( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "constants") == 0 ) {
			kad_debug_constants( STDOUT_FILENO );
			rc = 0;
#ifdef FWD
		} else if( strcmp( argv[1], "forwardings") == 0 ) {
			fwd_debug( STDOUT_FILENO );
			rc = 0;
#endif
#ifdef AUTH
		} else if( strcmp( argv[1], "pkeys") == 0 ) {
			auth_debug_pkeys( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "skeys") == 0 ) {
			auth_debug_skeys( STDOUT_FILENO );
			rc = 0;
#endif
		} else if( strcmp( argv[1], "results") == 0 ) {
			searches_debug( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "searches") == 0 ) {
			kad_debug_searches( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "storage") == 0 ) {
			kad_debug_storage( STDOUT_FILENO );
			rc = 0;
		} else if( strcmp( argv[1], "values" ) == 0 ) {
			announces_debug( STDOUT_FILENO );
			rc = 0;
		} else {
			dprintf( STDERR_FILENO, "Unknown argument.\n" );
			rc = 1;
		}
		r_printf( r ,"\nOutput send to console.\n" );

	} else {
		// Print usage
		r_printf( r, cmd_usage );
		if( r->allow_debug ) {
			r_printf( r, cmd_usage_debug );
		}
		rc = 1;
	}

end:
	;
	return rc;
}

void cmd_remote_handler( int rc, int sock ) {
	char* argv[32];
	int argc;

	IP clientaddr;
	socklen_t addrlen_ret;
	socklen_t addrlen;
	char request[1500];
	struct reply_t reply;

	addrlen_ret = sizeof(IP);
	rc = recvfrom( sock, request, sizeof(request) - 1, 0, (struct sockaddr*)&clientaddr, &addrlen_ret );
	if( rc <= 0 ) {
		return;
	} else {
		request[rc] = '\0';
	}

	// Initialize reply and reserve room for return status
	r_init( &reply, false );
	r_printf( &reply, "_" );

	// Split up the command line into an argument array
	cmd_to_args( request, &argc, &argv[0], N_ELEMS(argv) );

	// Execute command line
	rc = cmd_exec( &reply, argc, argv );

	// Insert return code
	reply.data[0] = (rc == 0) ? '0' : '1';

	addrlen = addr_len( &clientaddr );
	rc = sendto( sock, reply.data, reply.size, 0, (struct sockaddr *)&clientaddr, addrlen );
}

void cmd_console_handler( int rc, int fd ) {
	char request[512];
	char *req;
	struct reply_t reply;
	char *argv[32];
	int argc;

	if( rc == 0 ) {
		return;
	}

	// Read line
	req = fgets( request, sizeof(request), stdin );

	if( req == NULL ) {
		return;
	}

	// Split up the command line into an argument array
	cmd_to_args( request, &argc, &argv[0], N_ELEMS(argv) );

	// Initialize reply
	r_init( &reply, true );

	// Execute command line
	rc = cmd_exec( &reply, argc, argv );

	if( rc == 0 ) {
		fprintf( stdout, "%.*s\n", (int) reply.size, reply.data );
	} else {
		fprintf( stderr, "%.*s\n", (int) reply.size, reply.data );
	}
}

void cmd_setup( void ) {
	int sock;

	if( str_isZero( gconf->cmd_port ) ) {
		return;
	}

	sock = net_bind( "CMD", "::1", gconf->cmd_port, NULL, IPPROTO_UDP, AF_UNSPEC );
	net_add_handler( sock, &cmd_remote_handler );

	if( gconf->is_daemon == 0 && gconf->cmd_disable_stdin == 0 ) {
		// Wait for other messages to be displayed
		sleep( 1 );

		fprintf( stdout, "Press Enter for help.\n" );
		net_add_handler( STDIN_FILENO, &cmd_console_handler );
	}
}

void cmd_free( void ) {
	// Nothing to do
}
