#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

/*-----------------------*/
/* Configuration Macros  */
/*-----------------------*/
#define MAX_WORD_LEN       50
#define MAX_INPUT_SIZE     1000
#define MAX_TOKENS         1000
#define MAX_VOCAB_SIZE     10000
#define MAX_TRAIN_EXAMPLES 100000

/* Neural Network hyperparameters */
#define EMBEDDING_DIM   10      /* Dimension of word embeddings */
#define HIDDEN_SIZE1    32      /* Size of first hidden layer */
#define HIDDEN_SIZE2    32      /* Size of second hidden layer */
#define LEARNING_RATE   0.01
#define EPOCHS          5       /* Default number of training passes in automatic teaching mode */
#define MAX_PREDICT_WORDS 10     /* Maximum number of words to generate */

/* Special tokens */
#define START_TOKEN "<s>"
#define END_TOKEN   "</s>"

/*-----------------------*/
/* Data Structures       */
/*-----------------------*/

/* Vocabulary: a simple dynamic list of words. */
typedef struct {
    char *words[MAX_VOCAB_SIZE];
    int size;
} Vocabulary;

/* A training example consists of a context (two words) and a target word (all stored as indices) */
typedef struct {
    int context[2];
    int target;
} TrainingExample;

/* Neural network parameters and weight matrices */
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

/*-----------------------*/
/* Global Variables      */
/*-----------------------*/
Vocabulary vocab;
TrainingExample train_examples[MAX_TRAIN_EXAMPLES];
int train_example_count = 0;

NeuralNetwork net = {0};  /* Global network: pointer members are initialized to NULL */

/*-----------------------*/
/* Utility Functions     */
/*-----------------------*/

/* djb2 hash function (not used in the neural version but kept for consistency) */
unsigned long hash_djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/* Remove leading and trailing whitespace from str in place */
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

/* Normalize a word: convert to lowercase and remove punctuation (except for special tokens) */
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

/* Tokenize the input string into words.
   Returns the number of tokens; tokens are stored as pointers within input. */
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

/* Add word to vocabulary if not present, return its index */
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

/* Free vocabulary memory */
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
    if (m == NULL)
        return;
    for (int i = 0; i < rows; i++)
        free(m[i]);
    free(m);
}

/* Random initialization in range [-r, r] */
double rand_uniform(double r) {
    return ((double)rand() / RAND_MAX) * 2 * r - r;
}

/*-----------------------*/
/* Neural Network Setup  */
/*-----------------------*/
void init_network() {
    /* Update network dimensions based on the current vocabulary size */
    net.vocab_size = vocab.size;
    net.emb_dim = EMBEDDING_DIM;
    net.hidden1 = HIDDEN_SIZE1;
    net.hidden2 = HIDDEN_SIZE2;

    /* Allocate embedding: vocab_size x emb_dim */
    net.embedding = alloc_matrix(net.vocab_size, net.emb_dim);
    double r_emb = 0.5;
    for (int i = 0; i < net.vocab_size; i++)
        for (int j = 0; j < net.emb_dim; j++)
            net.embedding[i][j] = rand_uniform(r_emb);

    /* Layer 1: input dim = 2*emb_dim, output = hidden1 */
    net.W1 = alloc_matrix(2 * net.emb_dim, net.hidden1);
    net.b1 = alloc_vector(net.hidden1);
    double r_W1 = 0.5;
    for (int i = 0; i < 2 * net.emb_dim; i++)
        for (int j = 0; j < net.hidden1; j++)
            net.W1[i][j] = rand_uniform(r_W1);
    for (int j = 0; j < net.hidden1; j++)
        net.b1[j] = 0.0;

    /* Layer 2: hidden1 -> hidden2 */
    net.W2 = alloc_matrix(net.hidden1, net.hidden2);
    net.b2 = alloc_vector(net.hidden2);
    double r_W2 = 0.5;
    for (int i = 0; i < net.hidden1; i++)
        for (int j = 0; j < net.hidden2; j++)
            net.W2[i][j] = rand_uniform(r_W2);
    for (int j = 0; j < net.hidden2; j++)
        net.b2[j] = 0.0;

    /* Output layer: hidden2 -> vocab_size */
    net.W3 = alloc_matrix(net.hidden2, net.vocab_size);
    net.b3 = alloc_vector(net.vocab_size);
    double r_W3 = 0.5;
    for (int i = 0; i < net.hidden2; i++)
        for (int j = 0; j < net.vocab_size; j++)
            net.W3[i][j] = rand_uniform(r_W3);
    for (int j = 0; j < net.vocab_size; j++)
        net.b3[j] = 0.0;
}

/* Free network parameters with pointer guards */
void free_network() {
    if (net.embedding) {
        free_matrix(net.embedding, net.vocab_size);
        net.embedding = NULL;
    }
    if (net.W1) {
        free_matrix(net.W1, 2 * net.emb_dim);
        net.W1 = NULL;
    }
    if (net.b1) {
        free(net.b1);
        net.b1 = NULL;
    }
    if (net.W2) {
        free_matrix(net.W2, net.hidden1);
        net.W2 = NULL;
    }
    if (net.b2) {
        free(net.b2);
        net.b2 = NULL;
    }
    if (net.W3) {
        free_matrix(net.W3, net.hidden2);
        net.W3 = NULL;
    }
    if (net.b3) {
        free(net.b3);
        net.b3 = NULL;
    }
}

/*-----------------------*/
/* Forward Propagation   */
/*-----------------------*/

/* Given a context of two word indices, perform forward propagation.
   Outputs (via allocated arrays) intermediate activations needed for backprop.
   The returned output (probabilities) is an array of length vocab_size. 
   Caller is responsible for freeing allocated arrays.
   Note: All vectors are allocated dynamically.
*/
typedef struct {
    double *x;    /* concatenated embedding vector, length = 2*emb_dim */
    double *z1;   /* pre-activation of first hidden layer, length = hidden1 */
    double *a1;   /* activation of first hidden layer (ReLU), length = hidden1 */
    double *z2;   /* pre-activation of second hidden layer, length = hidden2 */
    double *a2;   /* activation of second hidden layer (ReLU), length = hidden2 */
    double *z3;   /* pre-activation of output layer, length = vocab_size */
    double *y;    /* softmax output, length = vocab_size */
} ForwardCache;

/* ReLU activation and its derivative */
double relu(double x) {
    return x > 0 ? x : 0;
}

double relu_deriv(double x) {
    return x > 0 ? 1.0 : 0.0;
}

/* Softmax function (in-place for a vector of length n) */
void softmax(double *z, int n) {
    double max = z[0];
    for (int i = 1; i < n; i++) {
        if (z[i] > max)
            max = z[i];
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        z[i] = exp(z[i] - max);
        sum += z[i];
    }
    for (int i = 0; i < n; i++)
        z[i] /= sum;
}

/* Forward propagation function */
ForwardCache forward_prop(int context[2]) {
    ForwardCache cache;
    int input_dim = 2 * net.emb_dim;
    cache.x = alloc_vector(input_dim);
    /* Lookup embeddings for the two context words and concatenate */
    for (int i = 0; i < net.emb_dim; i++) {
        cache.x[i] = net.embedding[context[0]][i];
        cache.x[i + net.emb_dim] = net.embedding[context[1]][i];
    }
    /* Layer 1: z1 = x * W1 + b1 */
    cache.z1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++) {
        cache.z1[j] = net.b1[j];
        for (int i = 0; i < input_dim; i++)
            cache.z1[j] += cache.x[i] * net.W1[i][j];
    }
    /* a1 = ReLU(z1) */
    cache.a1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++)
        cache.a1[j] = relu(cache.z1[j]);
    
    /* Layer 2: z2 = a1 * W2 + b2 */
    cache.z2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++) {
        cache.z2[j] = net.b2[j];
        for (int i = 0; i < net.hidden1; i++)
            cache.z2[j] += cache.a1[i] * net.W2[i][j];
    }
    /* a2 = ReLU(z2) */
    cache.a2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++)
        cache.a2[j] = relu(cache.z2[j]);
    
    /* Output layer: z3 = a2 * W3 + b3 */
    cache.z3 = alloc_vector(net.vocab_size);
    for (int j = 0; j < net.vocab_size; j++) {
        cache.z3[j] = net.b3[j];
        for (int i = 0; i < net.hidden2; i++)
            cache.z3[j] += cache.a2[i] * net.W3[i][j];
    }
    /* Softmax output */
    cache.y = alloc_vector(net.vocab_size);
    memcpy(cache.y, cache.z3, sizeof(double) * net.vocab_size);
    softmax(cache.y, net.vocab_size);
    
    return cache;
}

/* Free the cache allocated in forward_prop */
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
/* Backpropagation       */
/*-----------------------*/
/* Given a training example (context, target) and the forward cache,
   perform backpropagation and update the network parameters using SGD.
*/
void backpropagate(int context[2], int target, ForwardCache *cache) {
    int input_dim = 2 * net.emb_dim;
    int vocab_size = net.vocab_size;
    /* Create one-hot vector for target */
    double *t = alloc_vector(vocab_size);
    for (int i = 0; i < vocab_size; i++)
        t[i] = 0.0;
    t[target] = 1.0;

    /* Compute gradient at output layer: dL/dz3 = y - t */
    double *dz3 = alloc_vector(vocab_size);
    for (int j = 0; j < vocab_size; j++)
        dz3[j] = cache->y[j] - t[j];

    /* Gradients for W3 and b3 */
    double **dW3 = alloc_matrix(net.hidden2, vocab_size);
    double *db3 = alloc_vector(vocab_size);
    for (int j = 0; j < vocab_size; j++) {
        db3[j] = dz3[j];
    }
    for (int i = 0; i < net.hidden2; i++) {
        for (int j = 0; j < vocab_size; j++) {
            dW3[i][j] = cache->a2[i] * dz3[j];
        }
    }

    /* Backprop into layer 2: dL/da2 = dz3 * W3^T */
    double *da2 = alloc_vector(net.hidden2);
    for (int i = 0; i < net.hidden2; i++) {
        da2[i] = 0.0;
        for (int j = 0; j < vocab_size; j++)
            da2[i] += dz3[j] * net.W3[i][j];
    }
    /* dL/dz2 = da2 * ReLU'(z2) */
    double *dz2 = alloc_vector(net.hidden2);
    for (int i = 0; i < net.hidden2; i++)
        dz2[i] = da2[i] * relu_deriv(cache->z2[i]);

    /* Gradients for W2 and b2 */
    double **dW2 = alloc_matrix(net.hidden1, net.hidden2);
    double *db2 = alloc_vector(net.hidden2);
    for (int j = 0; j < net.hidden2; j++) {
        db2[j] = dz2[j];
    }
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++) {
            dW2[i][j] = cache->a1[i] * dz2[j];
        }
    }

    /* Backprop into layer 1: dL/da1 = dz2 * W2^T */
    double *da1 = alloc_vector(net.hidden1);
    for (int i = 0; i < net.hidden1; i++) {
        da1[i] = 0.0;
        for (int j = 0; j < net.hidden2; j++)
            da1[i] += dz2[j] * net.W2[i][j];
    }
    /* dL/dz1 = da1 * ReLU'(z1) */
    double *dz1 = alloc_vector(net.hidden1);
    for (int i = 0; i < net.hidden1; i++)
        dz1[i] = da1[i] * relu_deriv(cache->z1[i]);

    /* Gradients for W1 and b1 */
    double **dW1 = alloc_matrix(input_dim, net.hidden1);
    double *db1 = alloc_vector(net.hidden1);
    for (int j = 0; j < net.hidden1; j++) {
        db1[j] = dz1[j];
    }
    for (int i = 0; i < input_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            dW1[i][j] = cache->x[i] * dz1[j];
        }
    }

    /* Gradients for embeddings.
       The input x is a concatenation of embedding[context[0]] and embedding[context[1]].
    */
    double **dEmb = alloc_matrix(2, net.emb_dim);
    for (int i = 0; i < net.emb_dim; i++) {
        dEmb[0][i] = 0.0;
        dEmb[1][i] = 0.0;
    }
    /* For first word: gradient is the dot product of dz1 with the corresponding part of W1 */
    for (int i = 0; i < net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            dEmb[0][i] += net.W1[i][j] * dz1[j];
        }
    }
    /* For second word: gradient comes from second half of x */
    for (int i = 0; i < net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++) {
            dEmb[1][i] += net.W1[i + net.emb_dim][j] * dz1[j];
        }
    }

    /* SGD update for all parameters */
    int i, j;
    /* Update output layer */
    for (i = 0; i < net.hidden2; i++)
        for (j = 0; j < vocab_size; j++)
            net.W3[i][j] -= LEARNING_RATE * dW3[i][j];
    for (j = 0; j < vocab_size; j++)
        net.b3[j] -= LEARNING_RATE * db3[j];

    /* Update layer 2 */
    for (i = 0; i < net.hidden1; i++)
        for (j = 0; j < net.hidden2; j++)
            net.W2[i][j] -= LEARNING_RATE * dW2[i][j];
    for (j = 0; j < net.hidden2; j++)
        net.b2[j] -= LEARNING_RATE * db2[j];

    /* Update layer 1 */
    for (i = 0; i < input_dim; i++)
        for (j = 0; j < net.hidden1; j++)
            net.W1[i][j] -= LEARNING_RATE * dW1[i][j];
    for (j = 0; j < net.hidden1; j++)
        net.b1[j] -= LEARNING_RATE * db1[j];

    /* Update embeddings */
    /* For context[0] */
    for (i = 0; i < net.emb_dim; i++)
        net.embedding[context[0]][i] -= LEARNING_RATE * dEmb[0][i];
    /* For context[1] */
    for (i = 0; i < net.emb_dim; i++)
        net.embedding[context[1]][i] -= LEARNING_RATE * dEmb[1][i];

    /* Free all allocated gradients */
    free(t);
    free(dz3);
    free_matrix(dW3, net.hidden2);
    free(db3);
    free(da2);
    free(dz2);
    free_matrix(dW2, net.hidden1);
    free(db2);
    free(da1);
    free(dz1);
    free_matrix(dW1, input_dim);
    free(db1);
    free_matrix(dEmb, 2);
}

/* Train on one example: perform forward propagation, backpropagation, and update parameters */
void train_on_example(int context[2], int target) {
    ForwardCache cache = forward_prop(context);
    backpropagate(context, target, &cache);
    free_forward_cache(&cache);
}

/*-----------------------*/
/* Prediction (Inference)*/
/*-----------------------*/
/* Given a context (two word indices), run forward propagation and sample the output.
   Returns the predicted word index. */
int sample_prediction(int context[2]) {
    ForwardCache cache = forward_prop(context);
    /* Sample from the output probability distribution */
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
/* Save the vocabulary and network parameters to a text file.
   Format:
      vocab_size
      word0
      word1
      ...
      (then network parameters, dimensions followed by each matrix row)
*/
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
    /* Save dimensions */
    fprintf(fp, "%d %d %d %d\n", net.vocab_size, net.emb_dim, net.hidden1, net.hidden2);
    /* Save embedding matrix */
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++)
            fprintf(fp, "%lf ", net.embedding[i][j]);
        fprintf(fp, "\n");
    }
    /* Save W1 and b1 */
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++)
            fprintf(fp, "%lf ", net.W1[i][j]);
        fprintf(fp, "\n");
    }
    for (int j = 0; j < net.hidden1; j++)
        fprintf(fp, "%lf ", net.b1[j]);
    fprintf(fp, "\n");
    /* Save W2 and b2 */
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++)
            fprintf(fp, "%lf ", net.W2[i][j]);
        fprintf(fp, "\n");
    }
    for (int j = 0; j < net.hidden2; j++)
        fprintf(fp, "%lf ", net.b2[j]);
    fprintf(fp, "\n");
    /* Save W3 and b3 */
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

/* Load the model from a file (expects same format as save_model).
   If the model file is empty or invalid, this function leaves the global
   vocabulary and network uninitialized so that teaching mode can start fresh.
*/
void load_model(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* If model does not exist, we start fresh */
        return;
    }
    int vocab_size;
    if (fscanf(fp, "%d", &vocab_size) != 1) {
        /* Empty or invalid file: close and return without modifying globals */
        fclose(fp);
        return;
    }
    init_vocab();
    char buffer[256];
    /* Read the newline after the vocabulary size */
    fgets(buffer, sizeof(buffer), fp);
    for (int i = 0; i < vocab_size; i++) {
        if (!fgets(buffer, sizeof(buffer), fp))
            break;
        buffer[strcspn(buffer, "\n")] = '\0';
        vocab.words[i] = strdup(buffer);
    }
    vocab.size = vocab_size;
    if (fscanf(fp, "%d %d %d %d", &net.vocab_size, &net.emb_dim, &net.hidden1, &net.hidden2) != 4) {
        /* File did not contain valid network dimensions */
        fclose(fp);
        return;
    }
    /* Allocate network parameters */
    init_network();
    /* Load embedding */
    for (int i = 0; i < net.vocab_size; i++) {
        for (int j = 0; j < net.emb_dim; j++)
            fscanf(fp, "%lf", &net.embedding[i][j]);
    }
    /* Load W1 */
    for (int i = 0; i < 2 * net.emb_dim; i++) {
        for (int j = 0; j < net.hidden1; j++)
            fscanf(fp, "%lf", &net.W1[i][j]);
    }
    for (int j = 0; j < net.hidden1; j++)
        fscanf(fp, "%lf", &net.b1[j]);
    /* Load W2 */
    for (int i = 0; i < net.hidden1; i++) {
        for (int j = 0; j < net.hidden2; j++)
            fscanf(fp, "%lf", &net.W2[i][j]);
    }
    for (int j = 0; j < net.hidden2; j++)
        fscanf(fp, "%lf", &net.b2[j]);
    /* Load W3 */
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
/* Process a line of text for training: prepend START_TOKEN and append END_TOKEN,
   tokenize the sentence, add words to vocabulary, and record training examples
   for every trigram (context = previous two words, target = current word).
*/
void process_training_line(char *input) {
    char buffer[MAX_INPUT_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "%s %s %s", START_TOKEN, input, END_TOKEN);
    char *words[MAX_TOKENS];
    int count = tokenize(buffer, words, MAX_TOKENS);
    if (count < 3)
        return;
    /* Convert words to indices (adding to vocab as needed) */
    int indices[MAX_TOKENS];
    for (int i = 0; i < count; i++) {
        indices[i] = add_word(words[i]);
    }
    /* Create training examples for each trigram */
    for (int i = 0; i < count - 2; i++) {
        if (train_example_count >= MAX_TRAIN_EXAMPLES)
            break;
        train_examples[train_example_count].context[0] = indices[i];
        train_examples[train_example_count].context[1] = indices[i + 1];
        train_examples[train_example_count].target = indices[i + 2];
        train_example_count++;
    }
}

/*-----------------------*/
/* Helper for Run Mode   */
/*-----------------------*/
/* Capitalize first letter and append a period if needed */
void humanize_response(char *response) {
    if (response[0] != '\0')
        response[0] = toupper((unsigned char)response[0]);
    size_t len = strlen(response);
    if (len > 0 && response[len - 1] != '.' && response[len - 1] != '!' && response[len - 1] != '?')
        strncat(response, ".", MAX_INPUT_SIZE - strlen(response) - 1);
}

/* Check if input ends with a '?' */
int is_question(const char *input) {
    size_t len = strlen(input);
    while (len > 0 && isspace((unsigned char)input[len - 1]))
        len--;
    return (len > 0 && input[len - 1] == '?');
}

/*-----------------------*/
/* API Functions         */
/*-----------------------*/

/* cmd_teach_sv: API function called when entering teach mode in main.c
   Teaching mode. Supports two modes:
   - Manual teaching: interactive input lines.
   - Automatic teaching: user supplies a filename for teaching material.
   In either case, training examples are built from the text and the network is trained.
   
   IMPORTANT: When new words are added to the vocabulary, the network dimensions become outdated.
   In this implementation (for an empty model) we reinitialize the network (losing any previous training)
   so that its dimensions match the updated vocabulary.
*/
void cmd_teach_sv(char *filename) {
    char input[MAX_INPUT_SIZE];
    /* Try to load an existing model */
    load_model(filename);
    if (vocab.size == 0) {
        /* Start a new vocabulary with special tokens if the model is empty */
        init_vocab();
        add_word(START_TOKEN);
        add_word(END_TOKEN);
    }
    if (net.vocab_size == 0) {
        init_network();
    }
    
    printf("Welcome to the NN Teaching Tool.\n");
    printf("Would you like to use manual teaching mode? (Type 'y' for manual mode)\n");
    printf("Your choice: ");
    if (!fgets(input, sizeof(input), stdin)) {
        fprintf(stderr, "Input error.\n");
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    trim_whitespace(input);
    
    if (strcmp(input, "y") == 0 || strcmp(input, "Y") == 0) {
        printf("Manual teaching mode selected.\n");
        printf("Enter sentences to update the model. Type 'exit' to save and quit.\n");
        while (1) {
            printf("teach> ");
            if (!fgets(input, sizeof(input), stdin))
                break;
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strcmp(input, "exit") == 0)
                break;
            process_training_line(input);
            /* Reinitialize network if new words were added */
            if (net.vocab_size != vocab.size) {
                free_network();
                init_network();
            }
            /* Train for one epoch on the newly added examples */
            for (int i = 0; i < train_example_count; i++) {
                train_on_example(train_examples[i].context, train_examples[i].target);
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
        printf("Processing teaching material from %s...\n", materialFilename);
        while (fgets(input, sizeof(input), materialFile)) {
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strlen(input) > 0)
                process_training_line(input);
        }
        fclose(materialFile);
        /* Reinitialize network if new words were added */
        if (net.vocab_size != vocab.size) {
            free_network();
            init_network();
        }
        /* Ask the user how many epochs to run */
        char epochs_str[32];
        int num_epochs = 0;
        printf("Enter the number of epochs for training: ");
        if (!fgets(epochs_str, sizeof(epochs_str), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        num_epochs = atoi(epochs_str);
        if (num_epochs <= 0) {
            printf("Invalid input. Using default %d epochs.\n", EPOCHS);
            num_epochs = EPOCHS;
        }
        /* Train for the specified number of epochs */
        printf("Training on %d examples for %d epochs...\n", train_example_count, num_epochs);
        for (int epoch = 0; epoch < num_epochs; epoch++) {
            for (int i = 0; i < train_example_count; i++) {
                train_on_example(train_examples[i].context, train_examples[i].target);
            }
            printf("Epoch %d completed.\n", epoch+1);
        }
    }
    /* Save the updated model */
    save_model(filename);
    printf("Model saved to %s.\n", filename);
}

/* cmd_run_sv = API function called when entering run mode in main.c
   Run mode. The user enters a sentence as context and the system predicts continuations.
   The prediction uses the last two tokens (or duplicates if needed) and generates up to
   MAX_PREDICT_WORDS words.
*/
void cmd_run_sv(char *filename) {
    char input[MAX_INPUT_SIZE];
    char response[MAX_INPUT_SIZE] = {0};
    /* Load model */
    load_model(filename);
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
        /* Use the last two tokens as context */
        int context[2];
        context[0] = find_in_vocab(words[count - 2]);
        context[1] = find_in_vocab(words[count - 1]);
        if (context[0] < 0 || context[1] < 0) {
            printf("Unknown words in context. Please teach them first.\n");
            continue;
        }
        /* Start generating the response */
        response[0] = '\0';
        if (is_question(input)) {
            const char *question_prefixes[] = {"I think", "Well", "Perhaps", "In my opinion"};
            int num_prefixes = sizeof(question_prefixes) / sizeof(question_prefixes[0]);
            int idx = rand() % num_prefixes;
            strncat(response, question_prefixes[idx], sizeof(response) - strlen(response) - 1);
        }
        /* Generate first predicted word */
        int pred = sample_prediction(context);
        if (pred < 0 || pred >= vocab.size ||
            strcmp(vocab.words[pred], START_TOKEN) == 0 ||
            strcmp(vocab.words[pred], END_TOKEN) == 0) {
            printf("No valid prediction.\n");
            continue;
        }
        strncat(response, " ", sizeof(response) - strlen(response) - 1);
        strncat(response, vocab.words[pred], sizeof(response) - strlen(response) - 1);
        /* Iteratively predict further words */
        int current_context[2];
        current_context[0] = context[1];
        current_context[1] = pred;
        for (int i = 1; i < MAX_PREDICT_WORDS; i++) {
            int next_pred = sample_prediction(current_context);
            if (next_pred < 0 || next_pred >= vocab.size ||
                strcmp(vocab.words[next_pred], START_TOKEN) == 0 ||
                strcmp(vocab.words[next_pred], END_TOKEN) == 0)
                break;
            strncat(response, " ", sizeof(response) - strlen(response) - 1);
            strncat(response, vocab.words[next_pred], sizeof(response) - strlen(response) - 1);
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
    free_vocab();
    return 0;
}
#endif
