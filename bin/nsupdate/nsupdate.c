/*
 * Copyright (C) 2000  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/* $Id: nsupdate.c,v 1.31 2000/07/18 00:47:00 bwelling Exp $ */

#include <config.h>

#include <isc/base64.h>
#include <isc/buffer.h>
#include <isc/condition.h>
#include <isc/commandline.h>
#include <isc/entropy.h>
#include <isc/lex.h>
#include <isc/mem.h>
#include <isc/mutex.h>
#include <isc/region.h>
#include <isc/sockaddr.h>
#include <isc/socket.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/timer.h>
#include <isc/types.h>
#include <isc/util.h>

#include <dns/callbacks.h>
#include <dns/dispatch.h>
#include <dns/events.h>
#include <dns/message.h>
#include <dns/name.h>
#include <dns/rdata.h>
#include <dns/rdataclass.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatastruct.h>
#include <dns/rdatatype.h>
#include <dns/request.h>
#include <dns/result.h>
#include <dns/tsig.h>

#include <dst/dst.h>

#include <lwres/lwres.h>
#include <lwres/net.h>

#include <ctype.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>

#define MXNAME 256
#define MAXPNAME 1025
#define MAXCMD 1024
#define NAMEBUF 512
#define WORDLEN 512
#define PACKETSIZE 2048
#define MSGTEXT 4096
#define FIND_TIMEOUT 5

#define DNSDEFAULTPORT 53

#define RESOLV_CONF "/etc/resolv.conf"

static isc_boolean_t busy = ISC_FALSE;
static isc_boolean_t debugging = ISC_FALSE, ddebugging = ISC_FALSE;
static isc_boolean_t have_ipv6 = ISC_FALSE;
static isc_boolean_t is_dst_up = ISC_FALSE;
static isc_boolean_t usevc = ISC_FALSE;
static isc_mutex_t lock;
static isc_condition_t cond;
static isc_taskmgr_t *taskmgr = NULL;
static isc_task_t *global_task = NULL;
static isc_mem_t *mctx = NULL;
static dns_dispatchmgr_t *dispatchmgr = NULL;
static dns_requestmgr_t *requestmgr = NULL;
static isc_socketmgr_t *socketmgr = NULL;
static isc_timermgr_t *timermgr = NULL;
static dns_dispatch_t *dispatchv4 = NULL;
static dns_message_t *updatemsg = NULL;
static dns_fixedname_t resolvdomain; /* from resolv.conf's domain line */
static dns_name_t *origin; /* Points to one of above, or dns_rootname */
static dns_fixedname_t fuserzone;
static dns_name_t *userzone = NULL;
static dns_tsigkey_t *key = NULL;
static lwres_context_t *lwctx = NULL;
static lwres_conf_t *lwconf;
static isc_sockaddr_t *servers;
static int ns_inuse = 0;
static int ns_total = 0;
static isc_sockaddr_t *userserver = NULL;
static char *keystr = NULL, *keyfile = NULL;
static isc_entropy_t *entp = NULL;

typedef struct nsu_requestinfo {
	dns_message_t *msg;
	isc_sockaddr_t *addr;
} nsu_requestinfo_t;

static void sendrequest(isc_sockaddr_t *address, dns_message_t *msg,
			dns_request_t **request);

#define STATUS_MORE 0
#define STATUS_SEND 1
#define STATUS_QUIT 2
#define STATUS_SYNTAX 3

static void
fatal(const char *format, ...) {
	va_list args;

	va_start(args, format);	
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

static void
debug(const char *format, ...) {
	va_list args;

	if (debugging) {
		va_start(args, format);	
		vfprintf(stderr, format, args);
		va_end(args);
		fprintf(stderr, "\n");
	}
}

static void
ddebug(const char *format, ...) {
	va_list args;

	if (ddebugging) {
		va_start(args, format);	
		vfprintf(stderr, format, args);
		va_end(args);
		fprintf(stderr, "\n");
	}
}

static inline void
check_result(isc_result_t result, const char *msg) {
	if (result != ISC_R_SUCCESS)
		fatal("%s: %s", msg, isc_result_totext(result));
}

static void *
mem_alloc(void *arg, size_t size) {
	return (isc_mem_get(arg, size));
}

static void
mem_free(void *arg, void *mem, size_t size) {
	isc_mem_put(arg, mem, size);
}

static char *
nsu_strsep(char **stringp, const char *delim) {
	char *string = *stringp;
	char *s;
	const char *d;
	char sc, dc;

	if (string == NULL)
		return (NULL);

	for (; *string != '\0'; string++) {
		sc = *string;
		for (d = delim; (dc = *d) != '\0'; d++) {
			if (sc == dc)
				break;
		}
		if (dc == 0)
			break;
	}

	for (s = string; *s != '\0'; s++) {
		sc = *s;
		for (d = delim; (dc = *d) != '\0'; d++) {
			if (sc == dc) {
				*s++ = '\0';
				*stringp = s;
				return (string);
			}
		}
	}
	*stringp = NULL;
	return (string);
}

static unsigned int
count_dots(char *s, isc_boolean_t *last_was_dot) {
	int i = 0;
	*last_was_dot = ISC_FALSE;
	while (*s != 0) {
		if (*s++ == '.') {
			i++;
			*last_was_dot = ISC_TRUE;
		} else
			*last_was_dot = ISC_FALSE;
	}
	return (i);
}

static void
reset_system(void) {
	isc_result_t result;

	ddebug("reset_system()");
	/* If the update message is still around, destroy it */
	if (updatemsg != NULL)
		dns_message_reset(updatemsg, DNS_MESSAGE_INTENTRENDER);
	else {
		result = dns_message_create(mctx, DNS_MESSAGE_INTENTRENDER,
					    &updatemsg);
		check_result(result, "dns_message_create");
	}
	updatemsg->opcode = dns_opcode_update;
}

static void
setup_key() {
	unsigned char *secret = NULL;
	int secretlen;
	isc_buffer_t secretbuf;
	isc_result_t result;
	dns_fixedname_t fkeyname;
	dns_name_t *keyname;

	if (keystr != NULL) {
		isc_buffer_t keynamesrc;
		char *secretstr;
		char *s;

		debug("Creating key...");

		s = strchr(keystr, ':');
		if (s == NULL || s == keystr || *s == 0)
			fatal("key option must specify keyname:secret\n");
		secretstr = s + 1;

		dns_fixedname_init(&fkeyname);
		keyname = dns_fixedname_name(&fkeyname);

		isc_buffer_init(&keynamesrc, keystr, s - keystr);
		isc_buffer_add(&keynamesrc, s - keystr);

		debug("namefromtext");
		result = dns_name_fromtext(keyname, &keynamesrc, dns_rootname,
					   ISC_FALSE, NULL);
		check_result(result, "dns_name_fromtext");

		secretlen = strlen(secretstr) * 3 / 4;
		secret = isc_mem_allocate(mctx, secretlen);
		if (secret == NULL)
			fatal("out of memory");

		isc_buffer_init(&secretbuf, secret, secretlen);
		result = isc_base64_decodestring(mctx, secretstr, &secretbuf);
		if (result != ISC_R_SUCCESS) {
			fprintf(stderr, "Couldn't create key from %s: %s\n",
				keystr, isc_result_totext(result));
			goto failure;
		}

		secretlen = isc_buffer_usedlength(&secretbuf);
		debug("close");
	} else {
		dst_key_t *dstkey = NULL;

		result = dst_key_fromnamedfile(keyfile, DST_TYPE_PRIVATE,
					       mctx, &dstkey);
		if (result != ISC_R_SUCCESS) {
			fprintf(stderr, "Couldn't read key from %s: %s\n",
				keyfile, isc_result_totext(result));
			goto failure;
		}
		secretlen = (dst_key_size(dstkey) + 7) >> 3;
		secret = isc_mem_allocate(mctx, secretlen);
		if (secret == NULL)
			fatal("out of memory");
		isc_buffer_init(&secretbuf, secret, secretlen);
		result = dst_key_tobuffer(dstkey, &secretbuf);
		if (result != ISC_R_SUCCESS) {
			fprintf(stderr, "Couldn't read key from %s: %s\n",
				keyfile, isc_result_totext(result));
			goto failure;
		}
		keyname = dst_key_name(dstkey);
	}
		
	debug("keycreate");
	result = dns_tsigkey_create(keyname, dns_tsig_hmacmd5_name,
				    secret, secretlen, ISC_TRUE, NULL, 0, 0,
				    mctx, NULL, &key);
	if (result != ISC_R_SUCCESS) {
		char *str;
		if (keystr != NULL)
			str = keystr;
		else
			str = keyfile;
		fprintf(stderr, "Couldn't create key from %s: %s\n",
			str, dns_result_totext(result));
	}
	isc_mem_free(mctx, secret);
	return;

 failure:

	if (secret != NULL)
		isc_mem_free(mctx, secret);
}

static void
setup_system(void) {
	isc_result_t result;
	isc_sockaddr_t bind_any;
	isc_buffer_t buf;
	lwres_result_t lwresult;
	int i;

	ddebug("setup_system()");

	/*
	 * Warning: This is not particularly good randomness.  We'll
	 * just use random() now for getting id values, but doing so
	 * does NOT insure that id's can't be guessed.
	 *
	 * XXX Shouldn't random() be called somewhere if this is here?
	 */
	srandom(getpid() + (int)&setup_system);

	result = isc_net_probeipv4();
	check_result(result, "isc_net_probeipv4");

	/* XXXMWS There isn't any actual V6 support in the code yet */
	result = isc_net_probeipv6();
	if (result == ISC_R_SUCCESS)
		have_ipv6=ISC_TRUE;

	result = isc_mem_create(0, 0, &mctx);
	check_result(result, "isc_mem_create");

	lwresult = lwres_context_create(&lwctx, mctx, mem_alloc, mem_free, 1);
	if (lwresult != LWRES_R_SUCCESS)
		fatal("lwres_context_create failed");

	lwresult = lwres_conf_parse(lwctx, RESOLV_CONF);
	if (lwresult != LWRES_R_SUCCESS)
		fprintf(stderr,
			"An error was encountered in /etc/resolv.conf\n");

	lwconf = lwres_conf_get(lwctx);

	ns_total = lwconf->nsnext;
	if (ns_total <= 0)
		fatal("no valid servers found");
	servers = isc_mem_get(mctx, ns_total * sizeof(isc_sockaddr_t));
	if (servers == NULL)
		fatal("out of memory");
	for (i = 0; i < ns_total; i++) {
		if (lwconf->nameservers[i].family == LWRES_ADDRTYPE_V4) {
			struct in_addr in4;
			memcpy(&in4, lwconf->nameservers[i].address, 4);
			isc_sockaddr_fromin(&servers[i], &in4, DNSDEFAULTPORT);
		} else {
			struct in6_addr in6;
			memcpy(&in6, lwconf->nameservers[i].address, 16);
			isc_sockaddr_fromin6(&servers[i], &in6,
					     DNSDEFAULTPORT);
		}
	}

	result = dns_dispatchmgr_create(mctx, NULL, &dispatchmgr);
	check_result(result, "dns_dispatchmgr_create");

	result = isc_socketmgr_create(mctx, &socketmgr);
	check_result(result, "dns_socketmgr_create");

	result = isc_timermgr_create(mctx, &timermgr);
	check_result(result, "dns_timermgr_create");

	result = isc_taskmgr_create(mctx, 1, 0, &taskmgr);
	check_result(result, "isc_taskmgr_create");

	result = isc_task_create(taskmgr, 0, &global_task);
	check_result(result, "isc_task_create");

	result = isc_entropy_create(mctx, &entp);
	check_result(result, "isc_entropy_create");

	result = dst_lib_init(mctx, entp, 0);
	check_result(result, "dst_lib_init");
	is_dst_up = ISC_TRUE;

	isc_sockaddr_any(&bind_any);

	result = dns_dispatch_getudp(dispatchmgr, socketmgr, taskmgr,
				     &bind_any, PACKETSIZE, 4, 2, 3, 5,
				     DNS_DISPATCHATTR_UDP |
				     DNS_DISPATCHATTR_IPV4 |
				     DNS_DISPATCHATTR_MAKEQUERY, 0,
				     &dispatchv4);
	check_result(result, "dns_dispatch_getudp");

	result = dns_requestmgr_create(mctx, timermgr,
				       socketmgr, taskmgr, dispatchmgr,
				       dispatchv4, NULL, &requestmgr);
	check_result(result, "dns_requestmgr_create");

	if (lwconf->domainname != NULL) {
		dns_fixedname_init(&resolvdomain);
		isc_buffer_init(&buf, lwconf->domainname,
				strlen(lwconf->domainname));
		isc_buffer_add(&buf, strlen(lwconf->domainname));
		result = dns_name_fromtext(dns_fixedname_name(&resolvdomain),
					   &buf, dns_rootname, ISC_FALSE,
					   NULL);
		check_result(result, "dns_name_fromtext");
		origin = dns_fixedname_name(&resolvdomain);
	}
	else
		origin = dns_rootname;

	if (keystr != NULL || keyfile != NULL)
		setup_key();
}

static void
get_address(char *host, in_port_t port, isc_sockaddr_t *sockaddr) {
        struct in_addr in4;
        struct in6_addr in6;
        struct hostent *he;

        ddebug("get_address()");
        if (have_ipv6 && inet_pton(AF_INET6, host, &in6) == 1)
                isc_sockaddr_fromin6(sockaddr, &in6, port);
        else if (inet_pton(AF_INET, host, &in4) == 1)
                isc_sockaddr_fromin(sockaddr, &in4, port);
        else {
                he = gethostbyname(host);
                if (he == NULL)
                     fatal("Couldn't look up your server host %s.  errno=%d",
                              host, h_errno);
                INSIST(he->h_addrtype == AF_INET);
                isc_sockaddr_fromin(sockaddr,
                                    (struct in_addr *)(he->h_addr_list[0]),
                                    port);
        }
}

static void
parse_args(int argc, char **argv) {
	int ch;

	debug("parse_args");
	while ((ch = isc_commandline_parse(argc, argv, "dDMy:vk:")) != -1) {
		switch (ch) {
		case 'd':
			debugging = ISC_TRUE;
			break;
		case 'D': /* was -dd */
			debugging = ISC_TRUE;
			ddebugging = ISC_TRUE;
			break;
		case 'M': /* was -dm */
			debugging = ISC_TRUE;
			ddebugging = ISC_TRUE;
			isc_mem_debugging = ISC_TRUE;
			break;
		case 'y':
			keystr = isc_commandline_argument;
			break;
		case 'v':
			usevc = ISC_TRUE;
			break;
		case 'k':
			keyfile = isc_commandline_argument;
			break;
		default:
			fprintf(stderr, "%s: invalid argument -%c\n",
				argv[0], ch);
			fprintf(stderr, "usage: nsupdate [-d] "
				"[-y keyname:secret | -k keyfile] [-v]\n");
			exit(1);
		}
	}
	if (keyfile != NULL && keystr != NULL) {
		fprintf(stderr, "%s: cannot specify both -k and -y\n",
			argv[0]);
		exit(1);
	}
}

static isc_uint16_t
parse_name(char **cmdlinep, dns_message_t *msg, dns_name_t **namep) {
	isc_result_t result;
	char *word;
	isc_buffer_t *namebuf = NULL;
	isc_buffer_t source;
	unsigned int dots;
	isc_boolean_t last;
	dns_name_t *rn;

	word = nsu_strsep(cmdlinep, " \t\r\n");
	if (*word == 0) {
		fprintf(stderr, "failed to read owner name\n");
		return (STATUS_SYNTAX);
	}

	result = dns_message_gettempname(msg, namep);
	check_result(result, "dns_message_gettempname");
	result = isc_buffer_allocate(mctx, &namebuf, NAMEBUF);
	check_result(result, "isc_buffer_allocate");
	dns_name_init(*namep, NULL);
	dns_name_setbuffer(*namep, namebuf);
	dns_message_takebuffer(msg, &namebuf);
	isc_buffer_init(&source, word, strlen(word));
	isc_buffer_add(&source, strlen(word));
	dots = count_dots(word, &last);
	if (dots > lwconf->ndots || last)
		rn = dns_rootname;
	else if (userzone != NULL)
		rn = userzone;
	else
		rn = origin;
	result = dns_name_fromtext(*namep, &source, rn,
				   ISC_FALSE, NULL);
	check_result(result, "dns_name_fromtext");
	isc_buffer_invalidate(&source);
	return (STATUS_MORE);
}

static isc_uint16_t
parse_rdata(char **cmdlinep, dns_rdataclass_t rdataclass,
	    dns_rdatatype_t rdatatype, dns_message_t *msg,
	    dns_rdata_t **rdatap)
{
	char *cmdline = *cmdlinep;
	isc_buffer_t source, *buf = NULL;
	isc_lex_t *lex = NULL;
	dns_rdatacallbacks_t callbacks;
	isc_result_t result;
	dns_name_t *rn;

	while (*cmdline != 0 && isspace((unsigned char)*cmdline))
		cmdline++;

	if (*cmdline != 0) {
		result = isc_lex_create(mctx, WORDLEN, &lex);
		check_result(result, "isc_lex_create");	

		isc_buffer_init(&source, cmdline, strlen(cmdline));
		isc_buffer_add(&source, strlen(cmdline));
		result = isc_lex_openbuffer(lex, &source);
		check_result(result, "isc_lex_openbuffer");

		result = isc_buffer_allocate(mctx, &buf, MXNAME);
		check_result(result, "isc_buffer_allocate");
		dns_rdatacallbacks_init_stdio(&callbacks);
		if (userzone != NULL)
			rn = userzone;
		else
			rn = origin;
		result = dns_rdata_fromtext(*rdatap, rdataclass, rdatatype,
					    lex, rn, ISC_FALSE, buf,
					    &callbacks);
		dns_message_takebuffer(msg, &buf);
		isc_lex_destroy(&lex);
		if (result != ISC_R_SUCCESS)
			return (STATUS_MORE);
	}
	*cmdlinep = cmdline;
	return (STATUS_MORE);
}

static isc_uint16_t
make_prereq(char *cmdline, isc_boolean_t ispositive, isc_boolean_t isrrset) {
	isc_result_t result;
	char *word;
	dns_name_t *name = NULL;
	isc_textregion_t region;
	dns_rdataset_t *rdataset = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	dns_rdataclass_t rdataclass;
	dns_rdatatype_t rdatatype;
	dns_rdata_t *rdata = NULL;
	isc_uint16_t retval;

	ddebug("make_prereq()");

	/*
	 * Read the owner name
	 */
	retval = parse_name(&cmdline, updatemsg, &name);
	if (retval != STATUS_MORE)
		return (retval);

	/*
	 * If this is an rrset prereq, read the class or type.
	 */
	if (isrrset) {
		word = nsu_strsep(&cmdline, " \t\r\n");
		if (*word == 0) {
			fprintf(stderr, "failed to read class or type\n");
			dns_message_puttempname(updatemsg, &name);
			return (STATUS_SYNTAX);
		}
		region.base = word;
		region.length = strlen(word);
		result = dns_rdataclass_fromtext(&rdataclass, &region);
		if (result == ISC_R_SUCCESS) {
			/*
			 * Now read the type.
			 */
			word = nsu_strsep(&cmdline, " \t\r\n");
			if (*word == 0) {
				fprintf(stderr, "failed to read type\n");
				dns_message_puttempname(updatemsg, &name);
				return (STATUS_SYNTAX);
			}
			region.base = word;
			region.length = strlen(word);
			result = dns_rdatatype_fromtext(&rdatatype, &region);
			check_result(result, "dns_rdatatype_fromtext");
		} else {
			rdataclass = dns_rdataclass_in;
			result = dns_rdatatype_fromtext(&rdatatype, &region);
			check_result(result, "dns_rdatatype_fromtext");
		}
	} else
		rdatatype = dns_rdatatype_any;

	result = dns_message_gettemprdata(updatemsg, &rdata);
	check_result(result, "dns_message_gettemprdata");

	rdata->data = NULL;
	rdata->length = 0;

	if (isrrset && ispositive) {
		retval = parse_rdata(&cmdline, rdataclass, rdatatype,
				     updatemsg, &rdata);
		if (retval != STATUS_MORE)
			return (retval);
	}

	result = dns_message_gettemprdatalist(updatemsg, &rdatalist);
	check_result(result, "dns_message_gettemprdatalist");
	result = dns_message_gettemprdataset(updatemsg, &rdataset);
	check_result(result, "dns_message_gettemprdataset");
	dns_rdatalist_init(rdatalist);
	rdatalist->type = rdatatype;
	if (ispositive) {
		if (isrrset && rdata->data != NULL)
			rdatalist->rdclass = rdataclass;
		else
			rdatalist->rdclass = dns_rdataclass_any;
	} else
		rdatalist->rdclass = dns_rdataclass_none;
	rdatalist->covers = 0;
	rdatalist->ttl = 0;
	rdata->rdclass = rdatalist->rdclass;
	rdata->type = rdatatype;
	ISC_LIST_INIT(rdatalist->rdata);
	ISC_LIST_APPEND(rdatalist->rdata, rdata, link);
	dns_rdataset_init(rdataset);
	dns_rdatalist_tordataset(rdatalist, rdataset);		
	ISC_LIST_INIT(name->list);
	ISC_LIST_APPEND(name->list, rdataset, link);
	dns_message_addname(updatemsg, name, DNS_SECTION_PREREQUISITE);
	return (STATUS_MORE);
}

static isc_uint16_t
evaluate_prereq(char *cmdline) {
	char *word;
	isc_boolean_t ispositive, isrrset;

	ddebug("evaluate_prereq()");
	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0) {
		fprintf(stderr, "failed to read operation code\n");
		return (STATUS_SYNTAX);
	}
	if (strcasecmp(word, "nxdomain") == 0) {
		ispositive = ISC_FALSE;
		isrrset = ISC_FALSE;
	} else if (strcasecmp(word, "yxdomain") == 0) {
		ispositive = ISC_TRUE;
		isrrset = ISC_FALSE;
	} else if (strcasecmp(word, "nxrrset") == 0) {
		ispositive = ISC_FALSE;
		isrrset = ISC_TRUE;
	} else if (strcasecmp(word, "yxrrset") == 0) {
		ispositive = ISC_TRUE;
		isrrset = ISC_TRUE;
	} else {
		fprintf(stderr, "incorrect operation code: %s\n", word);
		return (STATUS_SYNTAX);
	}
	return (make_prereq(cmdline, ispositive, isrrset));
}

static isc_uint16_t
evaluate_server(char *cmdline) {
	char *word, *server;
	in_port_t port;

	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0) {
		fprintf(stderr, "failed to read server name\n");
		return (STATUS_SYNTAX);
	}
	server = word;

	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0)
		port = DNSDEFAULTPORT;
	else {
		char *endp;
		port = strtol(word, &endp, 10);
		if (*endp != 0) {
			fprintf(stderr, "port '%s' is not numeric\n", word);
			return (STATUS_SYNTAX);
		}
	}

	if (userserver == NULL) {
		userserver = isc_mem_get(mctx, sizeof(isc_sockaddr_t));
		if (userserver == NULL)
			fatal("out of memory");
	}

	get_address(server, port, userserver);

	return (STATUS_MORE);
}

static isc_uint16_t
evaluate_zone(char *cmdline) {
	char *word;
	isc_buffer_t b;
	isc_result_t result;

	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0) {
		fprintf(stderr, "failed to read zone name\n");
		return (STATUS_SYNTAX);
	}

	dns_fixedname_init(&fuserzone);
	userzone = dns_fixedname_name(&fuserzone);
	isc_buffer_init(&b, word, strlen(word));
	isc_buffer_add(&b, strlen(word));
	result = dns_name_fromtext(userzone, &b, dns_rootname, ISC_FALSE,
				   NULL);
	if (result != ISC_R_SUCCESS) {
		fprintf(stderr, "failed to parse zone name\n");
		return (STATUS_SYNTAX);
	}

	return (STATUS_MORE);
}

static isc_uint16_t
update_addordelete(char *cmdline, isc_boolean_t isdelete) {
	isc_result_t result;
	dns_name_t *name = NULL;
	isc_uint16_t ttl;
	char *word;
	dns_rdataclass_t rdataclass;
	dns_rdatatype_t rdatatype;
	dns_rdata_t *rdata = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	dns_rdataset_t *rdataset = NULL;
	isc_textregion_t region;
	char *endp;
	isc_uint16_t retval;

	ddebug("update_addordelete()");

	/*
	 * Read the owner name
	 */
	retval = parse_name(&cmdline, updatemsg, &name);
	if (retval != STATUS_MORE)
		return (retval);

	result = dns_message_gettemprdata(updatemsg, &rdata);
	check_result(result, "dns_message_gettemprdata");

	rdata->rdclass = 0;
	rdata->type = 0;
	rdata->data = NULL;
	rdata->length = 0;

	/*
	 * If this is an add, read the TTL and verify that it's numeric.
	 */
	if (!isdelete) {
		word = nsu_strsep(&cmdline, " \t\r\n");
		if (*word == 0) {
			fprintf(stderr, "failed to read owner ttl\n");
			goto failure;
		}
		ttl = strtol(word, &endp, 0);
		if (*endp != 0) {
			fprintf(stderr, "ttl '%s' is not numeric\n", word);
			goto failure;
		}
	} else
		ttl = 0;

	/*
	 * Read the class or type.
	 */
	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0) {
		if (isdelete) {
			rdataclass = dns_rdataclass_any;
			rdatatype = dns_rdatatype_any;
			goto doneparsing;
		} else {
			fprintf(stderr, "failed to read class or type\n");
			goto failure;
		}
	}
	region.base = word;
	region.length = strlen(word);
	result = dns_rdataclass_fromtext(&rdataclass, &region);
	if (result == ISC_R_SUCCESS) {
		/*
		 * Now read the type.
		 */
		word = nsu_strsep(&cmdline, " \t\r\n");
		if (*word == 0) {
			if (isdelete) {
				rdataclass = dns_rdataclass_any;
				rdatatype = dns_rdatatype_any;
				goto doneparsing;
			} else {
				fprintf(stderr, "failed to read type\n");
				goto failure;
			}
		}
		region.base = word;
		region.length = strlen(word);
		result = dns_rdatatype_fromtext(&rdatatype, &region);
		check_result(result, "dns_rdatatype_fromtext");
	} else {
		rdataclass = dns_rdataclass_in;
		result = dns_rdatatype_fromtext(&rdatatype, &region);
		check_result(result, "dns_rdatatype_fromtext");
	}

	retval = parse_rdata(&cmdline, rdataclass, rdatatype, updatemsg,
			     &rdata);
	if (retval != STATUS_MORE)
		goto failure;

	if (isdelete) {
		if (rdata->length == 0)
			rdataclass = dns_rdataclass_any;
		else
			rdataclass = dns_rdataclass_none;
	} else {
		if (rdata->length == 0) {
			fprintf(stderr, "failed to read rdata\n");
			goto failure;
		}
	}

 doneparsing:

	result = dns_message_gettemprdatalist(updatemsg, &rdatalist);
	check_result(result, "dns_message_gettemprdatalist");
	result = dns_message_gettemprdataset(updatemsg, &rdataset);
	check_result(result, "dns_message_gettemprdataset");
	dns_rdatalist_init(rdatalist);
	rdatalist->type = rdatatype;
	rdatalist->rdclass = rdataclass;
	rdatalist->covers = rdatatype;
	rdatalist->ttl = ttl;
	ISC_LIST_INIT(rdatalist->rdata);
	ISC_LIST_APPEND(rdatalist->rdata, rdata, link);
	dns_rdataset_init(rdataset);
	dns_rdatalist_tordataset(rdatalist, rdataset);
	ISC_LIST_INIT(name->list);
	ISC_LIST_APPEND(name->list, rdataset, link);
	dns_message_addname(updatemsg, name, DNS_SECTION_UPDATE);
	return (STATUS_MORE);

 failure:
	if (name != NULL)
		dns_message_puttempname(updatemsg, &name);
	if (rdata != NULL)
		dns_message_puttemprdata(updatemsg, &rdata);
	return (STATUS_SYNTAX);
}

static isc_uint16_t
evaluate_update(char *cmdline) {
	char *word;
	isc_boolean_t isdelete;

	ddebug("evaluate_update()");
	word = nsu_strsep(&cmdline, " \t\r\n");
	if (*word == 0) {
		fprintf(stderr, "failed to read operation code\n");
		return (STATUS_SYNTAX);
	}
	if (strcasecmp(word, "delete") == 0)
		isdelete = ISC_TRUE;
	else if (strcasecmp(word, "add") == 0)
		isdelete = ISC_FALSE;
	else {
		fprintf(stderr, "incorrect operation code: %s\n", word);
		return (STATUS_SYNTAX);
	}
	return (update_addordelete(cmdline, isdelete));
}

static void
show_message(void) {
	isc_result_t result;
	char store[MSGTEXT];
	isc_buffer_t buf;

	ddebug("show_message()");
	isc_buffer_init(&buf, store, MSGTEXT);
	result = dns_message_totext(updatemsg, 0, &buf);
	if (result != ISC_R_SUCCESS) {
		fprintf(stderr, "Failed to concert message to text format.\n");
		return;
	}
	printf("Outgoing update query:\n%.*s",
	       (int)isc_buffer_usedlength(&buf),
	       (char*)isc_buffer_base(&buf));
}
	

static isc_uint16_t
get_next_command(void) {
	char cmdlinebuf[MAXCMD];
	char *cmdline;
	char *word;

	ddebug("get_next_command()");
	fprintf(stdout, "> ");
	fgets (cmdlinebuf, MAXCMD, stdin);
	cmdline = cmdlinebuf;
	word = nsu_strsep(&cmdline, " \t\r\n");

	if (feof(stdin))
		return (STATUS_QUIT);
	if (*word == 0)
		return (STATUS_SEND);
	if (strcasecmp(word, "quit") == 0)
		return (STATUS_QUIT);
	if (strcasecmp(word, "prereq") == 0)
		return (evaluate_prereq(cmdline));
	if (strcasecmp(word, "update") == 0)
		return (evaluate_update(cmdline));
	if (strcasecmp(word, "server") == 0)
		return (evaluate_server(cmdline));
	if (strcasecmp(word, "zone") == 0)
		return (evaluate_zone(cmdline));
	if (strcasecmp(word, "send") == 0)
		return (STATUS_SEND);
	if (strcasecmp(word, "show") == 0) {
		show_message();
		return (STATUS_MORE);
	}
	fprintf(stderr, "incorrect section name: %s\n", word);
	return (STATUS_SYNTAX);
}

static isc_boolean_t
user_interaction(void) {
	isc_uint16_t result = STATUS_MORE;

	ddebug("user_interaction()");
	while ((result == STATUS_MORE) || (result == STATUS_SYNTAX))
		result = get_next_command();
	if (result == STATUS_SEND)
		return (ISC_TRUE);
	return (ISC_FALSE);

}

static void
done_update(isc_boolean_t acquirelock) {
	if (acquirelock)
		LOCK(&lock);
	busy = ISC_FALSE;
	SIGNAL(&cond);
	if (acquirelock)
		UNLOCK(&lock);
}

static void
update_completed(isc_task_t *task, isc_event_t *event) {
	dns_requestevent_t *reqev = NULL;
	isc_result_t result;
	isc_buffer_t buf;
	dns_message_t *rcvmsg = NULL;
	char bufstore[MSGTEXT];
	
	UNUSED(task);

	ddebug("updated_completed()");
	REQUIRE(event->ev_type == DNS_EVENT_REQUESTDONE);
	reqev = (dns_requestevent_t *)event;
	if (reqev->result != ISC_R_SUCCESS) {
		fprintf(stderr, "; Communication with server failed: %s\n",
			isc_result_totext(reqev->result));
		goto done;
	}

	result = dns_message_create(mctx, DNS_MESSAGE_INTENTPARSE, &rcvmsg);
	check_result(result, "dns_message_create");
	result = dns_request_getresponse(reqev->request, rcvmsg, ISC_TRUE);
	check_result(result, "dns_request_getresponse");
	if (debugging) {
		isc_buffer_init(&buf, bufstore, MSGTEXT);
		result = dns_message_totext(rcvmsg, 0, &buf);
		check_result(result, "dns_message_totext");
		fprintf(stderr, "\nReply from update query:\n%.*s\n",
			(int)isc_buffer_usedlength(&buf),
			(char*)isc_buffer_base(&buf));
	}
	dns_message_destroy(&rcvmsg);
 done:
	dns_request_destroy(&reqev->request);
	isc_event_free(&event);
	done_update(ISC_TRUE);
}

static void
send_update(dns_name_t *zonename, isc_sockaddr_t *master) {
	isc_result_t result;
	dns_request_t *request = NULL;
	dns_name_t *name = NULL;
	dns_rdataset_t *rdataset = NULL;
	unsigned int options = 0;

	ddebug("send_update()");

	result = dns_message_gettempname(updatemsg, &name);
	check_result(result, "dns_message_gettempname");
	dns_name_init(name, NULL);
	dns_name_clone(zonename, name);
	result = dns_message_gettemprdataset(updatemsg, &rdataset);
	check_result(result, "dns_message_gettemprdataset");
	dns_rdataset_makequestion(rdataset, dns_rdataclass_in,
				  dns_rdatatype_soa);
	ISC_LIST_INIT(name->list);
	ISC_LIST_APPEND(name->list, rdataset, link);
	dns_message_addname(updatemsg, name, DNS_SECTION_ZONE);

	if (usevc)
		options |= DNS_REQUESTOPT_TCP;
	result = dns_request_create(requestmgr, updatemsg, master,
				    options, key,
				    FIND_TIMEOUT, global_task,
				    update_completed, NULL, &request);
	check_result(result, "dns_request_create");
}

static void
recvsoa(isc_task_t *task, isc_event_t *event) {
	dns_requestevent_t *reqev = NULL;
	dns_request_t *request = NULL;
	isc_result_t result, eresult;
	dns_message_t *rcvmsg = NULL;
	dns_section_t section;
	dns_name_t *name = NULL;
	dns_rdataset_t *soaset = NULL;
	dns_rdata_soa_t soa;
	dns_rdata_t soarr;
	int pass = 0;
	dns_name_t master;
	isc_sockaddr_t *serveraddr, tempaddr;
	dns_name_t *zonename;
	nsu_requestinfo_t *reqinfo;
	dns_message_t *soaquery = NULL;
	isc_sockaddr_t *addr;

	UNUSED(task);

	ddebug("recvsoa()");
	REQUIRE(event->ev_type == DNS_EVENT_REQUESTDONE);
	reqev = (dns_requestevent_t *)event;
	request = reqev->request;
	eresult = reqev->result;
	reqinfo = reqev->ev_arg;
	soaquery = reqinfo->msg;
	addr = reqinfo->addr;

	isc_event_free(&event);
	reqev = NULL;

	if (eresult != ISC_R_SUCCESS) {
		char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	
		isc_sockaddr_format(addr, addrbuf, sizeof(addrbuf));
		fprintf(stderr, "; Communication with %s failed: %s\n",
		       addrbuf, isc_result_totext(eresult));
		if (userserver != NULL)
			fatal("Couldn't talk to specified nameserver.");
		else if (ns_inuse++ >= lwconf->nsnext)
			fatal("Couldn't talk to any default nameserver.");
		ddebug("Destroying request [%lx]", request);
		dns_request_destroy(&request);
		sendrequest(&servers[ns_inuse], soaquery, &request);
		isc_mem_put(mctx, reqinfo, sizeof(nsu_requestinfo_t));
		return;
	}
	dns_message_destroy(&soaquery);
	isc_mem_put(mctx, reqinfo, sizeof(nsu_requestinfo_t));

	ddebug("About to create rcvmsg");
	result = dns_message_create(mctx, DNS_MESSAGE_INTENTPARSE, &rcvmsg);
	check_result(result, "dns_message_create");
	result = dns_request_getresponse(request, rcvmsg, ISC_TRUE);
	check_result(result, "dns_request_getresponse");
	section = DNS_SECTION_ANSWER;
	if (debugging) {
		isc_buffer_t buf;
		char bufstore[MSGTEXT];

		isc_buffer_init(&buf, bufstore, MSGTEXT);
		result = dns_message_totext(rcvmsg, 0, &buf);
		check_result(result, "dns_message_totext");
		fprintf(stderr, "Reply from SOA query:\n%.*s\n",
			(int)isc_buffer_usedlength(&buf),
			(char*)isc_buffer_base(&buf));
	}

	if (rcvmsg->rcode != dns_rcode_noerror &&
	    rcvmsg->rcode != dns_rcode_nxdomain)
		fatal("response to SOA query was unsuccessful");

 lookforsoa:
	if (pass == 0)
		section = DNS_SECTION_ANSWER;
	else if (pass == 1)
		section = DNS_SECTION_AUTHORITY;
	else
		fatal("response to SOA query didn't contain an SOA");


	result = dns_message_firstname(rcvmsg, section);
	if (result != ISC_R_SUCCESS) {
		pass++;
		goto lookforsoa;
	}
	while (result == ISC_R_SUCCESS) {
		name = NULL;
		dns_message_currentname(rcvmsg, section, &name);
		soaset = NULL;
		result = dns_message_findtype(name, dns_rdatatype_soa, 0,
					      &soaset);
		if (result == ISC_R_SUCCESS)
			break;
		result = dns_message_nextname(rcvmsg, section);
	}

	if (soaset == NULL) {
		pass++;
		goto lookforsoa;
	}

	if (debugging) {
		char namestr[MAXPNAME];
		dns_name_format(name, namestr, sizeof(namestr));
		fprintf(stderr, "Found zone name: %s\n", namestr);
	}

	result = dns_rdataset_first(soaset);
	check_result(result, "dns_rdataset_first");

	dns_rdata_init(&soarr);
	dns_rdataset_current(soaset, &soarr);
	result = dns_rdata_tostruct(&soarr, &soa, NULL);
	check_result(result, "dns_rdata_tostruct");

	dns_name_init(&master, NULL);
	dns_name_clone(&soa.origin, &master);

	if (userzone != NULL)
		zonename = userzone;
	else
		zonename = name;

	if (debugging) {
		char namestr[MAXPNAME];
		dns_name_format(&master, namestr, sizeof(namestr));
		fprintf(stderr, "The master is: %s\n", namestr);
	}

	if (userserver != NULL)
		serveraddr = userserver;
	else {
		char serverstr[MXNAME];
		isc_buffer_t buf;

		isc_buffer_init(&buf, serverstr, sizeof(serverstr));
		result = dns_name_totext(&master, ISC_TRUE, &buf);
		check_result(result, "dns_name_totext");
		serverstr[isc_buffer_usedlength(&buf)] = 0;
		get_address(serverstr, DNSDEFAULTPORT, &tempaddr);
		serveraddr = &tempaddr;
	}

	send_update(zonename, serveraddr);

	dns_rdata_freestruct(&soa);
	dns_message_destroy(&rcvmsg);
	dns_request_destroy(&request);
	ddebug("Out of recvsoa");
}

static void
sendrequest(isc_sockaddr_t *address, dns_message_t *msg,
	    dns_request_t **request)
{
	isc_result_t result;
	nsu_requestinfo_t *reqinfo;

	reqinfo = isc_mem_get(mctx, sizeof(nsu_requestinfo_t));
	if (reqinfo == NULL)
		fatal("out of memory");
	reqinfo->msg = msg;
	reqinfo->addr = address;
	result = dns_request_create(requestmgr, msg, address,
				    0, NULL, FIND_TIMEOUT, global_task,
				    recvsoa, reqinfo, request);
	check_result(result, "dns_request_create");
}

static void
start_update(void) {
	isc_result_t result;
	dns_rdataset_t *rdataset = NULL;
	dns_name_t *name = NULL;
	dns_request_t *request = NULL;
	dns_message_t *soaquery = NULL;
	dns_name_t *firstname;

	ddebug("start_update()");

	if (userzone != NULL && userserver != NULL) {
		send_update(userzone, userserver);
		return;
	}

	result = dns_message_firstname(updatemsg, DNS_SECTION_UPDATE);
	if (result != ISC_R_SUCCESS) {
		done_update(ISC_FALSE);
		return;
	}

	result = dns_message_create(mctx, DNS_MESSAGE_INTENTRENDER,
				    &soaquery);
	check_result(result, "dns_message_create");

	soaquery->flags |= DNS_MESSAGEFLAG_RD;

	result = dns_message_gettempname(soaquery, &name);
	check_result(result, "dns_message_gettempname");

	result = dns_message_gettemprdataset(soaquery, &rdataset);
	check_result(result, "dns_message_gettemprdataset");

	dns_rdataset_makequestion(rdataset, dns_rdataclass_in,
				  dns_rdatatype_soa);

	firstname = NULL;
	dns_message_currentname(updatemsg, DNS_SECTION_UPDATE, &firstname);
	dns_name_init(name, NULL);
	dns_name_clone(firstname, name);

	ISC_LIST_INIT(name->list);
	ISC_LIST_APPEND(name->list, rdataset, link);
	dns_message_addname(soaquery, name, DNS_SECTION_QUESTION);

	if (userserver != NULL)
		sendrequest(userserver, soaquery, &request);
	else {
		ns_inuse = 0;
		sendrequest(&servers[ns_inuse], soaquery, &request);
	}
}

static void
cleanup(void) {
	ddebug("cleanup()");

	if (userserver != NULL)
		isc_mem_put(mctx, userserver, sizeof(isc_sockaddr_t));

	if (key != NULL) {
		debug("Freeing key");
		dns_tsigkey_setdeleted(key);
		dns_tsigkey_detach(&key);
	}

	if (updatemsg != NULL)
		dns_message_destroy(&updatemsg);

	if (is_dst_up) {
		debug("Destroy DST lib");
		dst_lib_destroy();
		is_dst_up = ISC_FALSE;
	}

	if (entp != NULL) {
		debug("Detach from entropy");
		isc_entropy_detach(&entp);
	}

	lwres_conf_clear(lwctx);
	lwres_context_destroy(&lwctx);

	isc_mem_put(mctx, servers, ns_total * sizeof(isc_sockaddr_t));
		
	ddebug("Shutting down request manager");
	dns_requestmgr_shutdown(requestmgr);
	dns_requestmgr_detach(&requestmgr);

	ddebug("Freeing the dispatcher");
	dns_dispatch_detach(&dispatchv4);

	ddebug("Shutting down dispatch manager");
	dns_dispatchmgr_destroy(&dispatchmgr);

	ddebug("Ending task");
	isc_task_detach(&global_task);

	ddebug("Shutting down task manager");
	isc_taskmgr_destroy(&taskmgr);

	ddebug("Shutting down socket manager");
	isc_socketmgr_destroy(&socketmgr);

	ddebug("Shutting down timer manager");
	isc_timermgr_destroy(&timermgr);

	ddebug("Destroying memory context");
	if (isc_mem_debugging)
		isc_mem_stats(mctx, stderr);
	isc_mem_destroy(&mctx);
}

int
main(int argc, char **argv) {
        isc_result_t result;

        parse_args(argc, argv);

        setup_system();
        result = isc_mutex_init(&lock);
        check_result(result, "isc_mutex_init");
        result = isc_condition_init(&cond);
        check_result(result, "isc_condition_init");
	LOCK(&lock);

        while (ISC_TRUE) {
		reset_system();
                if (!user_interaction())
			break;
		busy = ISC_TRUE;
		start_update();
		while (busy)
			WAIT(&cond, &lock);
        }

        fprintf(stdout, "\n");
        isc_mutex_destroy(&lock);
        isc_condition_destroy(&cond);
        cleanup();

        return (0);
}
