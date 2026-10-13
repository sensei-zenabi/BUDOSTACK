#ifndef BUDOWIN_FLOW_MODEL_H
#define BUDOWIN_FLOW_MODEL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FLOW_DIAGRAMS 8
#define FLOW_NODES 64
#define FLOW_EDGES 96
#define FLOW_ELEMENTS 16
#define FLOW_TEMPLATES 4
#define FLOW_HISTORY 24
#define FLOW_W 1200
#define FLOW_H 800
#define FLOW_LABEL 64
#define FLOW_DESCRIPTION 256

enum {
    FLOW_PROCESS, FLOW_DECISION, FLOW_TERMINATOR, FLOW_STATE, FLOW_BLOCK, FLOW_PARTICIPANT, FLOW_NOTE, FLOW_SHAPES
};
enum {
    FLOW_CHART, FLOW_SEQUENCE, FLOW_MACHINE, FLOW_SYSTEM, FLOW_CONTROL, FLOW_TYPES
};
static const char *flow_shapes[] = {"Process", "Decision", "Start / End", "State", "Block", "Participant", "Note"};
static const char *flow_types[] = {"Flowchart", "Sequence", "State machine", "System blocks", "Control signals"};
typedef struct FlowNode {
    int id, shape, x, y, w, h;
    char label[FLOW_LABEL], description[FLOW_DESCRIPTION];
} FlowNode;
typedef struct FlowEdge {
    int id, from, to, lane;
    char label[FLOW_LABEL], description[FLOW_DESCRIPTION];
} FlowEdge;
typedef struct FlowDiagram {
    int type, node_count, edge_count, next_id;
    char name[FLOW_LABEL], description[FLOW_DESCRIPTION];
    FlowNode nodes[FLOW_NODES];
    FlowEdge edges[FLOW_EDGES];
} FlowDiagram;
typedef struct FlowProject {
    int count, element_count, template_count;
    FlowDiagram diagrams[FLOW_DIAGRAMS], templates[FLOW_TEMPLATES];
    FlowNode elements[FLOW_ELEMENTS];
} FlowProject;

static int flow_min(int a, int b) {
    return a < b ? a : b;
}
static int flow_max(int a, int b) {
    return a > b ? a : b;
}
static int flow_abs(int a) {
    return a < 0 ? -a : a;
}
static int flow_clamp(int value, int low, int high) {
    return flow_min(high, flow_max(low, value));
}
static int flow_snap(int value) {
    return value < 0 ? -((-value + 5) / 10 * 10) : (value + 5) / 10 * 10;
}
static void flow_string(char *dest, size_t size, const char *source)
{
    if (!size) return;
    size_t length = strlen(source);
    if (length >= size) length = size - 1;
    memmove(dest, source, length);
    memset(dest + length, 0, size - length);
}
static int flow_node_index(const FlowDiagram *d, int id)
{
    for (int i = 0; i < d->node_count; ++i) {
        if (d->nodes[i].id == id) return i;
    }
    return -1;
}
static int flow_add_node(FlowDiagram *d, int shape, int x, int y, const char *label)
{
    if (d->node_count == FLOW_NODES || d->next_id >= 1000000) return -1;
    int index = d->node_count++;
    FlowNode *n = &d->nodes[index];
    memset(n, 0, sizeof(*n));
    n->id = d->next_id++;
    n->shape = shape;
    n->w = shape == FLOW_DECISION ? 120 : 100;
    n->h = shape == FLOW_DECISION ? 70 : 50;
    n->x = flow_clamp(flow_snap(x), 20, FLOW_W - n->w - 20);
    n->y = flow_clamp(flow_snap(y), 20, FLOW_H - n->h - 20);
    flow_string(n->label, sizeof(n->label), label);
    return index;
}
static int flow_add_edge(FlowDiagram *d, int from, int to, const char *label)
{
    if (d->edge_count == FLOW_EDGES || d->next_id >= 1000000 ||
        flow_node_index(d, from) < 0 || flow_node_index(d, to) < 0) return -1;
    FlowEdge *e = &d->edges[d->edge_count];
    memset(e, 0, sizeof(*e));
    e->id = d->next_id++;
    e->from = from;
    e->to = to;
    e->lane = flow_min(720, 150 + d->edge_count * 50);
    flow_string(e->label, sizeof(e->label), label);
    return d->edge_count++;
}
static void flow_blank(FlowDiagram *d, int type, const char *name)
{
    memset(d, 0, sizeof(*d));
    d->type = type;
    d->next_id = 1;
    flow_string(d->name, sizeof(d->name), name);
}
static void flow_starter(FlowDiagram *d, int type)
{
    flow_blank(d, type, flow_types[type]);
    if (type == FLOW_CHART) {
        flow_add_node(d, FLOW_TERMINATOR, 150, 40, "Start");
        flow_add_node(d, FLOW_PROCESS, 150, 140, "Do work");
        flow_add_node(d, FLOW_DECISION, 140, 250, "Ready?");
        flow_add_node(d, FLOW_TERMINATOR, 340, 260, "End");
        flow_add_edge(d, d->nodes[0].id, d->nodes[1].id, "");
        flow_add_edge(d, d->nodes[1].id, d->nodes[2].id, "");
        flow_add_edge(d, d->nodes[2].id, d->nodes[3].id, "Yes");
        flow_add_edge(d, d->nodes[2].id, d->nodes[1].id, "No");
    } else if (type == FLOW_SEQUENCE) {
        flow_add_node(d, FLOW_PARTICIPANT, 50, 40, "Controller");
        flow_add_node(d, FLOW_PARTICIPANT, 260, 40, "Device");
        flow_add_edge(d, d->nodes[0].id, d->nodes[1].id, "Request");
        flow_add_edge(d, d->nodes[1].id, d->nodes[0].id, "Response");
    } else if (type == FLOW_MACHINE) {
        flow_add_node(d, FLOW_STATE, 40, 100, "Idle");
        flow_add_node(d, FLOW_STATE, 230, 100, "Running");
        flow_add_node(d, FLOW_STATE, 230, 240, "Fault");
        flow_add_edge(d, d->nodes[0].id, d->nodes[1].id, "Start");
        flow_add_edge(d, d->nodes[1].id, d->nodes[0].id, "Stop");
        flow_add_edge(d, d->nodes[1].id, d->nodes[2].id, "Error");
        flow_add_edge(d, d->nodes[2].id, d->nodes[0].id, "Reset");
    } else {
        const char *labels[2][3] = {{"Sensor", "MCU", "Actuator"}, {"Setpoint", "Controller", "Plant"}};
        for (int i = 0; i < 3; ++i) flow_add_node(d, FLOW_BLOCK, 40 + i * 180, 100, labels[type - FLOW_SYSTEM][i]);
        flow_add_edge(d, d->nodes[0].id, d->nodes[1].id, type == FLOW_SYSTEM ? "SPI" : "Error");
        flow_add_edge(d, d->nodes[1].id, d->nodes[2].id, type == FLOW_SYSTEM ? "PWM" : "Control");
        if (type == FLOW_CONTROL) flow_add_edge(d, d->nodes[2].id, d->nodes[1].id, "Feedback");
    }
}

/* Orthogonal visibility routing preserves exact ports and avoids node interiors. */
#define FLOW_ROUTE_POINTS 96
typedef struct FlowPoint {
    int x, y;
} FlowPoint;
typedef struct FlowRoute {
    int count, blocked;
    FlowPoint points[FLOW_ROUTE_POINTS];
} FlowRoute;
static void flow_route_point(FlowRoute *route, int x, int y)
{
    int n = route->count;
    if (n && route->points[n - 1].x == x && route->points[n - 1].y == y) return;
    if (n >= 2 && ((route->points[n - 2].x == x && route->points[n - 1].x == x) ||
                  (route->points[n - 2].y == y && route->points[n - 1].y == y))) {
        route->points[n - 1] = (FlowPoint){
            x, y
        };
    } else if (n < FLOW_ROUTE_POINTS) route->points[route->count++] = (FlowPoint){
    x, y
    };
    else route->blocked = 1;
}
static FlowPoint flow_port(const FlowNode *n, int side)
{
    if (side == 0) return (FlowPoint){
        n->x + n->w / 2, n->y
    };
    if (side == 1) return (FlowPoint){
        n->x + n->w, n->y + n->h / 2
    };
    if (side == 2) return (FlowPoint){
        n->x + n->w / 2, n->y + n->h
    };
    return (FlowPoint){
        n->x, n->y + n->h / 2
    };
}
#include "flow_routing.h"
static void flow_route(const FlowDiagram *d, const FlowEdge *e, FlowRoute *route)
{
    memset(route, 0, sizeof(*route));
    const FlowNode *a = &d->nodes[flow_node_index(d, e->from)];
    const FlowNode *b = &d->nodes[flow_node_index(d, e->to)];
    if (d->type == FLOW_SEQUENCE && a->shape == FLOW_PARTICIPANT && b->shape == FLOW_PARTICIPANT) {
        int ax = a->x + a->w / 2, bx = b->x + b->w / 2;
        int lane = flow_max(e->lane, flow_max(a->y + a->h, b->y + b->h) + 30);
        flow_route_point(route, ax, lane);
        if (a == b) {
            flow_route_point(route, ax + 40, lane);
            flow_route_point(route, ax + 40, lane + 20);
            lane += 20;
        }
        flow_route_point(route, bx, lane);
        return;
    }
    int dx = b->x + b->w / 2 - a->x - a->w / 2;
    int dy = b->y + b->h / 2 - a->y - a->h / 2;
    int side = flow_abs(dx) > flow_abs(dy) ? (dx >= 0 ? 1 : 3) : (dy >= 0 ? 2 : 0);
    int other = a == b ? 0 : (side + 2) % 4;
    for (int i = 0; i < d->edge_count && &d->edges[i] != e; ++i) {
        if ((d->edges[i].from == e->to && d->edges[i].to == e->from) ||
            (d->edges[i].from == e->from && d->edges[i].to == e->to)) {
            side = other = flow_abs(dx) > flow_abs(dy) ? 2 : 3;
            break;
        }
    }
    FlowPoint start = flow_port(a, side), end = flow_port(b, other);
    static const int sx[] = {0, 12, 0, -12}, sy[] = {-12, 0, 12, 0};
    FlowPoint first = {start.x + sx[side], start.y + sy[side]};
    FlowPoint last = {end.x + sx[other], end.y + sy[other]};
    flow_route_point(route, start.x, start.y);
    if (!flow_visibility_route(d, first, last, side, other, route)) {
        route->count = 0;
        route->blocked = 1;
        flow_route_point(route, start.x, start.y);
        flow_route_point(route, start.x, end.y);
    }
    flow_route_point(route, end.x, end.y);
}
#endif
