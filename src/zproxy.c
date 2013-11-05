/*  =========================================================================
    zproxy - convenient zmq_proxy api

    -------------------------------------------------------------------------
    Copyright (c) 1991-2013 iMatix Corporation <www.imatix.com>
    Copyright other contributors as noted in the AUTHORS file.

    This file is part of CZMQ, the high-level C binding for 0MQ:
    http://czmq.zeromq.org.

    This is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the 
    Free Software Foundation; either version 3 of the License, or (at your 
    option) any later version.

    This software is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABIL-
    ITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General 
    Public License for more details.

    You should have received a copy of the GNU Lesser General Public License 
    along with this program. If not, see <http://www.gnu.org/licenses/>.
    =========================================================================
*/

/*
@header
    The zproxy class simplifies working with the zmq_proxy api.
@discuss
@end
*/

#include "../include/czmq.h"

//  Structure of our class

struct _zproxy_t {
    zctx_t *ctx;
    void *pipe;
    int type;
    char frontend_addr [256];
    int frontend_type;
    void *frontend;
    char backend_addr [256];
    int backend_type;
    void *backend;
    char capture_addr [256];
    int capture_type;
    void *capture;
};

//  --------------------------------------------------------------------------
//  zproxy attached thread

static void
s_zproxy_attached (void *args, zctx_t *ctx, void *pipe)
{   
    zproxy_t *self = (zproxy_t*) args;
    self->frontend = zsocket_new (ctx, zproxy_frontend_type (self));
    assert (self->frontend);
    zsocket_bind (self->frontend, zproxy_frontend_addr (self));

    self->backend = zsocket_new (ctx, zproxy_backend_type (self));
    assert (self->backend);
    zsocket_bind (self->backend, zproxy_backend_addr (self));

    self->capture = zsocket_new (ctx, zproxy_capture_type (self));
    assert (self->capture);
    zsocket_bind (self->capture, zproxy_capture_addr (self));

    free (zstr_recv (pipe));
    zstr_send (pipe, "OK");

    zmq_proxy (self->frontend, self->backend, self->capture);
}

//  --------------------------------------------------------------------------
//  Constructor

zproxy_t *
zproxy_new (zctx_t *ctx, int zproxy_type)
{
    assert (zproxy_type > 0 && zproxy_type < 4);
    
    zproxy_t *self;
    self = (zproxy_t *) zmalloc (sizeof (zproxy_t));
    self->ctx = ctx;
    self->type = zproxy_type;
    self->capture_type = ZMQ_PUB;
    
    switch (self->type) {
        case (ZPROXY_QUEUE):
            self->frontend_type = ZMQ_ROUTER;
            self->backend_type = ZMQ_DEALER;
            break;
        case (ZPROXY_FORWARDER):
            self->frontend_type = ZMQ_XSUB;
            self->backend_type = ZMQ_XPUB;
            break;
        case (ZPROXY_STREAMER):
            self->frontend_type = ZMQ_PULL;
            self->backend_type = ZMQ_PUSH;
            break;
        default:
            break;
    }
    return self;
}

//  --------------------------------------------------------------------------
//  Start a zproxy object

int
zproxy_bind (zproxy_t *self, const char *frontend_addr,
        const char *backend_addr, const char *capture_addr)
{
    strncpy (self->frontend_addr, frontend_addr, 255);
    self->frontend_addr[255] = '\0';

    strncpy (self->backend_addr, backend_addr, 255);
    self->frontend_addr[255] = '\0';

    strncpy (self->capture_addr, capture_addr, 255);
    self->frontend_addr[255] = '\0';

    self->pipe = zthread_fork (self->ctx, s_zproxy_attached, self);
    
    int rc = 0;
    zstr_send (self->pipe, "START");
    char *response = zstr_recv (self->pipe);
    if (streq (response, "OK") == 1)
        rc = 1;
    return rc;
}

//  --------------------------------------------------------------------------
//  Destructor

void
zproxy_destroy (zproxy_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zproxy_t *self = *self_p;
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Get the proxy type 

int
zproxy_type (zproxy_t *self)
{
    return self->type;
}


//  --------------------------------------------------------------------------
//  Get the proxy frontend address

char *
zproxy_frontend_addr (zproxy_t *self)
{
    return self->frontend_addr;
}


//  --------------------------------------------------------------------------
//  Get the proxy frontend type

int
zproxy_frontend_type (zproxy_t *self)
{
    return self->frontend_type;
}


//  --------------------------------------------------------------------------
//  Get the proxy backend address

char *
zproxy_backend_addr (zproxy_t *self)
{
    return self->backend_addr;
}


//  --------------------------------------------------------------------------
//  Get the proxy backend type

int
zproxy_backend_type (zproxy_t *self)
{
    return self->backend_type;
}


//  --------------------------------------------------------------------------
//  Get the proxy capture address

char *
zproxy_capture_addr (zproxy_t *self)
{
    return self->capture_addr;
}

//  --------------------------------------------------------------------------
//  Get the proxy capture type

int
zproxy_capture_type (zproxy_t *self)
{
    return self->capture_type;
}


//  --------------------------------------------------------------------------
//  Selftest

int
zproxy_test (bool verbose)
{
    printf (" * zproxy: ");

    //  @selftest
    const char *front_addr = "inproc://proxy_front";
    const char *back_addr = "inproc://proxy_back";
    const char *capture_addr = "inproc://proxy_capture";

    // Create and start the proxy
    zctx_t *ctx = zctx_new ();
    zproxy_t *proxy = zproxy_new (ctx, ZPROXY_STREAMER);
    int rc = zproxy_bind (proxy, front_addr, back_addr, capture_addr);
    assert (rc);

    // Test the accessor methods
    assert (zproxy_type (proxy) == ZPROXY_STREAMER);
    assert (zproxy_frontend_type (proxy) == ZMQ_PULL);
    assert (zproxy_backend_type (proxy) == ZMQ_PUSH);
    assert (zproxy_capture_type (proxy) == ZMQ_PUB);

    char *front_check = zproxy_frontend_addr (proxy);
    assert (streq (front_check, front_addr));
    char *back_check = zproxy_backend_addr (proxy);
    assert (streq (back_check, back_addr));
    char *capture_check = zproxy_capture_addr (proxy);
    assert (streq (capture_check, capture_addr));

    // Connect to the proxy front, back, and capture ports
    void *front_s = zsocket_new (ctx, ZMQ_PUSH);
    assert (front_s);
    zsocket_connect (front_s, zproxy_frontend_addr (proxy));

    void *back_s = zsocket_new (ctx, ZMQ_PULL);
    assert (back_s);
    zsocket_connect (back_s, zproxy_backend_addr (proxy));

    void *capture_s = zsocket_new (ctx, ZMQ_SUB);
    zsocket_set_subscribe (capture_s, "");
    assert (back_s);
    zsocket_connect (capture_s, zproxy_capture_addr (proxy));

    // Send a message through the proxy and receive it
    zstr_send (front_s, "STREAMER_TEST");
    
    char *back_resp = zstr_recv (back_s);
    assert (back_resp);
    assert (streq ("STREAMER_TEST", back_resp));
    
    char *capture_resp = zstr_recv (capture_s);
    assert (capture_resp);
    assert (capture_resp);
    assert (streq ("STREAMER_TEST", capture_resp));
   
    // Destroying the context will stop the proxy
    zctx_destroy (&ctx);
    zproxy_destroy (&proxy);
    
    //  @end
    printf ("OK\n");
    return 0;
}
