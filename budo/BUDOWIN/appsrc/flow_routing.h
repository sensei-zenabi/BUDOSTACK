#ifndef BUDOWIN_FLOW_ROUTING_H
#define BUDOWIN_FLOW_ROUTING_H
/* Coordinate-compressed orthogonal visibility graph. A* retains incoming
 * direction, minimizing path length plus a 24-pixel penalty per bend.
 * Coordinates include exact ports and obstacle-clearance boundaries. */
#define FLOW_AXES (FLOW_NODES * 2 + 6)
#define FLOW_GRAPH (FLOW_AXES * FLOW_AXES)
#define FLOW_STATES (FLOW_GRAPH * 4)
static int route_x[FLOW_AXES], route_y[FLOW_AXES], route_nx, route_ny, route_goal;
static int route_cost[FLOW_STATES], route_parent[FLOW_STATES];
static int route_heap[FLOW_STATES], route_heap_at[FLOW_STATES], route_heap_count;
static unsigned char route_blocked[FLOW_GRAPH], route_horizontal[FLOW_GRAPH], route_vertical[FLOW_GRAPH];
static int route_reverse[FLOW_STATES];
static int route_coordinate(int *axis, int *count, int value, int limit)
{
    value = flow_clamp(value, 0, limit);
    int at = 0;
    while (at < *count && axis[at] < value) ++at;
    if (at < *count && axis[at] == value) return at;
    if (*count >= FLOW_AXES) return -1;
    memmove(axis + at + 1, axis + at, (size_t)(*count - at) * sizeof(int));
    axis[at] = value;
    ++*count;
    return at;
}
static int route_index(const int *axis, int count, int value)
{
    for (int i = 0; i < count; ++i) if (axis[i] == value) return i;
    return -1;
}
static int route_estimate(int state)
{
    int cell = state / 4;
    return route_cost[state] + flow_abs(route_x[cell % route_nx] - route_x[route_goal % route_nx]) +
        flow_abs(route_y[cell / route_nx] - route_y[route_goal / route_nx]);
}
static int route_less(int a, int b)
{
    int fa = route_estimate(a), fb = route_estimate(b);
    return fa < fb || (fa == fb && route_cost[a] > route_cost[b]);
}
static void route_heap_swap(int a, int b)
{
    int temp = route_heap[a];
    route_heap[a] = route_heap[b];
    route_heap[b] = temp;
    route_heap_at[route_heap[a]] = a;
    route_heap_at[route_heap[b]] = b;
}
static void route_heap_push(int state)
{
    int at = route_heap_at[state];
    if (at < 0) {
        at = route_heap_count++;
        route_heap[at] = state;
        route_heap_at[state] = at;
    }
    while (at && route_less(route_heap[at], route_heap[(at - 1) / 2])) {
        int parent = (at - 1) / 2;
        route_heap_swap(at, parent);
        at = parent;
    }
}
static int route_heap_pop(void)
{
    int state = route_heap[0];
    --route_heap_count;
    if (route_heap_count) {
        route_heap[0] = route_heap[route_heap_count];
        route_heap_at[route_heap[0]] = 0;
        int at = 0;
        for (;;) {
            int child = at * 2 + 1;
            if (child >= route_heap_count) break;
            if (child + 1 < route_heap_count && route_less(route_heap[child + 1], route_heap[child])) ++child;
            if (!route_less(route_heap[child], route_heap[at])) break;
            route_heap_swap(at, child);
            at = child;
        }
    }
    route_heap_at[state] = -2;
    return state;
}
static int flow_visibility_route(const FlowDiagram *d, FlowPoint first, FlowPoint last, int side, int other, FlowRoute *route)
{
    route_nx = route_ny = 0;
    route_coordinate(route_x, &route_nx, 0, FLOW_W);
    route_coordinate(route_x, &route_nx, FLOW_W, FLOW_W);
    route_coordinate(route_y, &route_ny, 0, FLOW_H);
    route_coordinate(route_y, &route_ny, FLOW_H, FLOW_H);
    route_coordinate(route_x, &route_nx, first.x, FLOW_W);
    route_coordinate(route_x, &route_nx, last.x, FLOW_W);
    route_coordinate(route_y, &route_ny, first.y, FLOW_H);
    route_coordinate(route_y, &route_ny, last.y, FLOW_H);
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        route_coordinate(route_x, &route_nx, n->x - 12, FLOW_W);
        route_coordinate(route_x, &route_nx, n->x + n->w + 12, FLOW_W);
        route_coordinate(route_y, &route_ny, n->y - 12, FLOW_H);
        route_coordinate(route_y, &route_ny, n->y + n->h + 12, FLOW_H);
    }
    int cells = route_nx * route_ny;
    memset(route_blocked, 0, (size_t)cells);
    memset(route_horizontal, 0, (size_t)cells);
    memset(route_vertical, 0, (size_t)cells);
    for (int i = 0; i < d->node_count; ++i) {
        const FlowNode *n = &d->nodes[i];
        int left = n->x - 8, right = n->x + n->w + 8;
        int top = n->y - 8, bottom = n->y + n->h + 8;
        for (int y = 0; y < route_ny; ++y) {
            for (int x = 0; x < route_nx; ++x) {
                int cell = y * route_nx + x;
                int in_x = route_x[x] >= left && route_x[x] <= right;
                int in_y = route_y[y] >= top && route_y[y] <= bottom;
                if (in_x && in_y) route_blocked[cell] = 1;
                if (in_y && x + 1 < route_nx && route_x[x] <= right && route_x[x + 1] >= left) route_horizontal[cell] = 1;
                if (in_x && y + 1 < route_ny && route_y[y] <= bottom && route_y[y + 1] >= top) route_vertical[cell] = 1;
            }
        }
    }
    int begin = route_index(route_y, route_ny, first.y) * route_nx + route_index(route_x, route_nx, first.x);
    route_goal = route_index(route_y, route_ny, last.y) * route_nx + route_index(route_x, route_nx, last.x);
    if (begin < 0 || route_goal < 0 || route_blocked[begin] || route_blocked[route_goal]) return 0;
    for (int i = 0; i < cells * 4; ++i) {
        route_cost[i] = 1000000000;
        route_parent[i] = -1;
        route_heap_at[i] = -1;
    }
    route_heap_count = 0;
    int start_state = begin * 4 + side;
    route_cost[start_state] = 0;
    route_parent[start_state] = start_state;
    route_heap_push(start_state);
    int final = -1, final_cost = 1000000000;
    while (route_heap_count) {
        if (final >= 0 && route_estimate(route_heap[0]) >= final_cost) break;
        int state = route_heap_pop(), cell = state / 4, incoming = state % 4;
        if (cell == route_goal && incoming != other) {
            int cost = route_cost[state] + (incoming == (other + 2) % 4 ? 0 : 24);
            if (cost < final_cost) { final = state; final_cost = cost; }
        }
        int x = cell % route_nx, y = cell / route_nx;
        int neighbors[] = {y ? cell - route_nx : -1, x + 1 < route_nx ? cell + 1 : -1,
                           y + 1 < route_ny ? cell + route_nx : -1, x ? cell - 1 : -1};
        for (int heading = 0; heading < 4; ++heading) {
            int next = neighbors[heading];
            if (next < 0 || route_blocked[next] || heading == (incoming + 2) % 4) continue;
            if ((heading == 0 && route_vertical[next]) || (heading == 1 && route_horizontal[cell]) ||
                (heading == 2 && route_vertical[cell]) || (heading == 3 && route_horizontal[next])) continue;
            int next_state = next * 4 + heading;
            if (route_heap_at[next_state] == -2) continue;
            int length = flow_abs(route_x[x] - route_x[next % route_nx]) + flow_abs(route_y[y] - route_y[next / route_nx]);
            int cost = route_cost[state] + length + (incoming == heading ? 0 : 24);
            if (cost >= route_cost[next_state]) continue;
            route_cost[next_state] = cost;
            route_parent[next_state] = state;
            route_heap_push(next_state);
        }
    }
    if (final < 0) return 0;
    int count = 0;
    for (int state = final;; state = route_parent[state]) {
        route_reverse[count++] = state / 4;
        if (state == start_state) break;
    }
    for (int i = count - 1; i >= 0; --i) {
        int cell = route_reverse[i];
        flow_route_point(route, route_x[cell % route_nx], route_y[cell / route_nx]);
    }
    return !route->blocked;
}
#endif
