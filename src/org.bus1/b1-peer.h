#pragma once

/***
  This file is part of bus1. See COPYING for details.

  bus1 is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  bus1 is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bus1; If not, see <http://www.gnu.org/licenses/>.
***/

/*
 * XXX
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct B1Handle B1Handle;
typedef struct B1Interface B1Interface;
typedef struct B1Message B1Message;
typedef struct B1Node B1Node;
typedef struct B1Subscription B1Subscription;
typedef struct B1Peer B1Peer;
typedef struct B1ReplySlot B1ReplySlot;

typedef int (*B1NodeFn) (B1Node *node, void *userdata, B1Message *message);
typedef int (*B1SubscriptionFn) (B1Subscription *subscription, void *userdata, B1Handle *handle);
typedef int (*B1ReplySlotFn) (B1ReplySlot *slot, void *userdata, B1Message *message);

/* peers */

int b1_peer_new(B1Peer **peerp, const char *path);
int b1_peer_new_from_fd(B1Peer **peerp, int fd);
B1Peer *b1_peer_ref(B1Peer *peer);
B1Peer *b1_peer_unref(B1Peer *peer);

int b1_peer_get_fd(B1Peer *peer);

int b1_peer_recv(B1Peer *peer, B1Message **messagep);
int b1_peer_recv_seed(B1Peer *peer, B1Message **seedp);
int b1_peer_clone(B1Peer *peer, B1Peer **childp, B1Handle *handle, B1Handle **child_handlep);

int b1_peer_implement(B1Peer *peer, B1Node **nodep, void *userdata, B1Interface *interface);

/* slots */

B1ReplySlot *b1_reply_slot_free(B1ReplySlot *slot);
void *b1_reply_slot_get_userdata(B1ReplySlot *slot);

/* subscriptions */

B1Subscription *b1_subscription_free(B1Subscription *subscription);
void *b1_subscription_get_userdata(B1Subscription *subscription);

/* messages */

enum {
        B1_MESSAGE_TYPE_NODE_DESTROY,
        B1_MESSAGE_TYPE_CALL,
        B1_MESSAGE_TYPE_REPLY,
        B1_MESSAGE_TYPE_ERROR,
        B1_MESSAGE_TYPE_SEED,
        _B1_MESSAGE_TYPE_N,
        _B1_MESSAGE_TYPE_INVALID = -1,
};

int b1_message_new_call(B1Peer *peer,
                        B1Message **messagep,
                        const char *interface,
                        const char *member,
                        const char *signature_input,
                        const char *signature_output,
                        B1ReplySlot **slotp,
                        B1ReplySlotFn fn,
                        void *userdata);
int b1_message_new_reply(B1Peer *peer,
                         B1Message **messagep,
                         const char *signature);
int b1_message_new_error(B1Peer *peer,
                         B1Message **messagep,
                         const char *name,
                         const char *signature);
int b1_message_new_seed(B1Peer *peer,
                        B1Message **messagep,
                        B1Node **nodes,
                        const char **node_names,
                        size_t n_nodes,
                        const char *signature);
B1Message *b1_message_ref(B1Message *message);
B1Message *b1_message_unref(B1Message *message);

bool b1_message_is_sealed(B1Message *message);
unsigned int b1_message_get_type(B1Message *message);

int b1_message_dispatch(B1Message *message);
int b1_message_send(B1Message *message, B1Handle **handles, size_t n_handles);

B1Handle *b1_message_get_reply_handle(B1Message *message);
uid_t b1_message_get_uid(B1Message *message);
gid_t b1_message_get_gid(B1Message *message);
pid_t b1_message_get_pid(B1Message *message);
pid_t b1_message_get_tid(B1Message *message);

size_t b1_message_peek_count(B1Message *message);
const char *b1_message_peek_type(B1Message *message, size_t *sizep);
int b1_message_enter(B1Message *message, const char *containers);
int b1_message_exit(B1Message *message, const char *containers);
int b1_message_readv(B1Message *message, const char *signature, va_list args);
void b1_message_rewind(B1Message *message);

int b1_message_beginv(B1Message *message, const char *containers, va_list args);
int b1_message_end(B1Message *message, const char *containers);
int b1_message_writev(B1Message *message, const char *signature, va_list args);
int b1_message_insert(B1Message *message, const char *type, const struct iovec *vecs, size_t n_vecs);
int b1_message_seal(B1Message *message);

int b1_message_append_handle(B1Message *message, B1Handle *handle);
int b1_message_append_fd(B1Message *message, int fd);
int b1_message_get_handle(B1Message *message, unsigned int index, B1Handle **handlep);
int b1_message_get_fd(B1Message *message, unsigned int index, int *fdp);

/* nodes */

int b1_node_new(B1Peer *peer, B1Node **nodep, void *userdata);
B1Node *b1_node_free(B1Node *node);

B1Peer *b1_node_get_peer(B1Node *node);
B1Handle *b1_node_get_handle(B1Node *node);
void *b1_node_get_userdata(B1Node *node);

void b1_node_set_destroy_fn(B1Node *node, B1NodeFn fn);
int b1_node_implement(B1Node *node, B1Interface *interface);

void b1_node_release(B1Node *node);
void b1_node_destroy(B1Node *node);

/* handles */

B1Handle *b1_handle_ref(B1Handle *handle);
B1Handle *b1_handle_unref(B1Handle *handle);

B1Peer *b1_handle_get_peer(B1Handle *handle);

int b1_handle_subscribe(B1Handle *handle, B1Subscription **subscriptionp, B1SubscriptionFn fn, void *userdata);

/* interfaces */

int b1_interface_new(B1Interface **interfacep, const char *name);
B1Interface *b1_interface_ref(B1Interface *interface);
B1Interface *b1_interface_unref(B1Interface *interface);

int b1_interface_add_member(B1Interface *interface,
                            const char *name,
                            const char *type_input,
                            const char *type_output,
                            B1NodeFn fn);

/* convenience */

int b1_peer_export_to_environment(B1Peer *peer);
int b1_peer_new_from_environment(B1Peer **peerp);
int b1_message_reply(B1Message *origin, B1Message *reply);

/* inline helpers */

static inline void b1_peer_unrefp(B1Peer **peer) {
        if (*peer)
                b1_peer_unref(*peer);
}

static inline void b1_message_unrefp(B1Message **message) {
        if (*message)
                b1_message_unref(*message);
}

static inline void b1_node_freep(B1Node **node) {
        if (*node)
                b1_node_free(*node);
}

static inline void b1_reply_slot_freep(B1ReplySlot **slot) {
        if (*slot)
                b1_reply_slot_free(*slot);
}

static inline void b1_subscription_freep(B1Subscription **subscription) {
        if (*subscription)
                b1_subscription_free(*subscription);
}

static inline void b1_handle_unrefp(B1Handle **handle) {
        if (*handle)
                b1_handle_unref(*handle);
}

static inline void b1_interface_unrefp(B1Interface **interface) {
        if (*interface)
                b1_interface_unref(*interface);
}

static inline int b1_message_read(B1Message *message,
                                  const char *signature, ...) {
        va_list args;
        int r;

        va_start(args, signature);
        r = b1_message_readv(message, signature, args);
        va_end(args);
        return r;
}

static inline int b1_message_begin(B1Message *message,
                                   const char *containers, ...) {
        va_list args;
        int r;

        va_start(args, containers);
        r = b1_message_beginv(message, containers, args);
        va_end(args);
        return r;
}

static inline int b1_message_write(B1Message *message,
                                   const char *signature, ...) {
        va_list args;
        int r;

        va_start(args, signature);
        r = b1_message_writev(message, signature, args);
        va_end(args);
        return r;
}

#ifdef __cplusplus
}
#endif
