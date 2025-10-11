#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_STOCKS 9
#define MAX_DAYS 365
#define MAX_TIPS 128
#define MAX_EVENTS 32
#define MAX_INPUT 128
#define TIP_HISTORY_WINDOW 8
#define SAVE_FILE "stockmanager.sav"
#define INITIAL_HISTORY_DAYS 14

typedef struct {
    char symbol[6];
    char name[32];
    double base_price;
    double growth_bias;
    double volatility;
    double seasonal_strength;
} StockDefinition;

typedef struct {
    StockDefinition def;
    double price_history[MAX_DAYS];
    size_t history_len;
    int shares_owned;
    double avg_cost_basis;
} Stock;

typedef struct {
    int day_index;
    int stock_index;
    double effect_multiplier;
} MarketEvent;

typedef struct {
    int day_index;
    int stock_index;
    int reliability;
    bool positive;
    bool grounded_in_truth;
    char message[160];
} Tip;

typedef struct {
    double cash;
    int current_day;
    unsigned int rng_state;
    Tip tips[MAX_TIPS];
    size_t tip_count;
    MarketEvent events[MAX_EVENTS];
    size_t event_count;
    bool running;
} GameState;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const StockDefinition STOCK_LIBRARY[MAX_STOCKS] = {
    {"ORCL", "Oracle Systems", 72.0, 0.0030, 0.028, 0.015},
    {"NVLT", "NovaLight Energy", 31.0, 0.0045, 0.042, 0.020},
    {"GRNS", "GreenSprout Farms", 18.0, 0.0020, 0.035, 0.050},
    {"VRTX", "Vertex Robotics", 88.0, 0.0055, 0.060, 0.010},
    {"MRBL", "Marble Infrastructure", 43.0, 0.0038, 0.025, 0.035},
    {"CLDY", "CloudYard Networks", 56.0, 0.0048, 0.055, 0.018},
    {"ARCT", "Arctic Shipping", 22.0, 0.0015, 0.030, 0.045},
    {"HMNY", "Harmony Media", 15.0, 0.0025, 0.050, 0.025},
    {"VRGE", "Verge Healthcare", 68.0, 0.0042, 0.032, 0.030}
};

static const int MONTH_LENGTHS[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static unsigned int lcg_next(GameState *state) {
    state->rng_state = (state->rng_state * 1664525u) + 1013904223u;
    return state->rng_state;
}

static double lcg_rand(GameState *state) {
    return (double)(lcg_next(state) & 0x00FFFFFFu) / (double)0x01000000u;
}

static double clamp(double value, double min_val, double max_val) {
    if(value < min_val)
        return min_val;
    if(value > max_val)
        return max_val;
    return value;
}

static void clear_screen(void) {
    fputs("\033[2J\033[H", stdout);
}

static void pause_and_wait(void) {
    char buffer[MAX_INPUT];
    fputs("\nPress ENTER to continue...", stdout);
    if(fgets(buffer, sizeof(buffer), stdin) == NULL)
        return;
}

static void render_divider(int width) {
    for(int i = 0; i < width; ++i)
        putchar('-');
    putchar('\n');
}

static void format_date(int day_index, char *out, size_t out_size) {
    int year = 2024;
    int month = 0;
    int day = 0;
    int remaining = day_index;
    int month_lengths[12];
    memcpy(month_lengths, MONTH_LENGTHS, sizeof(MONTH_LENGTHS));
    while(remaining >= month_lengths[month]) {
        remaining -= month_lengths[month];
        month++;
        if(month == 12) {
            month = 0;
            year++;
            month_lengths[1] = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
        }
    }
    day = remaining + 1;
    snprintf(out, out_size, "%04d-%02d-%02d", year, month + 1, day);
}

static double seasonal_component(const Stock *stock, int day_index);
static double gaussian_noise(GameState *state, double scale);

static void seed_stock_history(Stock *stock, GameState *state, int warmup_days) {
    if(warmup_days < 1)
        warmup_days = 1;
    if(warmup_days >= MAX_DAYS)
        warmup_days = MAX_DAYS - 1;
    int start_day = -warmup_days;
    double initial_variation = (lcg_rand(state) - 0.5) * 2.0 * stock->def.volatility;
    double price = stock->def.base_price * (1.0 + initial_variation);
    if(price < 1.0)
        price = stock->def.base_price * 0.6;
    stock->history_len = 0;
    for(int day = start_day; day <= 0 && stock->history_len < MAX_DAYS; ++day) {
        if(day > start_day) {
            double drift = stock->def.growth_bias;
            double seasonal = seasonal_component(stock, day);
            double noise = gaussian_noise(state, stock->def.volatility * 0.9);
            double change = drift + seasonal + noise;
            change = clamp(change, -0.18, 0.18);
            price *= 1.0 + change;
            if(price < 0.5)
                price = 0.5;
        }
        stock->price_history[stock->history_len++] = price;
    }
}

static void initialise_stock(Stock *stock, const StockDefinition *def, GameState *state) {
    stock->def = *def;
    stock->shares_owned = 0;
    stock->avg_cost_basis = 0.0;
    seed_stock_history(stock, state, INITIAL_HISTORY_DAYS);
}

static void reset_game(GameState *state, Stock *stocks, size_t stock_count) {
    state->cash = 1000.0;
    state->current_day = 0;
    state->tip_count = 0;
    state->event_count = 0;
    state->running = true;
    unsigned int seed = (unsigned int)time(NULL);
    if(seed == 0)
        seed = 1u;
    state->rng_state = seed;
    for(size_t i = 0; i < stock_count; ++i) {
        initialise_stock(&stocks[i], &STOCK_LIBRARY[i], state);
    }
}

static double seasonal_component(const Stock *stock, int day_index) {
    double season = sin(((double)day_index / 30.0) * 2.0 * M_PI);
    return season * stock->def.seasonal_strength;
}

static double gaussian_noise(GameState *state, double scale) {
    double u1 = lcg_rand(state);
    double u2 = lcg_rand(state);
    if(u1 < 1e-6)
        u1 = 1e-6;
    double mag = sqrt(-2.0 * log(u1));
    double z0 = mag * cos(2.0 * M_PI * u2);
    return z0 * scale;
}

static double collect_event_effects(GameState *state, int stock_index) {
    double effect = 0.0;
    for(size_t i = 0; i < state->event_count; ++i) {
        if(state->events[i].day_index == state->current_day && state->events[i].stock_index == stock_index)
            effect += state->events[i].effect_multiplier;
    }
    return effect;
}

static void prune_events(GameState *state) {
    size_t write = 0;
    for(size_t read = 0; read < state->event_count; ++read) {
        if(state->events[read].day_index > state->current_day) {
            state->events[write++] = state->events[read];
        }
    }
    state->event_count = write;
}

static void append_tip(GameState *state, const Tip *tip) {
    if(state->tip_count < MAX_TIPS) {
        state->tips[state->tip_count++] = *tip;
    } else {
        memmove(&state->tips[0], &state->tips[1], sizeof(Tip) * (MAX_TIPS - 1));
        state->tips[MAX_TIPS - 1] = *tip;
    }
}

static void generate_tip(GameState *state, Stock *stocks, size_t stock_count) {
    double probability = 0.35;
    if(lcg_rand(state) > probability)
        return;
    size_t stock_index = (size_t)(lcg_rand(state) * (double)stock_count);
    if(stock_index >= stock_count)
        stock_index = stock_count - 1;
    bool positive = lcg_rand(state) > 0.45;
    bool grounded = lcg_rand(state) > 0.35;
    int reliability = (int)(lcg_rand(state) * 100.0);
    double magnitude = stocks[stock_index].def.volatility * (grounded ? 1.8 : 1.2);
    if(!positive)
        magnitude = -magnitude;
    if(grounded) {
        if(state->event_count < MAX_EVENTS) {
            MarketEvent ev;
            ev.day_index = state->current_day + 1;
            ev.stock_index = (int)stock_index;
            ev.effect_multiplier = magnitude;
            state->events[state->event_count++] = ev;
        }
    }
    Tip tip;
    tip.day_index = state->current_day;
    tip.stock_index = (int)stock_index;
    tip.reliability = reliability;
    tip.positive = positive;
    tip.grounded_in_truth = grounded;
    const char *template_positive_true[] = {
        "%s is rumored to land a big client next week!",
        "Insiders whisper that %s just beat quarterly guidance.",
        "A respected analyst just upgraded %s to STRONG BUY.",
        "Supply chain data hints at a surge in %s shipments."
    };
    const char *template_positive_false[] = {
        "Mysterious blog claims %s is getting acquired soon... maybe?",
        "Viral tweet says %s is the next meme rocket.",
        "%s CEO allegedly seen entering a megabank HQ!",
        "Random forum user predicts %s to double overnight."
    };
    const char *template_negative_true[] = {
        "Whistleblower hints at delays for %s product rollout.",
        "Credit agencies eye a downgrade on %s debt load.",
        "Major client reportedly cancelled %s contract.",
        "Supply woes might hurt %s margins this quarter."
    };
    const char *template_negative_false[] = {
        "Satirical site jokes that %s lost all its servers.",
        "Anonymous alt-account says %s is under SEC probe?!",
        "%s trending with hashtag #bankrupt... seems sus.",
        "Questionable newsletter says %s CFO fled the country."
    };
    const char *const *pool;
    size_t pool_size = 4;
    if(positive && grounded)
        pool = template_positive_true;
    else if(positive)
        pool = template_positive_false;
    else if(grounded)
        pool = template_negative_true;
    else
        pool = template_negative_false;
    size_t pick = (size_t)(lcg_rand(state) * (double)pool_size);
    if(pick >= pool_size)
        pick = pool_size - 1;
    snprintf(tip.message, sizeof(tip.message), pool[pick], stocks[stock_index].def.name);
    append_tip(state, &tip);
}

static void advance_day(GameState *state, Stock *stocks, size_t stock_count) {
    state->current_day++;
    for(size_t i = 0; i < stock_count; ++i) {
        double prev_price = stocks[i].price_history[stocks[i].history_len - 1];
        double drift = stocks[i].def.growth_bias;
        double seasonal = seasonal_component(&stocks[i], state->current_day);
        double noise = gaussian_noise(state, stocks[i].def.volatility);
        double event = collect_event_effects(state, (int)i);
        double change = drift + seasonal + noise + event;
        change = clamp(change, -0.25, 0.25);
        double next_price = prev_price * (1.0 + change);
        if(next_price < 0.5)
            next_price = 0.5;
        if(stocks[i].history_len < MAX_DAYS)
            stocks[i].price_history[stocks[i].history_len++] = next_price;
        else
            stocks[i].price_history[stocks[i].history_len - 1] = next_price;
    }
    prune_events(state);
    generate_tip(state, stocks, stock_count);
}

static double current_price(const Stock *stock) {
    if(stock->history_len == 0)
        return stock->def.base_price;
    return stock->price_history[stock->history_len - 1];
}

static double portfolio_value(const Stock *stocks, size_t stock_count) {
    double total = 0.0;
    for(size_t i = 0; i < stock_count; ++i) {
        total += (double)stocks[i].shares_owned * current_price(&stocks[i]);
    }
    return total;
}

static void sparkline(const Stock *stock, char *out, size_t out_size, size_t window) {
    const char gradient[] = ".:-=+*#%@";
    size_t grad_len = strlen(gradient) - 1;
    if(window == 0 || out_size == 0) {
        if(out_size > 0)
            out[0] = '\0';
        return;
    }
    size_t len = stock->history_len < window ? stock->history_len : window;
    if(len == 0) {
        snprintf(out, out_size, "%s", "");
        return;
    }
    double min_val = stock->price_history[stock->history_len - len];
    double max_val = min_val;
    for(size_t i = stock->history_len - len; i < stock->history_len; ++i) {
        if(stock->price_history[i] < min_val)
            min_val = stock->price_history[i];
        if(stock->price_history[i] > max_val)
            max_val = stock->price_history[i];
    }
    double range = max_val - min_val;
    if(range < 1e-6)
        range = 1.0;
    size_t idx = 0;
    for(size_t i = stock->history_len - len; i < stock->history_len && idx + 1 < out_size; ++i) {
        double normalized = (stock->price_history[i] - min_val) / range;
        size_t grad_index = (size_t)round(normalized * grad_len);
        if(grad_index > grad_len)
            grad_index = grad_len;
        out[idx++] = gradient[grad_index];
    }
    out[idx] = '\0';
}

static void show_market_overview(const GameState *state, const Stock *stocks, size_t stock_count) {
    char date[16];
    format_date(state->current_day, date, sizeof(date));
    clear_screen();
    printf("=== STOCK MANAGER ===\\n");
    printf("Date: %s | Cash: $%.2f | Portfolio: $%.2f | Net Worth: $%.2f\\n", date, state->cash,
           portfolio_value(stocks, stock_count), state->cash + portfolio_value(stocks, stock_count));
    render_divider(78);
    printf("%-6s %-22s %10s %10s %10s %-20s\\n", "SYM", "Company", "Price", "Day%", "Week%", "Trend");
    render_divider(78);
    for(size_t i = 0; i < stock_count; ++i) {
        double price = current_price(&stocks[i]);
        double previous = stocks[i].history_len > 1 ? stocks[i].price_history[stocks[i].history_len - 2] : price;
        double change_day = ((price - previous) / previous) * 100.0;
        size_t len = stocks[i].history_len < 7 ? stocks[i].history_len : 7;
        double week_base = stocks[i].price_history[stocks[i].history_len - len];
        double change_week = ((price - week_base) / week_base) * 100.0;
        char chart[32];
        sparkline(&stocks[i], chart, sizeof(chart), 16);
        printf("%-6s %-22s %10.2f %9.2f%% %9.2f%% %-20s\\n",
               stocks[i].def.symbol,
               stocks[i].def.name,
               price,
               change_day,
               change_week,
               chart);
    }
    render_divider(78);
}

static void show_portfolio_view(const GameState *state, const Stock *stocks, size_t stock_count) {
    (void)state;
    clear_screen();
    printf("=== PORTFOLIO SNAPSHOT ===\\n");
    render_divider(78);
    printf("%-6s %-22s %8s %12s %12s %10s\\n", "SYM", "Company", "Shares", "Avg Cost", "Mkt Value", "P/L");
    render_divider(78);
    double total_value = 0.0;
    double total_cost = 0.0;
    for(size_t i = 0; i < stock_count; ++i) {
        if(stocks[i].shares_owned == 0)
            continue;
        double price = current_price(&stocks[i]);
        double value = price * (double)stocks[i].shares_owned;
        double cost = stocks[i].avg_cost_basis * (double)stocks[i].shares_owned;
        total_value += value;
        total_cost += cost;
        double pl = value - cost;
        printf("%-6s %-22s %8d %12.2f %12.2f %10.2f\\n",
               stocks[i].def.symbol,
               stocks[i].def.name,
               stocks[i].shares_owned,
               stocks[i].avg_cost_basis,
               value,
               pl);
    }
    render_divider(78);
    printf("Total Invested: $%.2f | Portfolio Value: $%.2f | Unrealized P/L: $%.2f\\n",
           total_cost,
           total_value,
           total_value - total_cost);
    printf("Cash on hand: $%.2f\\n", state->cash);
}

static void show_tip_feed(const GameState *state, const Stock *stocks, size_t stock_count) {
    (void)stock_count;
    clear_screen();
    printf("=== MARKET BUZZ ===\\n");
    render_divider(78);
    size_t start = state->tip_count > TIP_HISTORY_WINDOW ? state->tip_count - TIP_HISTORY_WINDOW : 0u;
    if(state->tip_count == 0) {
        printf("The feeds are quiet today. No rumors in circulation.\\n");
    } else {
        for(size_t i = start; i < state->tip_count; ++i) {
            const Tip *tip = &state->tips[i];
            char date[16];
            format_date(tip->day_index, date, sizeof(date));
            printf("[%s] (%s) %s\\n", date, stocks[tip->stock_index].def.symbol, tip->message);
            printf("   Reliability meter: %d%% | Sentiment: %s\\n",
                   tip->reliability,
                   tip->positive ? "Bullish" : "Bearish");
        }
    }
    render_divider(78);
    printf("Some tips are gold, others are noise. Study how prices react to separate signal from hype.\\n");
}

static void show_help(void) {
    clear_screen();
    printf("=== HOW TO PLAY STOCK MANAGER ===\\n");
    render_divider(78);
    printf("You are a rookie broker armed with $1000. Each day you can review markets,\\n");
    printf("buy or sell shares, and advance time. Study price behavior and use the rumor\\n");
    printf("mill wisely. Reliable tips tend to come from grounded sources, but beware of\\n");
    printf("false alarms. Remember: long-term trends exist, but volatility can bite.\\n\\n");
    printf("Controls: enter the number shown in the menu. When trading, input the stock\\n");
    printf("symbol and quantity. The game auto-tracks your cost basis for P/L analysis.\\n");
    printf("Use Save/Load to continue your campaign another time.\\n");
    render_divider(78);
}

static int find_stock_index(const Stock *stocks, size_t stock_count, const char *symbol) {
    for(size_t i = 0; i < stock_count; ++i) {
        if(strcasecmp(stocks[i].def.symbol, symbol) == 0)
            return (int)i;
    }
    return -1;
}

static void trim_newline(char *buffer) {
    size_t len = strlen(buffer);
    if(len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';
}

static void buy_stock(GameState *state, Stock *stock) {
    char input[MAX_INPUT];
    printf("How many shares of %s (%s) would you like to buy? ", stock->def.name, stock->def.symbol);
    if(fgets(input, sizeof(input), stdin) == NULL)
        return;
    int shares = atoi(input);
    if(shares <= 0) {
        printf("Invalid quantity. Purchase cancelled.\\n");
        return;
    }
    double price = current_price(stock);
    double total_cost = price * (double)shares;
    if(total_cost > state->cash + 1e-6) {
        printf("Insufficient funds.\\n");
        return;
    }
    double previous_cost = stock->avg_cost_basis * (double)stock->shares_owned;
    double new_cost = previous_cost + total_cost;
    stock->shares_owned += shares;
    if(stock->shares_owned > 0)
        stock->avg_cost_basis = new_cost / (double)stock->shares_owned;
    else
        stock->avg_cost_basis = 0.0;
    state->cash -= total_cost;
    printf("Purchased %d shares of %s at $%.2f per share.\\n", shares, stock->def.symbol, price);
}

static void sell_stock(GameState *state, Stock *stock) {
    char input[MAX_INPUT];
    if(stock->shares_owned <= 0) {
        printf("You do not own shares of %s.\\n", stock->def.symbol);
        return;
    }
    printf("How many shares of %s (%s) would you like to sell? ", stock->def.name, stock->def.symbol);
    if(fgets(input, sizeof(input), stdin) == NULL)
        return;
    int shares = atoi(input);
    if(shares <= 0 || shares > stock->shares_owned) {
        printf("Invalid quantity. Sale cancelled.\\n");
        return;
    }
    double price = current_price(stock);
    double proceeds = price * (double)shares;
    stock->shares_owned -= shares;
    if(stock->shares_owned == 0)
        stock->avg_cost_basis = 0.0;
    state->cash += proceeds;
    printf("Sold %d shares of %s at $%.2f per share.\\n", shares, stock->def.symbol, price);
}

static void trade(GameState *state, Stock *stocks, size_t stock_count) {
    clear_screen();
    printf("=== TRADING TERMINAL ===\\n");
    render_divider(78);
    for(size_t i = 0; i < stock_count; ++i) {
        printf("%-6s %-22s Price: $%6.2f | Owned: %3d\\n",
               stocks[i].def.symbol,
               stocks[i].def.name,
               current_price(&stocks[i]),
               stocks[i].shares_owned);
    }
    render_divider(78);
    printf("Available cash: $%.2f\\n", state->cash);
    printf("Enter stock symbol to trade (or press ENTER to return): ");
    char input[MAX_INPUT];
    if(fgets(input, sizeof(input), stdin) == NULL)
        return;
    trim_newline(input);
    if(strlen(input) == 0)
        return;
    int index = find_stock_index(stocks, stock_count, input);
    if(index < 0) {
        printf("Unknown symbol '%s'.\\n", input);
        pause_and_wait();
        return;
    }
    printf("Buy or Sell? (b/s): ");
    if(fgets(input, sizeof(input), stdin) == NULL)
        return;
    if(tolower((unsigned char)input[0]) == 'b')
        buy_stock(state, &stocks[index]);
    else if(tolower((unsigned char)input[0]) == 's')
        sell_stock(state, &stocks[index]);
    else
        printf("Action cancelled.\\n");
    pause_and_wait();
}

static void show_deep_dive(const Stock *stocks, size_t stock_count) {
    clear_screen();
    printf("=== MARKET LAB ===\\n");
    render_divider(78);
    printf("Select a stock for an extended price trail (ENTER to exit): ");
    char input[MAX_INPUT];
    if(fgets(input, sizeof(input), stdin) == NULL)
        return;
    trim_newline(input);
    if(strlen(input) == 0)
        return;
    int index = find_stock_index(stocks, stock_count, input);
    if(index < 0) {
        printf("Unknown symbol '%s'.\\n", input);
        pause_and_wait();
        return;
    }
    const Stock *stock = &stocks[index];
    printf("\n%-6s %-22s\\n", stock->def.symbol, stock->def.name);
    render_divider(78);
    size_t start = stock->history_len > 40 ? stock->history_len - 40 : 0u;
    double min_val = stock->price_history[start];
    double max_val = min_val;
    for(size_t i = start; i < stock->history_len; ++i) {
        if(stock->price_history[i] < min_val)
            min_val = stock->price_history[i];
        if(stock->price_history[i] > max_val)
            max_val = stock->price_history[i];
    }
    double range = max_val - min_val;
    if(range < 1e-6)
        range = 1.0;
    for(size_t i = start; i < stock->history_len; ++i) {
        double price = stock->price_history[i];
        double normalized = (price - min_val) / range;
        int bar_width = (int)(normalized * 40.0);
        char date[16];
        format_date((int)i, date, sizeof(date));
        printf("%s | $%6.2f | ", date, price);
        for(int b = 0; b < bar_width; ++b)
            putchar('=');
        printf(">\\n");
    }
    render_divider(78);
    pause_and_wait();
}

static bool save_game(const GameState *state, const Stock *stocks, size_t stock_count) {
    FILE *fp = fopen(SAVE_FILE, "w");
    if(fp == NULL) {
        perror("Failed to save game");
        return false;
    }
    fprintf(fp, "STOCKMANAGER_SAVE 1\n");
    fprintf(fp, "DAY %d\n", state->current_day);
    fprintf(fp, "CASH %.17g\n", state->cash);
    fprintf(fp, "RNG %u\n", state->rng_state);
    fprintf(fp, "TIPS %zu\n", state->tip_count);
    for(size_t i = 0; i < state->tip_count; ++i) {
        const Tip *tip = &state->tips[i];
        fprintf(fp, "TIP %d %d %d %d %d|%s\n",
                tip->day_index,
                tip->stock_index,
                tip->reliability,
                tip->positive ? 1 : 0,
                tip->grounded_in_truth ? 1 : 0,
                tip->message);
    }
    fprintf(fp, "EVENTS %zu\n", state->event_count);
    for(size_t i = 0; i < state->event_count; ++i) {
        const MarketEvent *ev = &state->events[i];
        fprintf(fp, "EVENT %d %d %.17g\n", ev->day_index, ev->stock_index, ev->effect_multiplier);
    }
    fprintf(fp, "STOCKS %zu\n", stock_count);
    for(size_t i = 0; i < stock_count; ++i) {
        const Stock *stock = &stocks[i];
        fprintf(fp, "STOCK %d %.17g %zu", stock->shares_owned, stock->avg_cost_basis, stock->history_len);
        for(size_t h = 0; h < stock->history_len; ++h)
            fprintf(fp, " %.17g", stock->price_history[h]);
        fprintf(fp, "\n");
    }
    fclose(fp);
    printf("Game saved to '%s'.\\n", SAVE_FILE);
    return true;
}

static bool load_game(GameState *state, Stock *stocks, size_t stock_count) {
    FILE *fp = fopen(SAVE_FILE, "r");
    if(fp == NULL) {
        perror("Failed to load game");
        return false;
    }
    char header[32];
    if(fscanf(fp, "%31s", header) != 1 || strcmp(header, "STOCKMANAGER_SAVE") != 0) {
        fclose(fp);
        fprintf(stderr, "Invalid save file.\n");
        return false;
    }
    int version;
    if(fscanf(fp, "%d", &version) != 1 || version != 1) {
        fclose(fp);
        fprintf(stderr, "Unsupported save version.\n");
        return false;
    }
    char label[16];
    if(fscanf(fp, "%15s %d", label, &state->current_day) != 2 || strcmp(label, "DAY") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (DAY).\n");
        return false;
    }
    if(fscanf(fp, "%15s %lf", label, &state->cash) != 2 || strcmp(label, "CASH") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (CASH).\n");
        return false;
    }
    if(fscanf(fp, "%15s %u", label, &state->rng_state) != 2 || strcmp(label, "RNG") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (RNG).\n");
        return false;
    }
    size_t tip_count;
    if(fscanf(fp, "%15s %zu", label, &tip_count) != 2 || strcmp(label, "TIPS") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (TIPS).\n");
        return false;
    }
    state->tip_count = 0;
    for(size_t i = 0; i < tip_count; ++i) {
        int day_index;
        int stock_index;
        int reliability;
        int positive;
        int grounded;
        if(fscanf(fp, "%15s %d %d %d %d %d", label, &day_index, &stock_index, &reliability, &positive, &grounded) != 6 || strcmp(label, "TIP") != 0) {
            fclose(fp);
            fprintf(stderr, "Corrupt save (TIP).\n");
            return false;
        }
        int ch = fgetc(fp);
        if(ch != '|') {
            fclose(fp);
            fprintf(stderr, "Corrupt save (TIP separator).\n");
            return false;
        }
        char message[160];
        if(fgets(message, sizeof(message), fp) == NULL) {
            fclose(fp);
            fprintf(stderr, "Corrupt save (TIP message).\n");
            return false;
        }
        trim_newline(message);
        Tip tip;
        tip.day_index = day_index;
        tip.stock_index = stock_index;
        tip.reliability = reliability;
        tip.positive = positive != 0;
        tip.grounded_in_truth = grounded != 0;
        snprintf(tip.message, sizeof(tip.message), "%s", message);
        append_tip(state, &tip);
    }
    size_t event_count;
    if(fscanf(fp, "%15s %zu", label, &event_count) != 2 || strcmp(label, "EVENTS") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (EVENTS).\n");
        return false;
    }
    state->event_count = 0;
    for(size_t i = 0; i < event_count; ++i) {
        MarketEvent ev;
        if(fscanf(fp, "%15s %d %d %lf", label, &ev.day_index, &ev.stock_index, &ev.effect_multiplier) != 4 || strcmp(label, "EVENT") != 0) {
            fclose(fp);
            fprintf(stderr, "Corrupt save (EVENT).\n");
            return false;
        }
        if(state->event_count < MAX_EVENTS)
            state->events[state->event_count++] = ev;
    }
    size_t stored_stocks;
    if(fscanf(fp, "%15s %zu", label, &stored_stocks) != 2 || strcmp(label, "STOCKS") != 0) {
        fclose(fp);
        fprintf(stderr, "Corrupt save (STOCKS).\n");
        return false;
    }
    if(stored_stocks != stock_count) {
        fclose(fp);
        fprintf(stderr, "Save mismatch: expected %zu stocks, found %zu.\n", stock_count, stored_stocks);
        return false;
    }
    for(size_t i = 0; i < stock_count; ++i) {
        int shares;
        double avg_cost;
        size_t history_len;
        if(fscanf(fp, "%15s %d %lf %zu", label, &shares, &avg_cost, &history_len) != 4 || strcmp(label, "STOCK") != 0) {
            fclose(fp);
            fprintf(stderr, "Corrupt save (STOCK entry).\n");
            return false;
        }
        stocks[i].def = STOCK_LIBRARY[i];
        stocks[i].shares_owned = shares;
        stocks[i].avg_cost_basis = avg_cost;
        if(history_len > MAX_DAYS)
            history_len = MAX_DAYS;
        stocks[i].history_len = history_len;
        for(size_t h = 0; h < history_len; ++h) {
            if(fscanf(fp, "%lf", &stocks[i].price_history[h]) != 1) {
                fclose(fp);
                fprintf(stderr, "Corrupt save (price history).\n");
                return false;
            }
        }
    }
    fclose(fp);
    state->running = true;
    printf("Game loaded from '%s'.\\n", SAVE_FILE);
    return true;
}

static void show_menu(void) {
    printf("\nChoose an action:\n");
    printf(" 1) Market overview\n");
    printf(" 2) Portfolio view\n");
    printf(" 3) Trading terminal\n");
    printf(" 4) Advance to next day\n");
    printf(" 5) Market buzz feed\n");
    printf(" 6) Research lab (deep dive)\n");
    printf(" 7) Save game\n");
    printf(" 8) Load game\n");
    printf(" 9) Help\n");
    printf(" 0) Quit\n");
    printf("> ");
}

static void handle_menu_choice(GameState *state, Stock *stocks, size_t stock_count, const char *choice) {
    switch(choice[0]) {
        case '1':
            show_market_overview(state, stocks, stock_count);
            pause_and_wait();
            break;
        case '2':
            show_portfolio_view(state, stocks, stock_count);
            pause_and_wait();
            break;
        case '3':
            trade(state, stocks, stock_count);
            break;
        case '4':
            advance_day(state, stocks, stock_count);
            printf("A new trading day dawns. Stay sharp.\n");
            pause_and_wait();
            break;
        case '5':
            show_tip_feed(state, stocks, stock_count);
            pause_and_wait();
            break;
        case '6':
            show_deep_dive(stocks, stock_count);
            break;
        case '7':
            save_game(state, stocks, stock_count);
            pause_and_wait();
            break;
        case '8':
            if(load_game(state, stocks, stock_count))
                pause_and_wait();
            else
                pause_and_wait();
            break;
        case '9':
            show_help();
            pause_and_wait();
            break;
        case '0':
            state->running = false;
            break;
        default:
            printf("Unknown choice.\n");
            pause_and_wait();
            break;
    }
}

int main(void) {
    GameState state;
    Stock stocks[MAX_STOCKS];
    reset_game(&state, stocks, MAX_STOCKS);
    while(state.running) {
        show_market_overview(&state, stocks, MAX_STOCKS);
        show_menu();
        char input[MAX_INPUT];
        if(fgets(input, sizeof(input), stdin) == NULL)
            break;
        trim_newline(input);
        if(strlen(input) == 0)
            continue;
        handle_menu_choice(&state, stocks, MAX_STOCKS, input);
    }
    clear_screen();
    double total = state.cash + portfolio_value(stocks, MAX_STOCKS);
    printf("Thanks for playing STOCK MANAGER! Final net worth: $%.2f\n", total);
    return 0;
}
