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
/* Increased model capacity */
#define EMBEDDING_DIM       50
#define HIDDEN_SIZE1        128
#define HIDDEN_SIZE2        128

#define MAX_WORD_LEN        50
#define MAX_INPUT_SIZE      1000
#define MAX_TOKENS          1000
#define MAX_VOCAB_SIZE      10000
#define MAX_TRAIN_EXAMPLES  100000

#define INITIAL_LEARNING_RATE  0.01
#define DEFAULT_EPOCHS         30
#define LR_DECAY_FACTOR        0.95

#define DEFAULT_BATCH_SIZE     32
#define MAX_PREDICT_WORDS      10

/* Adam optimizer hyperparameters */
#define BETA1    0.9
#define BETA2    0.999
#define EPSILON  1e-8

/* Special tokens */
#define START_TOKEN "<s>"
#define END_TOKEN   "</s>"

/*-----------------------*/
/* Data Structures       */
/*-----------------------*/
typedef struct {
    char *words[MAX_VOCAB_SIZE];
    int size;
} Vocabulary;

typedef struct {
    int context[2];
    int target;
} TrainingExample;

typedef struct {
    int vocab_size;
    int emb_dim;
    int hidden1;
    int hidden2;
    /* Embedding: vocab_size x emb_dim */
    double **embedding;  // embedding[i][j]
    /* Layer 1: from concatenated embedding (2*emb_dim) to hidden1 */
    double **W1;         // (2*emb_dim) x hidden1
    double *b1;          // hidden1
    /* Layer 2: from hidden1 to hidden2 */
    double **W2;         // hidden1 x hidden2
    double *b2;          // hidden2
    /* Output layer: from hidden2 to vocab_size */
    double **W3;         // hidden2 x vocab_size
    double *b3;          // vocab_size
} NeuralNetwork;

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

/* For mini-batch training, we compute (raw) gradients and then average */
typedef struct {
    double **d_embedding; // shape: [vocab_size][emb_dim]
    double **d_W1;        // shape: [2*emb_dim][hidden1]
    double *d_b1;         // shape: [hidden1]
    double **d_W2;        // shape: [hidden1][hidden2]
    double *d_b2;         // shape: [hidden2]
    double **d_W3;        // shape: [hidden2][vocab_size]
    double *d_b3;         // shape: [vocab_size]
} Gradients;

/* For forward propagation caching */
typedef struct {
    double *x;
    double *z1;
    double *a1;
    double *z2;
    double *a2;
    double *z3;
    double *y;
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
    // Remove leading punctuation
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
    // Remove trailing punctuation
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
        if (strlen(token) > 0) {
            words[count++] = token;
        }
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

    net.W1 = alloc_matrix(2 * net.emb_dim, net.hidden1);
    net.b1 = alloc_vector(net.hidden1);
    double r_W1 = 0.5;
    for (int i = 0; i < 2 * net.emb_dim; i++)
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
    adam.m_W1 = alloc_matrix(2 * net.emb_dim, net.hidden1);
    adam.v_W1 = alloc_matrix(2 * net.emb_dim, net.hidden1);
    adam.m_b1 = alloc_vector(net.hidden1);
    adam.v_b1 = alloc_vector(net.hidden1);
    for (int i = 0; i < 2 * net.emb_dim; i++)
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
    if (net.W1) { free_matrix(net.W1, 2 * net.emb_dim); net.W1 = NULL; }
    if (net.b1) { free(net.b1); net.b1 = NULL; }
    if (net.W2) { free_matrix(net.W2, net.hidden1); net.W2 = NULL; }
    if (net.b2) { free(net.b2); net.b2 = NULL; }
    if (net.W3) { free_matrix(net.W3, net.hidden2); net.W3 = NULL; }
    if (net.b3) { free(net.b3); net.b3 = NULL; }
}

void free_adam() {
    if (adam.m_embedding) { free_matrix(adam.m_embedding, net.vocab_size); adam.m_embedding = NULL; }
    if (adam.v_embedding) { free_matrix(adam.v_embedding, net.vocab_size); adam.v_embedding = NULL; }
    if (adam.m_W1) { free_matrix(adam.m_W1, 2 * net.emb_dim); adam.m_W1 = NULL; }
    if (adam.v_W1) { free_matrix(adam.v_W1, 2 * net.emb_dim); adam.v_W1 = NULL; }
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
void softmax(double *z, int n) {
    double max = z[0];
    for (int i = 1; i < n; i++)
        if (z[i] > max)
            max = z[i];
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        z[i] = exp(z[i] - max);
        sum += z[i];
    }
    for (int i = 0; i < n; i++)
        z[i] /= sum;
}

ForwardCache forward_prop(int context[2]) {
    ForwardCache cache;
    int input_dim = 2 * net.emb_dim;
    cache.x = alloc_vector(input_dim);
    for (int i = 0; i < net.emb_dim; i++) {
        cache.x[i] = net.embedding[context[0]][i];
        cache.x[i + net.emb_dim] = net.embedding[context[1]][i];
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
    memcpy(cache.y, cache.z3, sizeof(double)*net.vocab_size);
    softmax(cache.y, net.vocab_size);
    return cache;
}

void free_forward_cache(ForwardCache *cache) {
    free(cache->x);
    free(cache->z1);
    free(cache->a1);
    free(cache->z2);
    free(cache->a2);
    free(cache->z3);
    free(cache->y);
}

/*-----------------------*/
/* Gradient Computation  */
/*-----------------------*/
Gradients *alloc_gradients() {
    Gradients *g = malloc(sizeof(Gradients));
    g->d_embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    g->d_W1 = alloc_matrix(2 * net.emb_dim, net.hidden1);
    g->d_b1 = alloc_vector(net.hidden1);
    g->d_W2 = alloc_matrix(net.hidden1, net.hidden2);
    g->d_b2 = alloc_vector(net.hidden2);
    g->d_W3 = alloc_matrix(net.hidden2, net.vocab_size);
    g->d_b3 = alloc_vector(net.vocab_size);
    /* Initialize all gradients to 0 */
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++)
            g->d_embedding[i][j] = 0.0;
    for (int i = 0; i < 2 * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++)
            g->d_W1[i][j] = 0.0;
    for (int j = 0; j < net.hidden1; j++)
        g->d_b1[j] = 0.0;
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++)
            g->d_W2[i][j] = 0.0;
    for (int j = 0; j < net.hidden2; j++)
        g->d_b2[j] = 0.0;
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++)
            g->d_W3[i][j] = 0.0;
    for (int j = 0; j < net.vocab_size; j++)
        g->d_b3[j] = 0.0;
    return g;
}

void free_gradients(Gradients *g) {
    free_matrix(g->d_embedding, net.vocab_size);
    free_matrix(g->d_W1, 2 * net.emb_dim);
    free(g->d_b1);
    free_matrix(g->d_W2, net.hidden1);
    free(g->d_b2);
    free_matrix(g->d_W3, net.hidden2);
    free(g->d_b3);
    free(g);
}

/* Compute gradients for one training example (without updating parameters) */
Gradients *compute_gradients(int context[2], int target) {
    ForwardCache cache = forward_prop(context);
    Gradients *g = alloc_gradients();
    int input_dim = 2 * net.emb_dim;
    int vocab_size = net.vocab_size;

    /* One-hot target */
    double *t = alloc_vector(vocab_size);
    for (int i = 0; i < vocab_size; i++) t[i] = 0.0;
    t[target] = 1.0;

    /* dz3 = y - t */
    double *dz3 = alloc_vector(vocab_size);
    for (int j = 0; j < vocab_size; j++)
        dz3[j] = cache.y[j] - t[j];

    /* Gradients for W3 and b3 */
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < vocab_size; j++) {
            g->d_W3[i][j] = cache.a2[i] * dz3[j];
        }
    }
    for (int j = 0; j < vocab_size; j++) {
        g->d_b3[j] = dz3[j];
    }

    /* Backprop into layer 2 */
    double *da2 = alloc_vector(net.hidden2);
    for (int i = 0; i < net.hidden2; i++) {
        da2[i] = 0.0;
        for (int j = 0; j < vocab_size; j++)
            da2[i] += dz3[j] * net.W3[i][j];
    }
    double *dz2 = alloc_vector(net.hidden2);
    for (int i = 0; i < net.hidden2; i++)
        dz2[i] = da2[i] * relu_deriv(cache.z2[i]);

    /* Gradients for W2 and b2 */
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++) {
            g->d_W2[i][j] = cache.a1[i] * dz2[j];
        }
    }
    for (int j = 0; j < net.hidden2; j++) {
        g->d_b2[j] = dz2[j];
    }

    /* Backprop into layer 1 */
    double *da1 = alloc_vector(net.hidden1);
    for (int i = 0; i < net.hidden1; i++) {
        da1[i] = 0.0;
        for (int j = 0; j < net.hidden2; j++)
            da1[i] += dz2[j] * net.W2[i][j];
    }
    double *dz1 = alloc_vector(net.hidden1);
    for (int i = 0; i < net.hidden1; i++)
        dz1[i] = da1[i] * relu_deriv(cache.z1[i]);

    /* Gradients for W1 and b1 */
    for (int i = 0; i < input_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            g->d_W1[i][j] = cache.x[i] * dz1[j];
        }
    }
    for (int j = 0; j < net.hidden1; j++) {
        g->d_b1[j] = dz1[j];
    }

    /* Gradients for embeddings (for the two context words) */
    for (int i = 0; i < net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            g->d_embedding[context[0]][i] += net.W1[i][j] * dz1[j];
            g->d_embedding[context[1]][i] += net.W1[i + net.emb_dim][j] * dz1[j];
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
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++)
            batch_grad->d_embedding[i][j] += g->d_embedding[i][j];
    for (int i = 0; i < 2 * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++)
            batch_grad->d_W1[i][j] += g->d_W1[i][j];
    for (int j = 0; j < net.hidden1; j++)
        batch_grad->d_b1[j] += g->d_b1[j];
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++)
            batch_grad->d_W2[i][j] += g->d_W2[i][j];
    for (int j = 0; j < net.hidden2; j++)
        batch_grad->d_b2[j] += g->d_b2[j];
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++)
            batch_grad->d_W3[i][j] += g->d_W3[i][j];
    for (int j = 0; j < net.vocab_size; j++)
        batch_grad->d_b3[j] += g->d_b3[j];
}

/*-----------------------*/
/* Parameter Update with Adam (Mini-Batch) */
/*-----------------------*/
void update_parameters(Gradients *avg_grad) {
    adam.t++; // one update per batch
    /* For each parameter update using averaged gradients */
    // Embedding update:
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++) {
            double grad = avg_grad->d_embedding[i][j];
            adam.m_embedding[i][j] = BETA1 * adam.m_embedding[i][j] + (1 - BETA1) * grad;
            adam.v_embedding[i][j] = BETA2 * adam.v_embedding[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_embedding[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_embedding[i][j] / (1 - pow(BETA2, adam.t));
            net.embedding[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    // W1 and b1:
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            double grad = avg_grad->d_W1[i][j];
            adam.m_W1[i][j] = BETA1 * adam.m_W1[i][j] + (1 - BETA1) * grad;
            adam.v_W1[i][j] = BETA2 * adam.v_W1[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W1[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W1[i][j] / (1 - pow(BETA2, adam.t));
            net.W1[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.hidden1; j++) {
        double grad = avg_grad->d_b1[j];
        adam.m_b1[j] = BETA1 * adam.m_b1[j] + (1 - BETA1) * grad;
        adam.v_b1[j] = BETA2 * adam.v_b1[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b1[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b1[j] / (1 - pow(BETA2, adam.t));
        net.b1[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    // W2 and b2:
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++) {
            double grad = avg_grad->d_W2[i][j];
            adam.m_W2[i][j] = BETA1 * adam.m_W2[i][j] + (1 - BETA1) * grad;
            adam.v_W2[i][j] = BETA2 * adam.v_W2[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W2[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W2[i][j] / (1 - pow(BETA2, adam.t));
            net.W2[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.hidden2; j++) {
        double grad = avg_grad->d_b2[j];
        adam.m_b2[j] = BETA1 * adam.m_b2[j] + (1 - BETA1) * grad;
        adam.v_b2[j] = BETA2 * adam.v_b2[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b2[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b2[j] / (1 - pow(BETA2, adam.t));
        net.b2[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    // W3 and b3:
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < net.vocab_size; j++) {
            double grad = avg_grad->d_W3[i][j];
            adam.m_W3[i][j] = BETA1 * adam.m_W3[i][j] + (1 - BETA1) * grad;
            adam.v_W3[i][j] = BETA2 * adam.v_W3[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W3[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W3[i][j] / (1 - pow(BETA2, adam.t));
            net.W3[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.vocab_size; j++) {
        double grad = avg_grad->d_b3[j];
        adam.m_b3[j] = BETA1 * adam.m_b3[j] + (1 - BETA1) * grad;
        adam.v_b3[j] = BETA2 * adam.v_b3[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b3[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b3[j] / (1 - pow(BETA2, adam.t));
        net.b3[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
}

/*-----------------------*/
/* Training Functions    */
/*-----------------------*/
/* Single-example training (manual mode) */
void train_on_example(int context[2], int target) {
    Gradients *g = compute_gradients(context, target);
    /* Use Adam update immediately */
    adam.t++;
    // Update for each parameter (similar to update_parameters, but using g directly)
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++) {
            double grad = g->d_embedding[i][j];
            adam.m_embedding[i][j] = BETA1 * adam.m_embedding[i][j] + (1 - BETA1) * grad;
            adam.v_embedding[i][j] = BETA2 * adam.v_embedding[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_embedding[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_embedding[i][j] / (1 - pow(BETA2, adam.t));
            net.embedding[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            double grad = g->d_W1[i][j];
            adam.m_W1[i][j] = BETA1 * adam.m_W1[i][j] + (1 - BETA1) * grad;
            adam.v_W1[i][j] = BETA2 * adam.v_W1[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W1[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W1[i][j] / (1 - pow(BETA2, adam.t));
            net.W1[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.hidden1; j++) {
        double grad = g->d_b1[j];
        adam.m_b1[j] = BETA1 * adam.m_b1[j] + (1 - BETA1) * grad;
        adam.v_b1[j] = BETA2 * adam.v_b1[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b1[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b1[j] / (1 - pow(BETA2, adam.t));
        net.b1[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++) {
            double grad = g->d_W2[i][j];
            adam.m_W2[i][j] = BETA1 * adam.m_W2[i][j] + (1 - BETA1) * grad;
            adam.v_W2[i][j] = BETA2 * adam.v_W2[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W2[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W2[i][j] / (1 - pow(BETA2, adam.t));
            net.W2[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.hidden2; j++) {
        double grad = g->d_b2[j];
        adam.m_b2[j] = BETA1 * adam.m_b2[j] + (1 - BETA1) * grad;
        adam.v_b2[j] = BETA2 * adam.v_b2[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b2[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b2[j] / (1 - pow(BETA2, adam.t));
        net.b2[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < net.vocab_size; j++) {
            double grad = g->d_W3[i][j];
            adam.m_W3[i][j] = BETA1 * adam.m_W3[i][j] + (1 - BETA1) * grad;
            adam.v_W3[i][j] = BETA2 * adam.v_W3[i][j] + (1 - BETA2) * grad * grad;
            double m_hat = adam.m_W3[i][j] / (1 - pow(BETA1, adam.t));
            double v_hat = adam.v_W3[i][j] / (1 - pow(BETA2, adam.t));
            net.W3[i][j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
        }
    }
    for (int j = 0; j < net.vocab_size; j++) {
        double grad = g->d_b3[j];
        adam.m_b3[j] = BETA1 * adam.m_b3[j] + (1 - BETA1) * grad;
        adam.v_b3[j] = BETA2 * adam.v_b3[j] + (1 - BETA2) * grad * grad;
        double m_hat = adam.m_b3[j] / (1 - pow(BETA1, adam.t));
        double v_hat = adam.v_b3[j] / (1 - pow(BETA2, adam.t));
        net.b3[j] -= learning_rate * m_hat / (sqrt(v_hat) + EPSILON);
    }
    free_gradients(g);
}

/* Mini-batch training: compute averaged gradients over the batch and update parameters */
void train_on_batch(TrainingExample batch[], int batch_size) {
    Gradients *batch_grad = alloc_gradients();
    for (int i = 0; i < batch_size; i++) {
        Gradients *g = compute_gradients(batch[i].context, batch[i].target);
        accumulate_gradients(batch_grad, g);
        free_gradients(g);
    }
    /* Average gradients */
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++)
            batch_grad->d_embedding[i][j] /= batch_size;
    for (int i = 0; i < 2 * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++)
            batch_grad->d_W1[i][j] /= batch_size;
    for (int j = 0; j < net.hidden1; j++)
        batch_grad->d_b1[j] /= batch_size;
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++)
            batch_grad->d_W2[i][j] /= batch_size;
    for (int j = 0; j < net.hidden2; j++)
        batch_grad->d_b2[j] /= batch_size;
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++)
            batch_grad->d_W3[i][j] /= batch_size;
    for (int j = 0; j < net.vocab_size; j++)
        batch_grad->d_b3[j] /= batch_size;
    update_parameters(batch_grad);
    free_gradients(batch_grad);
}

/*-----------------------*/
/* Prediction (Inference)*/
/*-----------------------*/
int sample_prediction(int context[2]) {
    ForwardCache cache = forward_prop(context);
    double r = (double)rand() / RAND_MAX;
    double cumulative = 0.0;
    int pred = 0;
    for (int i = 0; i < net.vocab_size; i++) {
        cumulative += cache.y[i];
        if (r < cumulative) {
            pred = i;
            break;
        }
    }
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
    for (int i = 0; i < vocab.size; i++) {
        fprintf(fp, "%s\n", vocab.words[i]);
    }
    fprintf(fp, "%d %d %d %d\n", net.vocab_size, net.emb_dim, net.hidden1, net.hidden2);
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++)
            fprintf(fp, "%lf ", net.embedding[i][j]);
        fprintf(fp, "\n");
    }
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++)
            fprintf(fp, "%lf ", net.W1[i][j]);
        fprintf(fp, "\n");
    }
    for (int j = 0; j < net.hidden1; j++)
        fprintf(fp, "%lf ", net.b1[j]);
    fprintf(fp, "\n");
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++)
            fprintf(fp, "%lf ", net.W2[i][j]);
        fprintf(fp, "\n");
    }
    for (int j = 0; j < net.hidden2; j++)
        fprintf(fp, "%lf ", net.b2[j]);
    fprintf(fp, "\n");
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < net.vocab_size; j++)
            fprintf(fp, "%lf ", net.W3[i][j]);
        fprintf(fp, "\n");
    }
    for (int j = 0; j < net.vocab_size; j++)
        fprintf(fp, "%lf ", net.b3[j]);
    fprintf(fp, "\n");
    fclose(fp);
}

void load_model(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0 && st.st_size == 0)
        return;
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return;
    int vocab_size;
    if (fscanf(fp, "%d", &vocab_size) != 1) {
        fclose(fp);
        return;
    }
    init_vocab();
    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    for (int i = 0; i < vocab_size; i++) {
        if (!fgets(buffer, sizeof(buffer), fp))
            break;
        buffer[strcspn(buffer, "\n")] = '\0';
        vocab.words[i] = strdup(buffer);
    }
    vocab.size = vocab_size;
    if (fscanf(fp, "%d %d %d %d", &net.vocab_size, &net.emb_dim, &net.hidden1, &net.hidden2) != 4) {
        fclose(fp);
        return;
    }
    init_network();
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++)
            fscanf(fp, "%lf", &net.embedding[i][j]);
    }
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++)
            fscanf(fp, "%lf", &net.W1[i][j]);
    }
    for (int j = 0; j < net.hidden1; j++)
        fscanf(fp, "%lf", &net.b1[j]);
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++)
            fscanf(fp, "%lf", &net.W2[i][j]);
    }
    for (int j = 0; j < net.hidden2; j++)
        fscanf(fp, "%lf", &net.b2[j]);
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < net.vocab_size; j++)
            fscanf(fp, "%lf", &net.W3[i][j]);
    }
    for (int j = 0; j < net.vocab_size; j++)
        fscanf(fp, "%lf", &net.b3[j]);
    fclose(fp);
}

/*-----------------------*/
/* Training Data Builder */
/*-----------------------*/
void process_training_line(char *input, int allow_new_words) {
    char buffer[MAX_INPUT_SIZE*2];
    snprintf(buffer, sizeof(buffer), "%s %s %s", START_TOKEN, input, END_TOKEN);
    char *words[MAX_TOKENS];
    int count = tokenize(buffer, words, MAX_TOKENS);
    if (count < 3)
        return;
    int indices[MAX_TOKENS];
    for (int i = 0; i < count; i++) {
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
    for (int i = 0; i < count - 2; i++) {
        if (train_example_count >= MAX_TRAIN_EXAMPLES)
            break;
        train_examples[train_example_count].context[0] = indices[i];
        train_examples[train_example_count].context[1] = indices[i+1];
        train_examples[train_example_count].target = indices[i+2];
        train_example_count++;
    }
}

void shuffle_training_examples() {
    for (int i = train_example_count - 1; i > 0; i--) {
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
    if (fileExists && !fileEmpty) {
        load_model(model_filename);
    }
    if (vocab.size == 0) {
        init_vocab();
        add_word(START_TOKEN);
        add_word(END_TOKEN);
    }
    /* For manual mode, we use the current network; for automatic mode we will rebuild below */
    if (net.vocab_size == 0)
        init_network();
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
                train_on_example(train_examples[last].context, train_examples[last].target);
            }
            printf("Processed and trained on the input line.\n");
        }
    } else {
        /* Automatic mode: build vocabulary/training examples from file first */
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
        printf("Processing teaching material from %s...\n", materialFilename);
        while (fgets(input, sizeof(input), materialFile)) {
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strlen(input) > 0)
                process_training_line(input, 1);
        }
        fclose(materialFile);
        printf("Built vocabulary of size %d with %d training examples.\n", vocab.size, train_example_count);
        /* Since new words were added, reinitialize network and Adam */
        free_network();
        free_adam();
        init_network();
        init_adam();

        char epochs_str[32];
        int num_epochs = 0;
        printf("Enter the number of epochs for training: ");
        if (!fgets(epochs_str, sizeof(epochs_str), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        num_epochs = atoi(epochs_str);
        if (num_epochs <= 0) {
            printf("Invalid input. Using default %d epochs.\n", DEFAULT_EPOCHS);
            num_epochs = DEFAULT_EPOCHS;
        }
        char batch_str[32];
        int batch_size = DEFAULT_BATCH_SIZE;
        printf("Enter mini-batch size (default %d): ", DEFAULT_BATCH_SIZE);
        if (fgets(batch_str, sizeof(batch_str), stdin)) {
            int bs = atoi(batch_str);
            if (bs > 0) batch_size = bs;
        }
        /* Training loop with mini-batches and learning rate decay */
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
    srand((unsigned int)time(NULL));
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
        char *words[MAX_TOKENS];
        int count = tokenize(input_copy, words, MAX_TOKENS);
        if (count < 2) {
            printf("Not enough context. Please enter at least two words.\n");
            continue;
        }
        int context[2];
        context[0] = find_in_vocab(words[count-2]);
        context[1] = find_in_vocab(words[count-1]);
        if (context[0] < 0 || context[1] < 0) {
            printf("Unknown words in context. Please teach them first.\n");
            continue;
        }
        response[0] = '\0';
        if (is_question(input)) {
            const char *question_prefixes[] = {"I think", "Well", "Perhaps", "In my opinion"};
            int num_prefixes = sizeof(question_prefixes) / sizeof(question_prefixes[0]);
            int idx = rand() % num_prefixes;
            strncat(response, question_prefixes[idx], sizeof(response)-strlen(response)-1);
        }
        int pred = sample_prediction(context);
        if (pred < 0 || pred >= vocab.size ||
            strcmp(vocab.words[pred], START_TOKEN) == 0 ||
            strcmp(vocab.words[pred], END_TOKEN) == 0) {
            printf("No valid prediction.\n");
            continue;
        }
        strncat(response, " ", sizeof(response)-strlen(response)-1);
        strncat(response, vocab.words[pred], sizeof(response)-strlen(response)-1);
        int current_context[2];
        current_context[0] = context[1];
        current_context[1] = pred;
        for (int i = 1; i < MAX_PREDICT_WORDS; i++) {
            int next_pred = sample_prediction(current_context);
            if (next_pred < 0 || next_pred >= vocab.size ||
                strcmp(vocab.words[next_pred], START_TOKEN) == 0 ||
                strcmp(vocab.words[next_pred], END_TOKEN) == 0)
                break;
            strncat(response, " ", sizeof(response)-strlen(response)-1);
            strncat(response, vocab.words[next_pred], sizeof(response)-strlen(response)-1);
            current_context[0] = current_context[1];
            current_context[1] = next_pred;
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
