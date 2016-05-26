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

#include <c-macro.h>
#include <c-rbtree.h>
#include <org.bus1/b1-peer.h>
#include <string.h>

typedef struct Manager {
        unsigned n_ref;
        B1Peer *peer;
        CRBTree components; /* currently only used as a list */
        CRBTree dependencies;
        B1Interface *component_interface;
} Manager;

typedef struct Component {
        Manager *manager;
        const char *name;
        CRBNode rb;
        B1Peer *peer;
        B1Node *node;
        B1Handle *handle;
        char **dependencies;
        size_t n_dependencies;
} Component;

typedef struct Dependency {
        Manager *manager;
        const char *name;
        CRBNode rb;
        B1Handle *handle;
} Dependency;

static Manager *manager_unref(Manager *m) {
        if (!m)
                return NULL;

        assert(m->n_ref > 0);
        -- m->n_ref;

        if (m->n_ref != 0)
                return NULL;

        assert(!c_rbtree_first(&m->components));
        assert(!c_rbtree_first(&m->dependencies));
        b1_interface_unref(m->component_interface);
        b1_peer_unref(m->peer);

        free(m);

        return NULL;
}

static void manager_unrefp(Manager **m) {
        (void)manager_unref(*m);
}

static Manager *manager_ref(Manager *m) {
        if (!m)
                return NULL;

        assert(m->n_ref > 0);

        ++ m->n_ref;

        return m;
}

static int component_set_root_nodes(B1Node *node, void *userdata, B1Message *message);
static int component_get_dependencies(B1Node *node, void *userdata, B1Message *message);

static int manager_new(Manager **managerp) {
        _c_cleanup_(manager_unrefp) Manager *manager = NULL;
        int r;

        assert(managerp);

        manager = calloc(1, sizeof(*manager));
        if (!manager)
                return -ENOMEM;

        manager->n_ref = 1;

        r = b1_peer_new(&manager->peer, NULL);
        if (r < 0)
                return r;

        r = b1_interface_new(&manager->component_interface, "org.bus1.Activator.Component");
        if (r < 0)
                return r;

        r = b1_interface_add_member(manager->component_interface,
                                    "setRootNodes", "a(su)", "()", component_set_root_nodes);
        if (r < 0)
                return r;

        r = b1_interface_add_member(manager->component_interface,
                                    "getDependencies", "()", "a(su)", component_get_dependencies);
        if (r < 0)
                return r;

        *managerp = manager;
        manager = NULL;
        return 0;
}

static void component_free(Component *c) {
        if (!c)
                return;

        c_rbtree_remove_init(&c->manager->components, &c->rb);

        manager_unref(c->manager);
        b1_peer_unref(c->peer);
        b1_node_free(c->node);
        b1_handle_unref(c->handle);

        free(c);
}

static void component_freep(Component **c) {
        component_free(*c);
}

static int components_compare(CRBTree *t, void *k, CRBNode *n) {
        Component *c = c_container_of(n, Component, rb);
        const char *name = k;

        return strcmp(name, c->name);
}

static int component_new(Manager *manager, Component **componentp, const char *name,
                         const char **dependencies, size_t n_dependencies) {
        _c_cleanup_(component_freep) Component *component = NULL;
        size_t n_names;
        CRBNode **slot, *p;
        char *buf;
        int r;

        assert(manager);
        assert(componentp);
        assert(name);
        assert(n_dependencies == 0 || dependencies);

        slot = c_rbtree_find_slot(&manager->components, components_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_names = strlen(name) + 1;
        for (unsigned int i = 0; i < n_dependencies; ++i)
                n_names += strlen(dependencies[i]) + 1;

        component = calloc(1, sizeof(*component) + sizeof(char*) * n_dependencies + n_names);
        if (!component)
                return -ENOMEM;
        component->name = (void*)(component + 1);
        component->n_dependencies = n_dependencies;
        component->dependencies = (char **)(stpcpy((char*)component->name, name) + 1);
        buf = (char*)(component->dependencies + n_dependencies);
        for (unsigned int i = 0; i < n_dependencies; ++i) {
                component->dependencies[i] = buf;
                buf = stpcpy(buf, name) + 1;
        }

        c_rbnode_init(&component->rb);
        component->manager = manager_ref(manager);

        r = b1_node_new(manager->peer, &component->node, component);
        if (r < 0)
                return r;

        r = b1_node_implement(component->node, manager->component_interface);
        if (r < 0)
                return r;

        r = b1_peer_clone(manager->peer, &component->peer,
                          b1_node_get_handle(component->node),
                          &component->handle);
        if (r < 0)
                return r;

        c_rbtree_add(&manager->components, p, slot, &component->rb);

        *componentp = component;
        component = NULL;
        return 0;
}

static void dependency_free(Dependency *d) {
        if (!d)
                return;

        c_rbtree_remove_init(&d->manager->dependencies, &d->rb);

        manager_unref(d->manager);
        b1_handle_unref(d->handle);

        free(d);
}

static void dependency_freep(Dependency **d) {
        dependency_free(*d);
}

static int dependencies_compare(CRBTree *t, void *k, CRBNode *n) {
        Dependency *d = c_container_of(n, Dependency, rb);
        const char *name = k;

        return strcmp(name, d->name);
}

static int dependency_new(Manager *manager, Dependency **dependencyp, const char *name, B1Handle *handle) {
        _c_cleanup_(dependency_freep) Dependency *dependency = NULL;
        size_t n_name;
        CRBNode **slot, *p;

        assert(manager);
        assert(name);
        assert(handle);

        slot = c_rbtree_find_slot(&manager->dependencies, dependencies_compare, name, &p);
        if (!slot)
                return -ENOTUNIQ;

        n_name = strlen(name) + 1;
        dependency = calloc(1, sizeof(*dependency) + n_name);
        if (!dependency)
                return -ENOMEM;
        memcpy((void*)(dependency + 1), name, n_name);

        c_rbnode_init(&dependency->rb);
        dependency->manager = manager_ref(manager);
        dependency->handle = b1_handle_ref(handle);
        c_rbtree_add(&manager->dependencies, p, slot, &dependency->rb);

        if (dependencyp)
                *dependencyp = dependency;
        dependency = NULL;
        return 0;
}

static Dependency *dependency_get(Manager *manager, const char *name) {
        CRBNode *n;

        n = c_rbtree_find_node(&manager->dependencies, dependencies_compare, name);
        if (!n)
                return NULL;

        return c_container_of(n, Dependency, rb);
}

int component_set_root_nodes(B1Node *node, void *userdata, B1Message *message) {
        Component *component = userdata;
        unsigned int n;
        int r;

        assert(component);

        r = b1_message_enter(message, "a");
        if (r < 0)
                return r;

        r = b1_message_peek_count(message);
        if (r < 0)
                return r;
        else
                n = r;

        for (unsigned int i = 0; i < n; ++i) {
                const char *name;
                uint32_t handle_id;
                B1Handle *handle;

                r = b1_message_read(message, "(su)", &name, &handle_id);
                if (r < 0)
                        return r;

                r = b1_message_get_handle(message, handle_id, &handle);
                if (r < 0)
                        return r;

                r = dependency_new(component->manager, NULL, name, handle);
                if (r < 0)
                        return r;
        }

        r = b1_message_exit(message, "a");
        if (r < 0)
                return r;

        return 0;
}

int component_get_dependencies(B1Node *node, void *userdata, B1Message *message) {
        Component *component = userdata;
        _c_cleanup_(b1_message_unrefp) B1Message *reply = NULL;
        int r;

        assert(component);

        r = b1_message_new_reply(component->manager->peer, &reply, "a(su)");
        if (r < 0)
                return r;

        r = b1_message_begin(reply, "a");
        if (r < 0)
                return r;

        for (unsigned int i = 0; i < component->n_dependencies; ++i) {
                Dependency *dep;
                uint32_t handle;

                dep = dependency_get(component->manager, component->dependencies[i]);
                if (!dep)
                        return -ENOENT;

                r = b1_message_append_handle(message, dep->handle);
                if (r < 0)
                        return r;
                else
                        handle = r;

                r = b1_message_write(reply, "(su)", dep->name, handle);
                if (r < 0)
                        return r;
        }

        r = b1_message_end(reply, "a");
        if (r < 0)
                return r;

        return b1_message_reply(message, reply);
}

int main(void) {
        _c_cleanup_(manager_unrefp) Manager *manager = NULL;
        _c_cleanup_(component_freep) Component *foo = NULL, *bar = NULL;
        const char *foo_deps[] = { "org.bus1.bar", "org.bus1.baz" };
        int r;

        r = manager_new(&manager);
        if (r < 0)
                return EXIT_FAILURE;

        r = component_new(manager, &foo, "org.bus1.foo", foo_deps, 2);
        if (r < 0)
                return r;

        r = component_new(manager, &bar, "org.bus1.bar", NULL, 0);
        if (r < 0)
                return r;

        return EXIT_SUCCESS;
}