#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

/*-----------------------*/
/* Configuration Macros  */
/*-----------------------*/
#define CONTEXT_LENGTH        5  // Use five words of context
#define EMBEDDING_DIM         50
#define HIDDEN_SIZE1          128
#define HIDDEN_SIZE2          128

#define MAX_WORD_LEN          50
#define MAX_INPUT_SIZE        1000
#define MAX_TOKENS            1000
#define MAX_VOCAB_SIZE        10000
#define MAX_TRAIN_EXAMPLES    100000

#define INITIAL_LEARNING_RATE 0.01
#define DEFAULT_EPOCHS        30
#define LR_DECAY_FACTOR       0.95

#define DEFAULT_BATCH_SIZE    32
#define MAX_PREDICT_WORDS     10

/* Adam optimizer hyperparameters */
#define BETA1    0.9
#define BETA2    0.999
#define EPSILON  1e-8

/* Special tokens */
#define START_TOKEN "<s>"
#define END_TOKEN   "</s>"

/*-----------------------*/
/* Type Definitions      */
/*-----------------------*/

// Training example: context of CONTEXT_LENGTH tokens and one target.
typedef struct {
    int context[CONTEXT_LENGTH];
    int target;
} TrainingExample;

// Vocabulary structure.
typedef struct {
    char *words[MAX_VOCAB_SIZE];
    int size;
} Vocabulary;

// Neural network structure.
typedef struct {
    int vocab_size;
    int emb_dim;
    int hidden1;
    int hidden2;
    double **embedding; // [vocab_size][emb_dim]
    double **W1;        // [(CONTEXT_LENGTH * emb_dim)][hidden1]
    double *b1;         // [hidden1]
    double **W2;        // [hidden1][hidden2]
    double *b2;         // [hidden2]
    double **W3;        // [hidden2][vocab_size]
    double *b3;         // [vocab_size]
} NeuralNetwork;

// Adam optimizer state.
typedef struct {
    double **m_embedding;
    double **v_embedding;
    double **m_W1;
    double **v_W1;
    double *m_b1;
    double *v_b1;
    double **m_W2;
    double **v_W2;
    double *m_b2;
    double *v_b2;
    double **m_W3;
    double **v_W3;
    double *m_b3;
    double *v_b3;
    int t;
} AdamParams;

// Gradients for mini-batch training.
typedef struct {
    double **d_embedding; // [vocab_size][emb_dim]
    double **d_W1;        // [CONTEXT_LENGTH * emb_dim][hidden1]
    double *d_b1;         // [hidden1]
    double **d_W2;        // [hidden1][hidden2]
    double *d_b2;         // [hidden2]
    double **d_W3;        // [hidden2][vocab_size]
    double *d_b3;         // [vocab_size]
} Gradients;

// Forward propagation cache.
typedef struct ForwardCache {
    double *x;   // concatenated input: length = CONTEXT_LENGTH * emb_dim
    double *z1;
    double *a1;
    double *z2;
    double *a2;
    double *z3;  // raw logits
    double *y;   // softmax probabilities
} ForwardCache;

/*-----------------------*/
/* Global Variables      */
/*-----------------------*/
Vocabulary vocab;
TrainingExample train_examples[MAX_TRAIN_EXAMPLES];
int train_example_count = 0;
NeuralNetwork net = {0};
AdamParams adam = {0};

double learning_rate = INITIAL_LEARNING_RATE;

/*-----------------------*/
/* Function Prototypes   */
/*-----------------------*/
double relu(double x);
double relu_deriv(double x);
void free_forward_cache(ForwardCache *cache);
double compute_temperature(const char *input);
void softmax_temp(double *z, int n, double temp);

/* (Other prototypes are omitted for brevity; assume they appear in the same order as their definitions.) */

/*-----------------------*/
/* Utility Functions     */
/*-----------------------*/
void trim_whitespace(char *str) {
    char *start = str;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != str)
        memmove(str, start, strlen(start) + 1);
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

void normalize_word(char *word) {
    if (strcmp(word, START_TOKEN) == 0 || strcmp(word, END_TOKEN) == 0)
        return;
    for (int i = 0; word[i]; i++)
        word[i] = tolower((unsigned char)word[i]);
    int start = 0;
    while (word[start] && !isalnum((unsigned char)word[start]))
        start++;
    if (start > 0) {
        int i = 0;
        while (word[start + i]) {
            word[i] = word[start + i];
            i++;
        }
        word[i] = '\0';
    }
    int len = strlen(word);
    while (len > 0 && !isalnum((unsigned char)word[len - 1])) {
        word[len - 1] = '\0';
        len--;
    }
}

int tokenize(char *input, char **words, int max_tokens) {
    int count = 0;
    char *token = strtok(input, " ");
    while (token && count < max_tokens) {
        normalize_word(token);
        if (strlen(token) > 0)
            words[count++] = token;
        token = strtok(NULL, " ");
    }
    return count;
}

/*-----------------------*/
/* Vocabulary Functions  */
/*-----------------------*/
void init_vocab() {
    vocab.size = 0;
}

int find_in_vocab(const char *word) {
    for (int i = 0; i < vocab.size; i++) {
        if (strcmp(vocab.words[i], word) == 0)
            return i;
    }
    return -1;
}

int add_word(const char *word) {
    int idx = find_in_vocab(word);
    if (idx >= 0)
        return idx;
    if (vocab.size >= MAX_VOCAB_SIZE) {
        fprintf(stderr, "Vocabulary limit reached.\n");
        exit(1);
    }
    vocab.words[vocab.size] = strdup(word);
    if (!vocab.words[vocab.size]) {
        fprintf(stderr, "Memory allocation error for vocabulary.\n");
        exit(1);
    }
    return vocab.size++;
}

void free_vocab() {
    for (int i = 0; i < vocab.size; i++) {
        free(vocab.words[i]);
    }
}

/*-----------------------*/
/* Matrix/Vector Helpers */
/*-----------------------*/
double *alloc_vector(int size) {
    double *v = malloc(sizeof(double) * size);
    if (!v) {
        fprintf(stderr, "Failed to allocate vector.\n");
        exit(1);
    }
    return v;
}

double **alloc_matrix(int rows, int cols) {
    double **m = malloc(sizeof(double*) * rows);
    if (!m) {
        fprintf(stderr, "Failed to allocate matrix rows.\n");
        exit(1);
    }
    for (int i = 0; i < rows; i++) {
        m[i] = malloc(sizeof(double) * cols);
        if (!m[i]) {
            fprintf(stderr, "Failed to allocate matrix row.\n");
            exit(1);
        }
    }
    return m;
}

void free_matrix(double **m, int rows) {
    if (!m)
        return;
    for (int i = 0; i < rows; i++)
        free(m[i]);
    free(m);
}

double rand_uniform(double r) {
    return ((double)rand() / RAND_MAX) * 2 * r - r;
}

/*-----------------------*/
/* Neural Network Setup  */
/*-----------------------*/
void init_network() {
    net.vocab_size = vocab.size;
    net.emb_dim = EMBEDDING_DIM;
    net.hidden1 = HIDDEN_SIZE1;
    net.hidden2 = HIDDEN_SIZE2;

    net.embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    double r_emb = 0.5;
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++)
            net.embedding[i][j] = rand_uniform(r_emb);

    net.W1 = alloc_matrix(CONTEXT_LENGTH * net.emb_dim, net.hidden1);
    net.b1 = alloc_vector(net.hidden1);
    double r_W1 = 0.5;
    for (int i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++)
            net.W1[i][j] = rand_uniform(r_W1);
    for (int j = 0; j < net.hidden1; j++)
        net.b1[j] = 0.0;

    net.W2 = alloc_matrix(net.hidden1, net.hidden2);
    net.b2 = alloc_vector(net.hidden2);
    double r_W2 = 0.5;
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++)
            net.W2[i][j] = rand_uniform(r_W2);
    for (int j = 0; j < net.hidden2; j++)
        net.b2[j] = 0.0;

    net.W3 = alloc_matrix(net.hidden2, net.vocab_size);
    net.b3 = alloc_vector(net.vocab_size);
    double r_W3 = 0.5;
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++)
            net.W3[i][j] = rand_uniform(r_W3);
    for (int j = 0; j < net.vocab_size; j++)
        net.b3[j] = 0.0;
}

void init_adam() {
    adam.t = 0;
    adam.m_embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    adam.v_embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++) {
            adam.m_embedding[i][j] = 0.0;
            adam.v_embedding[i][j] = 0.0;
        }
    adam.m_W1 = alloc_matrix(CONTEXT_LENGTH * net.emb_dim, net.hidden1);
    adam.v_W1 = alloc_matrix(CONTEXT_LENGTH * net.emb_dim, net.hidden1);
    adam.m_b1 = alloc_vector(net.hidden1);
    adam.v_b1 = alloc_vector(net.hidden1);
    for (int i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++) {
            adam.m_W1[i][j] = 0.0;
            adam.v_W1[i][j] = 0.0;
        }
    for (int j = 0; j < net.hidden1; j++) {
        adam.m_b1[j] = 0.0;
        adam.v_b1[j] = 0.0;
    }
    adam.m_W2 = alloc_matrix(net.hidden1, net.hidden2);
    adam.v_W2 = alloc_matrix(net.hidden1, net.hidden2);
    adam.m_b2 = alloc_vector(net.hidden2);
    adam.v_b2 = alloc_vector(net.hidden2);
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++) {
            adam.m_W2[i][j] = 0.0;
            adam.v_W2[i][j] = 0.0;
        }
    for (int j = 0; j < net.hidden2; j++) {
        adam.m_b2[j] = 0.0;
        adam.v_b2[j] = 0.0;
    }
    adam.m_W3 = alloc_matrix(net.hidden2, net.vocab_size);
    adam.v_W3 = alloc_matrix(net.hidden2, net.vocab_size);
    adam.m_b3 = alloc_vector(net.vocab_size);
    adam.v_b3 = alloc_vector(net.vocab_size);
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++) {
            adam.m_W3[i][j] = 0.0;
            adam.v_W3[i][j] = 0.0;
        }
    for (int j = 0; j < net.vocab_size; j++) {
        adam.m_b3[j] = 0.0;
        adam.v_b3[j] = 0.0;
    }
}

/*-----------------------*/
/* Free Functions        */
/*-----------------------*/
void free_network() {
    if (net.embedding) { free_matrix(net.embedding, net.vocab_size); net.embedding = NULL; }
    if (net.W1) { free_matrix(net.W1, CONTEXT_LENGTH * net.emb_dim); net.W1 = NULL; }
    if (net.b1) { free(net.b1); net.b1 = NULL; }
    if (net.W2) { free_matrix(net.W2, net.hidden1); net.W2 = NULL; }
    if (net.b2) { free(net.b2); net.b2 = NULL; }
    if (net.W3) { free_matrix(net.W3, net.hidden2); net.W3 = NULL; }
    if (net.b3) { free(net.b3); net.b3 = NULL; }
}

void free_adam() {
    if (adam.m_embedding) { free_matrix(adam.m_embedding, net.vocab_size); adam.m_embedding = NULL; }
    if (adam.v_embedding) { free_matrix(adam.v_embedding, net.vocab_size); adam.v_embedding = NULL; }
    if (adam.m_W1) { free_matrix(adam.m_W1, CONTEXT_LENGTH * net.emb_dim); adam.m_W1 = NULL; }
    if (adam.v_W1) { free_matrix(adam.v_W1, CONTEXT_LENGTH * net.emb_dim); adam.v_W1 = NULL; }
    if (adam.m_b1) { free(adam.m_b1); adam.m_b1 = NULL; }
    if (adam.v_b1) { free(adam.v_b1); adam.v_b1 = NULL; }
    if (adam.m_W2) { free_matrix(adam.m_W2, net.hidden1); adam.m_W2 = NULL; }
    if (adam.v_W2) { free_matrix(adam.v_W2, net.hidden1); adam.v_W2 = NULL; }
    if (adam.m_b2) { free(adam.m_b2); adam.m_b2 = NULL; }
    if (adam.v_b2) { free(adam.v_b2); adam.v_b2 = NULL; }
    if (adam.m_W3) { free_matrix(adam.m_W3, net.hidden2); adam.m_W3 = NULL; }
    if (adam.v_W3) { free_matrix(adam.v_W3, net.hidden2); adam.v_W3 = NULL; }
    if (adam.m_b3) { free(adam.m_b3); adam.m_b3 = NULL; }
    if (adam.v_b3) { free(adam.v_b3); adam.v_b3 = NULL; }
}

/*-----------------------*/
/* Forward Propagation   */
/*-----------------------*/
double relu(double x) {
    return x > 0 ? x : 0;
}

double relu_deriv(double x) {
    return x > 0 ? 1.0 : 0.0;
}

/* softmax with temperature */
void softmax_temp(double *z, int n, double temp) {
    double max = z[0] / temp;
    for (int i = 1; i < n; i++) {
        double v = z[i] / temp;
        if (v > max)
            max = v;
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        z[i] = exp(z[i] / temp - max);
        sum += z[i];
    }
    for (int i = 0; i < n; i++)
        z[i] /= sum;
}

ForwardCache forward_prop(int context[CONTEXT_LENGTH]) {
    ForwardCache cache;
    int input_dim = CONTEXT_LENGTH * net.emb_dim;
    cache.x = alloc_vector(input_dim);
    for (int i = 0; i < CONTEXT_LENGTH; i++) {
        for (int j = 0; j < net.emb_dim; j++) {
            cache.x[i * net.emb_dim + j] = net.embedding[ context[i] ][j];
        }
    }
    cache.z1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++) {
        cache.z1[j] = net.b1[j];
        for (int i = 0; i < input_dim; i++)
            cache.z1[j] += cache.x[i] * net.W1[i][j];
    }
    cache.a1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++)
        cache.a1[j] = relu(cache.z1[j]);
    cache.z2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++) {
        cache.z2[j] = net.b2[j];
        for (int i = 0; i < net.hidden1; i++)
            cache.z2[j] += cache.a1[i] * net.W2[i][j];
    }
    cache.a2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++)
        cache.a2[j] = relu(cache.z2[j]);
    cache.z3 = alloc_vector(net.vocab_size);
    for (int j = 0; j < net.vocab_size; j++) {
        cache.z3[j] = net.b3[j];
        for (int i = 0; i < net.hidden2; i++)
            cache.z3[j] += cache.a2[i] * net.W3[i][j];
    }
    cache.y = alloc_vector(net.vocab_size);
    memcpy(cache.y, cache.z3, sizeof(double) * net.vocab_size);
    double max = cache.y[0];
    for (int i = 1; i < net.vocab_size; i++)
        if (cache.y[i] > max)
            max = cache.y[i];
    double sum = 0.0;
    for (int i = 0; i < net.vocab_size; i++) {
        cache.y[i] = exp(cache.y[i] - max);
        sum += cache.y[i];
    }
    for (int i = 0; i < net.vocab_size; i++)
        cache.y[i] /= sum;
    return cache;
}

ForwardCache forward_prop_raw(int context[CONTEXT_LENGTH]) {
    ForwardCache cache;
    int input_dim = CONTEXT_LENGTH * net.emb_dim;
    cache.x = alloc_vector(input_dim);
    for (int i = 0; i < CONTEXT_LENGTH; i++) {
        for (int j = 0; j < net.emb_dim; j++) {
            cache.x[i * net.emb_dim + j] = net.embedding[ context[i] ][j];
        }
    }
    cache.z1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++) {
        cache.z1[j] = net.b1[j];
        for (int i = 0; i < input_dim; i++)
            cache.z1[j] += cache.x[i] * net.W1[i][j];
    }
    cache.a1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++)
        cache.a1[j] = relu(cache.z1[j]);
    cache.z2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++) {
        cache.z2[j] = net.b2[j];
        for (int i = 0; i < net.hidden1; i++)
            cache.z2[j] += cache.a1[i] * net.W2[i][j];
    }
    cache.a2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++)
        cache.a2[j] = relu(cache.z2[j]);
    cache.z3 = alloc_vector(net.vocab_size);
    for (int j = 0; j < net.vocab_size; j++) {
        cache.z3[j] = net.b3[j];
        for (int i = 0; i < net.hidden2; i++)
            cache.z3[j] += cache.a2[i] * net.W3[i][j];
    }
    cache.y = NULL;
    return cache;
}

void free_forward_cache(ForwardCache *cache) {
    if (cache->x) free(cache->x);
    if (cache->z1) free(cache->z1);
    if (cache->a1) free(cache->a1);
    if (cache->z2) free(cache->z2);
    if (cache->a2) free(cache->a2);
    if (cache->z3) free(cache->z3);
    if (cache->y) free(cache->y);
}

/*-----------------------*/
/* Dynamic Temperature   */
/*-----------------------*/
double compute_temperature(const char *input) {
    char copy[MAX_INPUT_SIZE];
    strncpy(copy, input, MAX_INPUT_SIZE);
    copy[MAX_INPUT_SIZE - 1] = '\0';
    char *tokens[MAX_TOKENS];
    int count = tokenize(copy, tokens, MAX_TOKENS);
    if(count < CONTEXT_LENGTH)
        return 1.5;
    else
        return 1.0;
}

/*-----------------------*/
/* Gradient Computation  */
/*-----------------------*/
Gradients *alloc_gradients() {
    Gradients *g = malloc(sizeof(Gradients));
    g->d_embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    g->d_W1 = alloc_matrix(CONTEXT_LENGTH * net.emb_dim, net.hidden1);
    g->d_b1 = alloc_vector(net.hidden1);
    g->d_W2 = alloc_matrix(net.hidden1, net.hidden2);
    g->d_b2 = alloc_vector(net.hidden2);
    g->d_W3 = alloc_matrix(net.hidden2, net.vocab_size);
    g->d_b3 = alloc_vector(net.vocab_size);
    int i, j;
    for (i = 0; i < net.vocab_size; i++)
        for (j = 0; j < net.emb_dim; j++)
            g->d_embedding[i][j] = 0.0;
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++)
        for (j = 0; j < net.hidden1; j++)
            g->d_W1[i][j] = 0.0;
    for (j = 0; j < net.hidden1; j++)
        g->d_b1[j] = 0.0;
    for (i = 0; i < net.hidden1; i++)
        for (j = 0; j < net.hidden2; j++)
            g->d_W2[i][j] = 0.0;
    for (j = 0; j < net.hidden2; j++)
        g->d_b2[j] = 0.0;
    for (i = 0; i < net.hidden2; i++)
        for (j = 0; j < net.vocab_size; j++)
            g->d_W3[i][j] = 0.0;
    for (j = 0; j < net.vocab_size; j++)
        g->d_b3[j] = 0.0;
    return g;
}

void free_gradients(Gradients *g) {
    free_matrix(g->d_embedding, net.vocab_size);
    free_matrix(g->d_W1, CONTEXT_LENGTH * net.emb_dim);
    free(g->d_b1);
    free_matrix(g->d_W2, net.hidden1);
    free(g->d_b2);
    free_matrix(g->d_W3, net.hidden2);
    free(g->d_b3);
    free(g);
}

Gradients *compute_gradients(int context[CONTEXT_LENGTH], int target) {
    ForwardCache cache = forward_prop(context);
    Gradients *g = alloc_gradients();
    int input_dim = CONTEXT_LENGTH * net.emb_dim;
    int vocab_size = net.vocab_size;
    double *t = alloc_vector(vocab_size);
    int i, j;
    for (i = 0; i < vocab_size; i++) t[i] = 0.0;
    t[target] = 1.0;
    double *dz3 = alloc_vector(vocab_size);
    for (j = 0; j < vocab_size; j++)
        dz3[j] = cache.y[j] - t[j];
    for (i = 0; i < net.hidden2; i++) {
        for (j = 0; j < vocab_size; j++) {
            g->d_W3[i][j] = cache.a2[i] * dz3[j];
        }
    }
    for (j = 0; j < vocab_size; j++) {
        g->d_b3[j] = dz3[j];
    }
    double *da2 = alloc_vector(net.hidden2);
    for (i = 0; i < net.hidden2; i++) {
        da2[i] = 0.0;
        for (j = 0; j < vocab_size; j++)
            da2[i] += dz3[j] * net.W3[i][j];
    }
    double *dz2 = alloc_vector(net.hidden2);
    for (i = 0; i < net.hidden2; i++)
        dz2[i] = da2[i] * relu_deriv(cache.z2[i]);
    for (i = 0; i < net.hidden1; i++) {
        for (j = 0; j < net.hidden2; j++) {
            g->d_W2[i][j] = cache.a1[i] * dz2[j];
        }
    }
    for (j = 0; j < net.hidden2; j++) {
        g->d_b2[j] = dz2[j];
    }
    double *da1 = alloc_vector(net.hidden1);
    for (i = 0; i < net.hidden1; i++) {
        da1[i] = 0.0;
        for (j = 0; j < net.hidden2; j++)
            da1[i] += dz2[j] * net.W2[i][j];
    }
    double *dz1 = alloc_vector(net.hidden1);
    for (i = 0; i < net.hidden1; i++)
        dz1[i] = da1[i] * relu_deriv(cache.z1[i]);
    for (i = 0; i < input_dim; i++) {
        for (j = 0; j < net.hidden1; j++) {
            g->d_W1[i][j] = cache.x[i] * dz1[j];
        }
    }
    for (j = 0; j < net.hidden1; j++) {
        g->d_b1[j] = dz1[j];
    }
    for (int k = 0; k < CONTEXT_LENGTH; k++) {
        for (i = 0; i < net.emb_dim; i++) {
            for (j = 0; j < net.hidden1; j++) {
                g->d_embedding[ context[k] ][i] += net.W1[k * net.emb_dim + i][j] * dz1[j];
            }
        }
    }
    free(t);
    free(dz3);
    free(da2);
    free(dz2);
    free(da1);
    free(dz1);
    free_forward_cache(&cache);
    return g;
}

void accumulate_gradients(Gradients *batch_grad, Gradients *g) {
    int i, j;
    for (i = 0; i < net.vocab_size; i++)
        for (j = 0; j < net.emb_dim; j++)
            batch_grad->d_embedding[i][j] += g->d_embedding[i][j];
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++)
        for (j = 0; j < net.hidden1; j++)
            batch_grad->d_W1[i][j] += g->d_W1[i][j];
    for (j = 0; j < net.hidden1; j++)
        batch_grad->d_b1[j] += g->d_b1[j];
    for (i = 0; i < net.hidden1; i++)
        for (j = 0; j < net.hidden2; j++)
            batch_grad->d_W2[i][j] += g->d_W2[i][j];
    for (j = 0; j < net.hidden2; j++)
        batch_grad->d_b2[j] += g->d_b2[j];
    for (i = 0; i < net.hidden2; i++)
        for (j = 0; j < net.vocab_size; j++)
            batch_grad->d_W3[i][j] += g->d_W3[i][j];
    for (j = 0; j < net.vocab_size; j++)
        batch_grad->d_b3[j] += g->d_b3[j];
}

/*-----------------------*/
/* Parameter Update with Adam (Mini-Batch) */
/*-----------------------*/
void update_parameters(Gradients *avg_grad) {
    adam.t++;
    double beta1t_denom = 1 - pow(BETA1, adam.t);
    double beta2t_denom = 1 - pow(BETA2, adam.t);
    int i, j;
    for (i = 0; i < net.vocab_size; i++) {
        for (j = 0; j < net.emb_dim; j++) {
            double grad = avg_grad->d_embedding[i][j];
            adam.m_embedding[i][j] = BETA1 * adam.m_embedding[i][j] + (1 - BETA1) * grad;
            adam.v_embedding[i][j] = BETA2 * adam.v_embedding[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_embedding[i][j] / beta1t_denom;
            double v_hat = adam.v_embedding[i][j] / beta2t_denom;
            net.embedding[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++) {
        for (j = 0; j < net.hidden1; j++) {
            double grad = avg_grad->d_W1[i][j];
            adam.m_W1[i][j] = BETA1 * adam.m_W1[i][j] + (1 - BETA1) * grad;
            adam.v_W1[i][j] = BETA2 * adam.v_W1[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W1[i][j] / beta1t_denom;
            double v_hat = adam.v_W1[i][j] / beta2t_denom;
            net.W1[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (j = 0; j < net.hidden1; j++) {
        double grad = avg_grad->d_b1[j];
        adam.m_b1[j] = BETA1 * adam.m_b1[j] + (1 - BETA1) * grad;
        adam.v_b1[j] = BETA2 * adam.v_b1[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b1[j] / beta1t_denom;
        double v_hat = adam.v_b1[j] / beta2t_denom;
        net.b1[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    for (i = 0; i < net.hidden1; i++) {
        for (j = 0; j < net.hidden2; j++) {
            double grad = avg_grad->d_W2[i][j];
            adam.m_W2[i][j] = BETA1 * adam.m_W2[i][j] + (1 - BETA1) * grad;
            adam.v_W2[i][j] = BETA2 * adam.v_W2[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W2[i][j] / beta1t_denom;
            double v_hat = adam.v_W2[i][j] / beta2t_denom;
            net.W2[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (j = 0; j < net.hidden2; j++) {
        double grad = avg_grad->d_b2[j];
        adam.m_b2[j] = BETA1 * adam.m_b2[j] + (1 - BETA1) * grad;
        adam.v_b2[j] = BETA2 * adam.v_b2[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b2[j] / beta1t_denom;
        double v_hat = adam.v_b2[j] / beta2t_denom;
        net.b2[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    for (i = 0; i < net.hidden2; i++) {
        for (j = 0; j < net.vocab_size; j++) {
            double grad = avg_grad->d_W3[i][j];
            adam.m_W3[i][j] = BETA1 * adam.m_W3[i][j] + (1 - BETA1) * grad;
            adam.v_W3[i][j] = BETA2 * adam.v_W3[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W3[i][j] / beta1t_denom;
            double v_hat = adam.v_W3[i][j] / beta2t_denom;
            net.W3[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (j = 0; j < net.vocab_size; j++) {
        double grad = avg_grad->d_b3[j];
        adam.m_b3[j] = BETA1 * adam.m_b3[j] + (1 - BETA1) * grad;
        adam.v_b3[j] = BETA2 * adam.v_b3[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b3[j] / beta1t_denom;
        double v_hat = adam.v_b3[j] / beta2t_denom;
        net.b3[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
}

/*-----------------------*/
/* Training Functions    */
/*-----------------------*/
void train_on_batch(TrainingExample batch[], int batch_size) {
    Gradients *batch_grad = alloc_gradients();
    int i;
    for (i = 0; i < batch_size; i++) {
        Gradients *g = compute_gradients(batch[i].context, batch[i].target);
        accumulate_gradients(batch_grad, g);
        free_gradients(g);
    }
    int j;
    for (i = 0; i < net.vocab_size; i++)
        for (j = 0; j < net.emb_dim; j++)
            batch_grad->d_embedding[i][j] /= batch_size;
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++)
        for (j = 0; j < net.hidden1; j++)
            batch_grad->d_W1[i][j] /= batch_size;
    for (j = 0; j < net.hidden1; j++)
        batch_grad->d_b1[j] /= batch_size;
    for (i = 0; i < net.hidden1; i++)
        for (j = 0; j < net.hidden2; j++)
            batch_grad->d_W2[i][j] /= batch_size;
    for (j = 0; j < net.hidden2; j++)
        batch_grad->d_b2[j] /= batch_size;
    for (i = 0; i < net.hidden2; i++)
        for (j = 0; j < net.vocab_size; j++)
            batch_grad->d_W3[i][j] /= batch_size;
    for (j = 0; j < net.vocab_size; j++)
        batch_grad->d_b3[j] /= batch_size;
    update_parameters(batch_grad);
    free_gradients(batch_grad);
}

/*-----------------------*/
/* Prediction (Inference)*/
/*-----------------------*/
int sample_prediction(int context[CONTEXT_LENGTH], const char *raw_input) {
    ForwardCache cache = forward_prop_raw(context);
    double temp = compute_temperature(raw_input);
    double *probs = alloc_vector(net.vocab_size);
    memcpy(probs, cache.z3, sizeof(double)*net.vocab_size);
    softmax_temp(probs, net.vocab_size, temp);
    double r = (double)rand() / RAND_MAX;
    double cumulative = 0.0;
    int pred = 0;
    int i;
    for (i = 0; i < net.vocab_size; i++) {
        cumulative += probs[i];
        if (r < cumulative) {
            pred = i;
            break;
        }
    }
    free(probs);
    free_forward_cache(&cache);
    return pred;
}

/*-----------------------*/
/* Model Persistence     */
/*-----------------------*/
void save_model(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Error: could not open %s for writing.\n", filename);
        return;
    }
    fprintf(fp, "%d\n", vocab.size);
    int i, j;
    for (i = 0; i < vocab.size; i++) {
        fprintf(fp, "%s\n", vocab.words[i]);
    }
    fprintf(fp, "%d %d %d %d\n", net.vocab_size, net.emb_dim, net.hidden1, net.hidden2);
    for (i = 0; i < net.vocab_size; i++) {
        for (j = 0; j < net.emb_dim; j++)
            fprintf(fp, "%lf ", net.embedding[i][j]);
        fprintf(fp, "\n");
    }
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++) {
        for (j = 0; j < net.hidden1; j++)
            fprintf(fp, "%lf ", net.W1[i][j]);
        fprintf(fp, "\n");
    }
    for (j = 0; j < net.hidden1; j++)
        fprintf(fp, "%lf ", net.b1[j]);
    fprintf(fp, "\n");
    for (i = 0; i < net.hidden1; i++) {
        for (j = 0; j < net.hidden2; j++)
            fprintf(fp, "%lf ", net.W2[i][j]);
        fprintf(fp, "\n");
    }
    for (j = 0; j < net.hidden2; j++)
        fprintf(fp, "%lf ", net.b2[j]);
    fprintf(fp, "\n");
    for (i = 0; i < net.hidden2; i++) {
        for (j = 0; j < net.vocab_size; j++)
            fprintf(fp, "%lf ", net.W3[i][j]);
        fprintf(fp, "\n");
    }
    for (j = 0; j < net.vocab_size; j++)
        fprintf(fp, "%lf ", net.b3[j]);
    fprintf(fp, "\n");
    fclose(fp);
}

void load_model(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) // file does not exist
        return;
    if (st.st_size == 0)
        return;
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return;
    int file_vocab_size;
    if (fscanf(fp, "%d", &file_vocab_size) != 1) {
        fclose(fp);
        return;
    }
    // Load existing vocabulary from file into a temporary array.
    // We assume that if a model exists, then the in-memory vocab will be updated by appending.
    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    for (int i = 0; i < file_vocab_size; i++) {
        if (!fgets(buffer, sizeof(buffer), fp))
            break;
        buffer[strcspn(buffer, "\n")] = '\0';
        // If the word is not already in the current vocabulary, append it.
        if (find_in_vocab(buffer) < 0) {
            add_word(buffer);
        }
    }
    // Read network dimensions from file.
    if (fscanf(fp, "%d %d %d %d", &net.vocab_size, &net.emb_dim, &net.hidden1, &net.hidden2) != 4) {
        fclose(fp);
        return;
    }
    // If a network already exists, free it.
    if (net.embedding != NULL)
        free_network();
    init_network();
    int i, j;
    for (i = 0; i < net.vocab_size; i++) {
        for (j = 0; j < net.emb_dim; j++)
            fscanf(fp, "%lf", &net.embedding[i][j]);
    }
    for (i = 0; i < CONTEXT_LENGTH * net.emb_dim; i++) {
        for (j = 0; j < net.hidden1; j++)
            fscanf(fp, "%lf", &net.W1[i][j]);
    }
    for (j = 0; j < net.hidden1; j++)
        fscanf(fp, "%lf", &net.b1[j]);
    for (i = 0; i < net.hidden1; i++) {
        for (j = 0; j < net.hidden2; j++)
            fscanf(fp, "%lf", &net.W2[i][j]);
    }
    for (j = 0; j < net.hidden2; j++)
        fscanf(fp, "%lf", &net.b2[j]);
    for (i = 0; i < net.hidden2; i++) {
        for (j = 0; j < net.vocab_size; j++)
            fscanf(fp, "%lf", &net.W3[i][j]);
    }
    for (j = 0; j < net.vocab_size; j++)
        fscanf(fp, "%lf", &net.b3[j]);
    fclose(fp);
}

/*-----------------------*/
/* Network Resizing      */
/*-----------------------*/
// This function resizes the network (and Adam states) if the new vocabulary size is larger.
void resize_network(int new_vocab_size) {
    if (new_vocab_size <= net.vocab_size)
        return;  // no resizing needed

    int old_vocab_size = net.vocab_size;
    int emb_dim = net.emb_dim;
    int hidden2 = net.hidden2;
    double r_emb = 0.5;
    double r_W3 = 0.5;

    // --- Resize the Embedding Matrix ---
    double **new_embedding = alloc_matrix(new_vocab_size, emb_dim);
    for (int i = 0; i < old_vocab_size; i++) {
        for (int j = 0; j < emb_dim; j++) {
            new_embedding[i][j] = net.embedding[i][j];
        }
    }
    for (int i = old_vocab_size; i < new_vocab_size; i++) {
        for (int j = 0; j < emb_dim; j++) {
            new_embedding[i][j] = rand_uniform(r_emb);
        }
    }
    free_matrix(net.embedding, net.vocab_size);
    net.embedding = new_embedding;

    // --- Resize the Output Layer weights W3 ---
    double **new_W3 = alloc_matrix(hidden2, new_vocab_size);
    for (int i = 0; i < hidden2; i++) {
        for (int j = 0; j < old_vocab_size; j++) {
            new_W3[i][j] = net.W3[i][j];
        }
        for (int j = old_vocab_size; j < new_vocab_size; j++) {
            new_W3[i][j] = rand_uniform(r_W3);
        }
    }
    free_matrix(net.W3, net.hidden2);
    net.W3 = new_W3;

    // --- Resize the output bias vector b3 ---
    double *new_b3 = alloc_vector(new_vocab_size);
    for (int j = 0; j < old_vocab_size; j++) {
        new_b3[j] = net.b3[j];
    }
    for (int j = old_vocab_size; j < new_vocab_size; j++) {
        new_b3[j] = 0.0;
    }
    free(net.b3);
    net.b3 = new_b3;

    // --- Update network vocabulary size ---
    net.vocab_size = new_vocab_size;

    // --- Resize Adam states for the embedding ---
    double **new_m_embedding = alloc_matrix(new_vocab_size, emb_dim);
    double **new_v_embedding = alloc_matrix(new_vocab_size, emb_dim);
    for (int i = 0; i < old_vocab_size; i++) {
        for (int j = 0; j < emb_dim; j++) {
            new_m_embedding[i][j] = adam.m_embedding[i][j];
            new_v_embedding[i][j] = adam.v_embedding[i][j];
        }
    }
    for (int i = old_vocab_size; i < new_vocab_size; i++) {
        for (int j = 0; j < emb_dim; j++) {
            new_m_embedding[i][j] = 0.0;
            new_v_embedding[i][j] = 0.0;
        }
    }
    free_matrix(adam.m_embedding, old_vocab_size);
    free_matrix(adam.v_embedding, old_vocab_size);
    adam.m_embedding = new_m_embedding;
    adam.v_embedding = new_v_embedding;

    // --- Resize Adam states for W3 ---
    double **new_m_W3 = alloc_matrix(hidden2, new_vocab_size);
    double **new_v_W3 = alloc_matrix(hidden2, new_vocab_size);
    for (int i = 0; i < hidden2; i++) {
        for (int j = 0; j < old_vocab_size; j++) {
            new_m_W3[i][j] = adam.m_W3[i][j];
            new_v_W3[i][j] = adam.v_W3[i][j];
        }
        for (int j = old_vocab_size; j < new_vocab_size; j++) {
            new_m_W3[i][j] = 0.0;
            new_v_W3[i][j] = 0.0;
        }
    }
    free_matrix(adam.m_W3, hidden2);
    free_matrix(adam.v_W3, hidden2);
    adam.m_W3 = new_m_W3;
    adam.v_W3 = new_v_W3;

    // --- Resize Adam states for b3 ---
    double *new_m_b3 = alloc_vector(new_vocab_size);
    double *new_v_b3 = alloc_vector(new_vocab_size);
    for (int j = 0; j < old_vocab_size; j++) {
        new_m_b3[j] = adam.m_b3[j];
        new_v_b3[j] = adam.v_b3[j];
    }
    for (int j = old_vocab_size; j < new_vocab_size; j++) {
        new_m_b3[j] = 0.0;
        new_v_b3[j] = 0.0;
    }
    free(adam.m_b3);
    free(adam.v_b3);
    adam.m_b3 = new_m_b3;
    adam.v_b3 = new_v_b3;
}

/*-----------------------*/
/* Training Data Builder */
/*-----------------------*/
void process_training_line(char *input, int allow_new_words) {
    char buffer[MAX_INPUT_SIZE*2];
    snprintf(buffer, sizeof(buffer), "%s %s %s", START_TOKEN, input, END_TOKEN);
    char *words[MAX_TOKENS];
    int count = tokenize(buffer, words, MAX_TOKENS);
    if (count < CONTEXT_LENGTH + 1)
        return;
    int indices[MAX_TOKENS];
    int i;
    for (i = 0; i < count; i++) {
        int idx = find_in_vocab(words[i]);
        if (idx < 0) {
            if (!allow_new_words) {
                fprintf(stderr, "Word '%s' is unknown. Please teach it first in automatic mode.\n", words[i]);
                return;
            }
            idx = add_word(words[i]);
        }
        indices[i] = idx;
    }
    int j;
    for (i = 0; i < count - CONTEXT_LENGTH; i++) {
        if (train_example_count >= MAX_TRAIN_EXAMPLES)
            break;
        for (j = 0; j < CONTEXT_LENGTH; j++) {
            train_examples[train_example_count].context[j] = indices[i+j];
        }
        train_examples[train_example_count].target = indices[i + CONTEXT_LENGTH];
        train_example_count++;
    }
}

void shuffle_training_examples() {
    int i;
    for (i = train_example_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        TrainingExample temp = train_examples[i];
        train_examples[i] = train_examples[j];
        train_examples[j] = temp;
    }
}

/*-----------------------*/
/* Helper for Run Mode   */
/*-----------------------*/
void humanize_response(char *response) {
    if (response[0] != '\0')
        response[0] = toupper((unsigned char)response[0]);
    size_t len = strlen(response);
    if (len > 0 && response[len-1] != '.' && response[len-1] != '!' && response[len-1] != '?')
        strncat(response, ".", MAX_INPUT_SIZE - strlen(response) - 1);
}

int is_question(const char *input) {
    size_t len = strlen(input);
    while (len > 0 && isspace((unsigned char)input[len-1]))
        len--;
    return (len > 0 && input[len-1] == '?');
}

/*-----------------------*/
/* API Functions         */
/*-----------------------*/
void cmd_teach_sv(char *model_filename) {
    char input[MAX_INPUT_SIZE];
    struct stat st;
    int fileExists = (stat(model_filename, &st) == 0);
    int fileEmpty = fileExists && st.st_size == 0;

    // Load model if file exists and is non-empty.
    if (fileExists && !fileEmpty) {
        load_model(model_filename);
    }
    // If vocabulary is empty (first time), initialize it.
    if (vocab.size == 0) {
        init_vocab();
        add_word(START_TOKEN);
        add_word(END_TOKEN);
    }
    // If network is not initialized (first time), do so.
    if (net.embedding == NULL)
        init_network();
    // If Adam state is not initialized, set it up.
    if (adam.m_embedding == NULL)
        init_adam();

    printf("Welcome to the NN Teaching Tool.\n");
    printf("Select teaching mode: (m)anual or (a)utomatic? ");
    if (!fgets(input, sizeof(input), stdin)) {
        fprintf(stderr, "Input error.\n");
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    trim_whitespace(input);

    if (input[0] == 'm' || input[0] == 'M') {
        printf("Manual teaching mode selected. (New words are not allowed.)\n");
        printf("Enter sentences to update the model. Type 'exit' to save and quit.\n");
        while (1) {
            printf("teach> ");
            if (!fgets(input, sizeof(input), stdin))
                break;
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strcmp(input, "exit") == 0)
                break;
            process_training_line(input, 0);
            int last = train_example_count - 1;
            if (last >= 0) {
                train_on_batch(&train_examples[last], 1);
            }
            printf("Processed and trained on the input line.\n");
        }
    } else {
        char materialFilename[256];
        FILE *materialFile;
        printf("Automatic teaching mode selected.\n");
        printf("Enter the filename for teaching material (e.g., material.txt): ");
        if (!fgets(materialFilename, sizeof(materialFilename), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        materialFilename[strcspn(materialFilename, "\n")] = '\0';
        trim_whitespace(materialFilename);
        materialFile = fopen(materialFilename, "r");
        if (!materialFile) {
            fprintf(stderr, "Error: Could not open file %s\n", materialFilename);
            return;
        }
        while (fgets(input, sizeof(input), materialFile)) {
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strlen(input) > 0)
                process_training_line(input, 1);
        }
        fclose(materialFile);
        printf("Built vocabulary of size %d with %d training examples.\n", vocab.size, train_example_count);

        // Resize the network if new words have been appended.
        if (vocab.size > net.vocab_size) {
            resize_network(vocab.size);
        }

        char epochs_str[32];
        int num_epochs = 0;
        printf("Enter the number of epochs for training: ");
        if (!fgets(epochs_str, sizeof(epochs_str), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        num_epochs = atoi(epochs_str);
        if (num_epochs <= 0) {
            num_epochs = DEFAULT_EPOCHS;
            printf("Using default %d epochs.\n", DEFAULT_EPOCHS);
        }
        char batch_str[32];
        int batch_size = DEFAULT_BATCH_SIZE;
        printf("Enter mini-batch size (default %d): ", DEFAULT_BATCH_SIZE);
        if (fgets(batch_str, sizeof(batch_str), stdin)) {
            int bs = atoi(batch_str);
            if (bs > 0)
                batch_size = bs;
        }
        for (int epoch = 0; epoch < num_epochs; epoch++) {
            shuffle_training_examples();
            for (int i = 0; i < train_example_count; i += batch_size) {
                int current_batch = (i + batch_size <= train_example_count) ? batch_size : (train_example_count - i);
                train_on_batch(&train_examples[i], current_batch);
            }
            printf("Epoch %d completed.\n", epoch + 1);
            learning_rate *= LR_DECAY_FACTOR;
        }
    }
    save_model(model_filename);
    printf("Model saved to %s.\n", model_filename);
}

void cmd_run_sv(char *model_filename) {
    char input[MAX_INPUT_SIZE];
    char response[MAX_INPUT_SIZE] = {0};
    load_model(model_filename);
    if (vocab.size == 0 || net.vocab_size == 0) {
        fprintf(stderr, "No model found. Please teach first.\n");
        return;
    }
    printf("Entering run mode. Type a sentence to receive predictions.\n");
    printf("Type 'exit' to quit.\n");
    while (1) {
        printf("run> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);
        if (strcmp(input, "exit") == 0)
            break;
        char input_copy[MAX_INPUT_SIZE];
        strncpy(input_copy, input, sizeof(input_copy)-1);
        input_copy[sizeof(input_copy)-1] = '\0';
        char *tokens[MAX_TOKENS];
        int count = tokenize(input_copy, tokens, MAX_TOKENS);
        if (count < CONTEXT_LENGTH) {
            printf("Not enough context. Please enter at least %d words.\n", CONTEXT_LENGTH);
            continue;
        }
        int context[CONTEXT_LENGTH];
        int valid = 1;
        for (int i = 0; i < CONTEXT_LENGTH; i++) {
            context[i] = find_in_vocab(tokens[count - CONTEXT_LENGTH + i]);
            if (context[i] < 0) {
                printf("Unknown word '%s' in context. Please teach it first.\n", tokens[count - CONTEXT_LENGTH + i]);
                valid = 0;
                break;
            }
        }
        if (!valid)
            continue;
        response[0] = '\0';
        if (is_question(input)) {
            const char *question_prefixes[] = {"I think", "Well", "Perhaps", "In my opinion"};
            int num_prefixes = sizeof(question_prefixes) / sizeof(question_prefixes[0]);
            int idx = rand() % num_prefixes;
            strncat(response, question_prefixes[idx], sizeof(response)-strlen(response)-1);
        }
        int pred = sample_prediction(context, input);
        if (pred < 0 || pred >= vocab.size ||
            strcmp(vocab.words[pred], START_TOKEN) == 0 ||
            strcmp(vocab.words[pred], END_TOKEN) == 0) {
            printf("No valid prediction.\n");
            continue;
        }
        strncat(response, " ", sizeof(response)-strlen(response)-1);
        strncat(response, vocab.words[pred], sizeof(response)-strlen(response)-1);
        int current_context[CONTEXT_LENGTH];
        for (int i = 0; i < CONTEXT_LENGTH - 1; i++) {
            current_context[i] = context[i+1];
        }
        current_context[CONTEXT_LENGTH - 1] = pred;
        for (int i = 1; i < MAX_PREDICT_WORDS; i++) {
            int next_pred = sample_prediction(current_context, input);
            if (next_pred < 0 || next_pred >= vocab.size ||
                strcmp(vocab.words[next_pred], START_TOKEN) == 0 ||
                strcmp(vocab.words[next_pred], END_TOKEN) == 0)
                break;
            strncat(response, " ", sizeof(response)-strlen(response)-1);
            strncat(response, vocab.words[next_pred], sizeof(response)-strlen(response)-1);
            for (int j = 0; j < CONTEXT_LENGTH - 1; j++) {
                current_context[j] = current_context[j+1];
            }
            current_context[CONTEXT_LENGTH - 1] = next_pred;
        }
        humanize_response(response);
        printf("Prediction: %s\n", response);
    }
}

/*-----------------------*/
/* Main (for Testing)    */
/*-----------------------*/
#ifdef TEST_MAIN
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <mode: teach|run> <model_filename>\n", argv[0]);
        return 1;
    }
    // Seed random number generator once for the entire run.
    srand((unsigned int)time(NULL));

    if (strcmp(argv[1], "teach") == 0)
        cmd_teach_sv(argv[2]);
    else if (strcmp(argv[1], "run") == 0)
        cmd_run_sv(argv[2]);
    else
        fprintf(stderr, "Unknown mode: %s. Use 'teach' or 'run'.\n", argv[1]);

    free_network();
    free_adam();
    free_vocab();
    return 0;
}
#endif
