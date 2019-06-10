/*
     Mobile App  WISA Server

     Description:
        Keep reading/writing command/responses from the socket 
        and echoing to USB TX.

        usage:  maws [-d] [-p port]
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <time.h>
#include <stdio.h>
#include <stdlib.h>      /* needed for os x */
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>      /* for memset */
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>   /* for printing an internet address in a user-friendly way */
#include <netinet/in.h>
#include <sys/errno.h>   /* defines ERESTART, EINTR */
#include <sys/wait.h>    /* defines WNOHANG, for wait() */

#include <fcntl.h>
#include <syslog.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>

#include <libusb.h>

#include "port.h"       /* defines default port */
#include "Message.h"

#ifndef ERESTART
#define ERESTART EINTR
#endif

#define USB_VENDOR_ID  0x2495
#define USB_PRODUCT_ID 0x0016

#define CTRL_IN     (LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_ENDPOINT)
#define CTRL_OUT    (LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_ENDPOINT_OUT | LIBUSB_RECIPIENT_ENDPOINT)
#define MEM_RQ      0x03

extern int errno;

static AvahiEntryGroup *group = NULL;
static AvahiSimplePoll *simple_poll = NULL;
static char *name = NULL;
static int32_t port = SERVICE_PORT;

// Forward Declarations
void serve(int32_t port);   /* main server function */
static void create_services(AvahiClient *c);

// Structures
#pragma pack(1)
typedef struct _netmsg_struct
{
    uint8_t readWrite;
    uint16_t length;
    uint32_t notifications;
    MESSAGE payload;
} NET_MESSAGE;
#pragma pack()

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
    assert(g == group || group == NULL);
    group = g;
    /* Called whenever the entry group state changes */
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            syslog(LOG_ERR, "Service '%s' successfully established.\n", name);
            break;
        case AVAHI_ENTRY_GROUP_COLLISION : {
            char *n;
            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(name);
            avahi_free(name);
            name = n;
            syslog(LOG_ERR, "Service name collision, renaming service to '%s'\n", name);
            /* And recreate the services */
            create_services(avahi_entry_group_get_client(g));
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE :
            syslog(LOG_ERR, "Entry group failure: %s\n", avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
            /* Some kind of failure happened while we were registering our services */
            avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

static void create_services(AvahiClient *c) {
    char *n;
    int ret;
    assert(c);
    /* If this is the first time we're called, let's create a new
     * entry group if necessary */
    if (!group)
        if (!(group = avahi_entry_group_new(c, entry_group_callback, NULL))) {
            syslog(LOG_ERR, "avahi_entry_group_new() failed: %s\n", avahi_strerror(avahi_client_errno(c)));
            goto fail;
        }
    /* If the group is empty (either because it was just created, or
     * because it was reset previously, add our entries.  */
    if (avahi_entry_group_is_empty(group)) {
        syslog(LOG_ERR, "Adding service '%s'\n", name);

        /* Add the service for WISA */
        if ((ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, name, "_wisa._tcp", NULL, NULL, port, NULL, NULL, NULL)) < 0) {
            if (ret == AVAHI_ERR_COLLISION)
                goto collision;
            syslog(LOG_ERR, "Failed to add _wisa._tcp service: %s\n", avahi_strerror(ret));
            goto fail;
        }
        /* Tell the server to register the service */
        if ((ret = avahi_entry_group_commit(group)) < 0) {
            syslog(LOG_ERR, "Failed to commit entry group: %s\n", avahi_strerror(ret));
            goto fail;
        }
    }
    return;

collision:
    /* A service name collision with a local service happened. Let's
     * pick a new name */
    n = avahi_alternative_service_name(name);
    avahi_free(name);
    name = n;
    syslog(LOG_ERR, "Service name collision, renaming service to '%s'\n", name);
    avahi_entry_group_reset(group);
    create_services(c);
    return;
fail:
    avahi_simple_poll_quit(simple_poll);
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    assert(c);
    /* Called whenever the client or server state changes */
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            /* The server has startup successfully and registered its host
             * name on the network, so it's time to create our services */
            create_services(c);
            break;
        case AVAHI_CLIENT_FAILURE:
            syslog(LOG_ERR, "Client failure: %s\n", avahi_strerror(avahi_client_errno(c)));
            avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_CLIENT_S_COLLISION:
            /* Let's drop our registered services. When the server is back
             * in AVAHI_SERVER_RUNNING state we will register them
             * again with the new host name. */
        case AVAHI_CLIENT_S_REGISTERING:
            /* The server records are now being established. This
             * might be caused by a host name change. We need to wait
             * for our own records to register until the host name is
             * properly esatblished. */
            if (group)
                avahi_entry_group_reset(group);
            break;
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

static void server_callback(AVAHI_GCC_UNUSED AvahiTimeout *e, void *userdata) {
    AvahiClient *client = userdata;

    // start server
    serve(port);
}

int main(int argc, char **argv)
{
    extern char *optarg;
    extern int optind;
    int32_t c, err = 0; 
    uint8_t *room = NULL;

    AvahiClient *client = NULL;
    int error;
    int ret = 1;
    struct timeval tv;

    static char usage[] = "usage: %s [-d] [-p port]\n";

    while ((c = getopt(argc, argv, "dpn:")) != -1) {
       switch (c) {
           case 'p':
               port = atoi(optarg);
               if (port < 1024 || port > 65535) {
                   syslog(LOG_ERR, "invalid port number: %s\n", optarg);
                   err = 1;
               }
               break;
           case 'n':
	       room = optarg;
               break;
           case '?':
               err = 1;
               break;
       }
    }

    if (err || (optind < argc)) {
        fprintf(stderr, usage, argv[0]);
        exit(1);
    }


    setlogmask (LOG_UPTO (LOG_NOTICE));
    openlog ("maws", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    /* Our process ID and Session ID */
    pid_t pid, sid;
    
    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }

    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);
    
            
    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }

    FILE* pid_file = fopen("/run/maws.pid", "w");
    fprintf(pid_file, "%d", sid);
    fclose(pid_file);
            
    /* Open any logs here */        

    
    /* Change the current working directory */
    if ((chdir("/")) < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }
    
    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // ------------------------------------------------------
    // Setup Avahai service
    // ------------------------------------------------------
    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        syslog(LOG_ERR, "Failed to create simple poll object.\n");
        goto fail;
    }

    name = avahi_strdup(room);

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

    /* Check whether creating the client object succeeded */
    if (!client) {
        syslog(LOG_ERR, "Failed to create client: %s\n", avahi_strerror(error));
        goto fail;
    }

    // After 5s ... start server
    avahi_simple_poll_get(simple_poll)->timeout_new(
        avahi_simple_poll_get(simple_poll),
        avahi_elapse_time(&tv, 1000*5, 0),
        server_callback,
        client);

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    ret = 0;


fail:
    /* Cleanup things */
    if (client)
        avahi_client_free(client);
    if (simple_poll)
        avahi_simple_poll_free(simple_poll);
    avahi_free(name);
    return ret;
}

/* serve: set up the service */

void
serve(int32_t port)
{
    uint32_t svc;                   /* listening socket providing service */
    uint32_t rqst;                  /* socket accepting the request */
    socklen_t alen;                 /* length of address structure */
    struct sockaddr_in my_addr;     /* address of this service */
    struct sockaddr_in client_addr; /* client's address */
    uint32_t sockoptval = 1;
    uint8_t hostname[128];           /* host name, for debugging */
    NET_MESSAGE message;
    int32_t bytes_written;
    int32_t bytes_read;

    int32_t rval;
    struct libusb_device_handle *devh = NULL;

    gethostname((char *)hostname, 128);

    // Open USB TX
    if (libusb_init(NULL) < 0) {
        syslog(LOG_ERR, "cannot initialize USB interface\n");
        exit(1);
    }
    if ((devh = libusb_open_device_with_vid_pid(NULL, USB_VENDOR_ID, USB_PRODUCT_ID)) <= 0) {
        syslog(LOG_ERR, "cannot open USB device\n");
        exit(1);
    }

    /* get a tcp/ip socket */
    /*   AF_INET is the Internet address (protocol) family  */
    /*   with SOCK_STREAM we ask for a sequenced, reliable, two-way */
    /*   conenction based on byte streams.  With IP, this means that */
    /*   TCP will be used */
    if ((svc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "cannot create socket\n");
        exit(1);
    }

    /* we use setsockopt to set SO_REUSEADDR. This allows us */
    /* to reuse the port immediately as soon as the service exits. */
    /* Some operating systems will not allow immediate reuse */
    /* on the chance that some packets may still be en route */
    /* to the port. */
    setsockopt(svc, SOL_SOCKET, SO_REUSEADDR, &sockoptval, sizeof(int));

    /* set up our address */
    /* htons converts a short integer into the network representation */
    /* htonl converts a long integer into the network representation */
    /* INADDR_ANY is the special IP address 0.0.0.0 which binds the */
    /* transport endpoint to all IP addresses on the machine. */
    memset((char*)&my_addr, 0, sizeof(my_addr));  /* 0 out the structure */
    my_addr.sin_family = AF_INET;   /* address family */
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* bind to the address to which the service will be offered */
    if (bind(svc, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        syslog(LOG_ERR, "bind failed\n");
        exit(1);
    }

    /* set up the socket for listening with a queue length of 5 */
    if (listen(svc, 5) < 0) {
        syslog(LOG_ERR, "listen failed\n");
        exit(1);
    }

    syslog(LOG_INFO, "server started on %s, listening on port %d\n", hostname, port);

    /* loop forever - wait for connection requests and perform the service */
    alen = sizeof(client_addr);     /* length of address */

    for (;;) {
        while ((rqst = accept(svc, (struct sockaddr *)&client_addr, &alen)) < 0) {
            /* we may break out of accept if the system call */
            /* was interrupted. In this case, loop back and */
            /* try again */
            if ((errno != ECHILD) && (errno != ERESTART) && (errno != EINTR)) {
                syslog(LOG_ERR, "accept failed\n");
                exit(1);
            }
        }

        syslog(LOG_INFO,"received a connection from: %s port %d\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        do {
            memset(&message, 0, sizeof(message));
            
            // Read Command from socket
            if ((rval = read(rqst, &message, sizeof(message))) == -1)
                syslog(LOG_ERR, "reading stream message\n");
            if (rval == 0)
                syslog(LOG_INFO, "Ending connection\n");
            else {

                // NET_Write 
                if ( message.readWrite ==  MSG_WRITE) {
                    bytes_written = libusb_control_transfer(devh, CTRL_OUT, MEM_RQ, 0, 0, (unsigned char *)&message.payload, message.length, 0);
                    if (bytes_written < 0) {
                        syslog(LOG_ERR, "USB write Error: %d\n", bytes_written); 
                    }
                    
                // NET_Read
                } else {
                    bytes_written = libusb_control_transfer(devh, CTRL_OUT, MEM_RQ, 0, 0, (unsigned char *)&message.payload, message.length, 0);
                    if (bytes_written == message.length)
                    {
                        bytes_read = libusb_control_transfer(devh, CTRL_IN, MEM_RQ, 0, 0, (unsigned char *)&message.payload, sizeof(message.payload), 0);
                        if (bytes_read < 0) {
                            syslog(LOG_ERR, "USB read Error: %d\n", bytes_read); 
                        }
                        write(rqst, (unsigned char *)&message.payload, bytes_read);
                    }
                }
            }
        } while (rval > 0);

        shutdown(rqst, 2);    /* close the connection */
    }
}
