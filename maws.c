/*
     Mobile App  WISA Server

     Description:
        Keep reading/writing command/responses from the socket 
        and echoing to USB TX.

        usage:  maws [-d] [-p port]
*/

#include <stdio.h>
#include <stdlib.h>      /* needed for os x */
#include <string.h>      /* for memset */
#include <sys/socket.h>
#include <arpa/inet.h>   /* for printing an internet address in a user-friendly way */
#include <netinet/in.h>
#include <sys/errno.h>   /* defines ERESTART, EINTR */
#include <sys/wait.h>    /* defines WNOHANG, for wait() */
#include <unistd.h>
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

void serve(int32_t port);   /* main server function */

#pragma pack(1)
typedef struct _netmsg_struct
{
    uint8_t readWrite;
    uint16_t length;
    uint32_t notifications;
    MESSAGE payload;
} NET_MESSAGE;
#pragma pack()

int main(int argc, char **argv)
{
    extern char *optarg;
    extern int optind;
    int32_t c, err = 0; 
    int32_t port = SERVICE_PORT;
    static char usage[] = "usage: %s [-d] [-p port]\n";

    while ((c = getopt(argc, argv, "dp:")) != -1) {
       switch (c) {
           case 'p':
               port = atoi(optarg);
               if (port < 1024 || port > 65535) {
                   fprintf(stderr, "invalid port number: %s\n", optarg);
                   err = 1;
               }
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
    serve(port);
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
        perror("cannot initialize USB interface");
        exit(1);
    }
    if ((devh = libusb_open_device_with_vid_pid(NULL, USB_VENDOR_ID, USB_PRODUCT_ID)) <= 0) {
        perror("cannot open USB device");
        exit(1);
    }

    /* get a tcp/ip socket */
    /*   AF_INET is the Internet address (protocol) family  */
    /*   with SOCK_STREAM we ask for a sequenced, reliable, two-way */
    /*   conenction based on byte streams.  With IP, this means that */
    /*   TCP will be used */
    if ((svc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("cannot create socket");
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
        perror("bind failed");
        exit(1);
    }

    /* set up the socket for listening with a queue length of 5 */
    if (listen(svc, 5) < 0) {
        perror("listen failed");
        exit(1);
    }

    printf("server started on %s, listening on port %d\n", hostname, port);

    /* loop forever - wait for connection requests and perform the service */
    alen = sizeof(client_addr);     /* length of address */

    for (;;) {
        while ((rqst = accept(svc, (struct sockaddr *)&client_addr, &alen)) < 0) {
            /* we may break out of accept if the system call */
            /* was interrupted. In this case, loop back and */
            /* try again */
            if ((errno != ECHILD) && (errno != ERESTART) && (errno != EINTR)) {
                perror("accept failed");
                exit(1);
            }
        }

        printf("received a connection from: %s port %d\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        do {
            memset(&message, 0, sizeof(message));
            
            // Read Command from socket
            if ((rval = read(rqst, &message, sizeof(message))) == -1)
                perror("reading stream message");
            if (rval == 0)
                printf("Ending connection\n");
            else {

                // NET_Write 
                if ( message.readWrite ==  MSG_WRITE) {
                    bytes_written = libusb_control_transfer(devh, CTRL_OUT, MEM_RQ, 0, 0, (unsigned char *)&message.payload, message.length, 0);
                    if (bytes_written < 0) {
                        printf("USB write Error: %d\n", bytes_written); 
                    }
                    
                // NET_Read
                } else {
                    bytes_written = libusb_control_transfer(devh, CTRL_OUT, MEM_RQ, 0, 0, (unsigned char *)&message.payload, message.length, 0);
                    if (bytes_written == message.length)
                    {
                        bytes_read = libusb_control_transfer(devh, CTRL_IN, MEM_RQ, 0, 0, (unsigned char *)&message.payload, sizeof(message.payload), 0);
                        if (bytes_read < 0) {
                            printf("USB read Error: %d\n", bytes_read); 
                        }
                        write(rqst, (unsigned char *)&message.payload, bytes_read);
                    }
                }
            }
        } while (rval > 0);

        shutdown(rqst, 2);    /* close the connection */
    }
}
