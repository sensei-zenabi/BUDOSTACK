#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_BIGRAM_CAPACITY 10000
#define INITIAL_TRIGRAM_CAPACITY 10000
#define WORD_LEN 50

/*------------------------------------------
  Data Structures for Bigrams and Trigrams
------------------------------------------*/

typedef struct {
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    int count;
} Bigram;

typedef struct {
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    char word3[WORD_LEN];
    int count;
} Trigram;

/*------------------------------------------
  Global Variables and Dynamic Memory
------------------------------------------*/

Bigram *bigrams = NULL;
int bigram_size = 0;
int bigram_capacity = 0;

Trigram *trigrams = NULL;
int trigram_size = 0;
int trigram_capacity = 0;

/*------------------------------------------
  Model Initialization and Memory Management
------------------------------------------*/

void init_bigrams() {
    if (bigrams == NULL) {
        bigrams = malloc(INITIAL_BIGRAM_CAPACITY * sizeof(Bigram));
        if (bigrams == NULL) {
            fprintf(stderr, "Memory allocation failed for bigrams\n");
            exit(1);
        }
        bigram_capacity = INITIAL_BIGRAM_CAPACITY;
    }
}

void init_trigrams() {
    if (trigrams == NULL) {
        trigrams = malloc(INITIAL_TRIGRAM_CAPACITY * sizeof(Trigram));
        if (trigrams == NULL) {
            fprintf(stderr, "Memory allocation failed for trigrams\n");
            exit(1);
        }
        trigram_capacity = INITIAL_TRIGRAM_CAPACITY;
    }
}

void ensure_bigram_capacity() {
    if (bigram_size >= bigram_capacity) {
        int new_capacity = bigram_capacity * 2;
        Bigram *temp = realloc(bigrams, new_capacity * sizeof(Bigram));
        if (temp == NULL) {
            fprintf(stderr, "Memory allocation failed during bigram expansion\n");
            exit(1);
        }
        bigrams = temp;
        bigram_capacity = new_capacity;
    }
}

void ensure_trigram_capacity() {
    if (trigram_size >= trigram_capacity) {
        int new_capacity = trigram_capacity * 2;
        Trigram *temp = realloc(trigrams, new_capacity * sizeof(Trigram));
        if (temp == NULL) {
            fprintf(stderr, "Memory allocation failed during trigram expansion\n");
            exit(1);
        }
        trigrams = temp;
        trigram_capacity = new_capacity;
    }
}

void free_bigrams() {
    if (bigrams) {
        free(bigrams);
        bigrams = NULL;
    }
}

void free_trigrams() {
    if (trigrams) {
        free(trigrams);
        trigrams = NULL;
    }
}

/*------------------------------------------
  Utility Functions for String Processing
------------------------------------------*/

// Trim leading and trailing whitespace.
void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';
}

// Normalize a word: convert to lowercase and remove leading/trailing punctuation.
void normalize_word(char *word) {
    // Convert to lowercase.
    for (int i = 0; word[i]; i++) {
        word[i] = tolower((unsigned char)word[i]);
    }
    // Remove leading non-alphanumeric characters.
    int start = 0;
    while (word[start] && !isalnum((unsigned char)word[start])) {
        start++;
    }
    if (start > 0) {
        int i = 0;
        while (word[start + i]) {
            word[i] = word[start + i];
            i++;
        }
        word[i] = '\0';
    }
    // Remove trailing non-alphanumeric characters.
    int len = strlen(word);
    while (len > 0 && !isalnum((unsigned char)word[len - 1])) {
        word[len - 1] = '\0';
        len--;
    }
}

/*------------------------------------------
  Updating the Models with New Input
------------------------------------------*/

// Update bigram model with a pair of words.
void update_bigram(const char *word1, const char *word2) {
    for (int i = 0; i < bigram_size; i++) {
        if (strcmp(bigrams[i].word1, word1) == 0 && strcmp(bigrams[i].word2, word2) == 0) {
            bigrams[i].count++;
            return;
        }
    }
    ensure_bigram_capacity();
    strncpy(bigrams[bigram_size].word1, word1, WORD_LEN - 1);
    bigrams[bigram_size].word1[WORD_LEN - 1] = '\0';
    strncpy(bigrams[bigram_size].word2, word2, WORD_LEN - 1);
    bigrams[bigram_size].word2[WORD_LEN - 1] = '\0';
    bigrams[bigram_size].count = 1;
    bigram_size++;
}

// Update trigram model with a triple of words.
void update_trigram(const char *word1, const char *word2, const char *word3) {
    for (int i = 0; i < trigram_size; i++) {
        if (strcmp(trigrams[i].word1, word1) == 0 &&
            strcmp(trigrams[i].word2, word2) == 0 &&
            strcmp(trigrams[i].word3, word3) == 0) {
            trigrams[i].count++;
            return;
        }
    }
    ensure_trigram_capacity();
    strncpy(trigrams[trigram_size].word1, word1, WORD_LEN - 1);
    trigrams[trigram_size].word1[WORD_LEN - 1] = '\0';
    strncpy(trigrams[trigram_size].word2, word2, WORD_LEN - 1);
    trigrams[trigram_size].word2[WORD_LEN - 1] = '\0';
    strncpy(trigrams[trigram_size].word3, word3, WORD_LEN - 1);
    trigrams[trigram_size].word3[WORD_LEN - 1] = '\0';
    trigrams[trigram_size].count = 1;
    trigram_size++;
}

// Process a line of input by tokenizing and updating both bigram and trigram models.
void process_input(char *input) {
    char *words[1000];
    int count = 0;

    char *token = strtok(input, " ");
    while (token && count < 1000) {
        normalize_word(token);
        if (strlen(token) > 0) {  // Skip empty tokens.
            words[count++] = token;
        }
        token = strtok(NULL, " ");
    }

    // Update bigrams.
    for (int i = 0; i < count - 1; i++) {
        update_bigram(words[i], words[i + 1]);
    }
    // Update trigrams.
    for (int i = 0; i < count - 2; i++) {
        update_trigram(words[i], words[i + 1], words[i + 2]);
    }
}

/*------------------------------------------
  Model Persistence: Saving and Loading
------------------------------------------*/

// Save both models to a file using headers to differentiate.
void save_models(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s for writing\n", filename);
        return;
    }
    // Save bigrams.
    fprintf(file, "BIGRAMS %d\n", bigram_size);
    for (int i = 0; i < bigram_size; i++) {
        fprintf(file, "%s %s %d\n", bigrams[i].word1, bigrams[i].word2, bigrams[i].count);
    }
    // Save trigrams.
    fprintf(file, "TRIGRAMS %d\n", trigram_size);
    for (int i = 0; i < trigram_size; i++) {
        fprintf(file, "%s %s %s %d\n", trigrams[i].word1, trigrams[i].word2, trigrams[i].word3, trigrams[i].count);
    }
    fclose(file);
}

// Load models from a file. The file format expects headers "BIGRAMS" and "TRIGRAMS".
void load_models(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return;

    char header[20];
    int count;

    // Load bigrams.
    if (fscanf(file, "%s %d", header, &count) == 2) {
        if (strcmp(header, "BIGRAMS") == 0) {
            for (int i = 0; i < count; i++) {
                char w1[WORD_LEN], w2[WORD_LEN];
                int c;
                if (fscanf(file, "%49s %49s %d", w1, w2, &c) == 3) {
                    normalize_word(w1);
                    normalize_word(w2);
                    ensure_bigram_capacity();
                    strncpy(bigrams[bigram_size].word1, w1, WORD_LEN - 1);
                    bigrams[bigram_size].word1[WORD_LEN - 1] = '\0';
                    strncpy(bigrams[bigram_size].word2, w2, WORD_LEN - 1);
                    bigrams[bigram_size].word2[WORD_LEN - 1] = '\0';
                    bigrams[bigram_size].count = c;
                    bigram_size++;
                }
            }
        }
    }
    // Load trigrams.
    if (fscanf(file, "%s %d", header, &count) == 2) {
        if (strcmp(header, "TRIGRAMS") == 0) {
            for (int i = 0; i < count; i++) {
                char w1[WORD_LEN], w2[WORD_LEN], w3[WORD_LEN];
                int c;
                if (fscanf(file, "%49s %49s %49s %d", w1, w2, w3, &c) == 4) {
                    normalize_word(w1);
                    normalize_word(w2);
                    normalize_word(w3);
                    ensure_trigram_capacity();
                    strncpy(trigrams[trigram_size].word1, w1, WORD_LEN - 1);
                    trigrams[trigram_size].word1[WORD_LEN - 1] = '\0';
                    strncpy(trigrams[trigram_size].word2, w2, WORD_LEN - 1);
                    trigrams[trigram_size].word2[WORD_LEN - 1] = '\0';
                    strncpy(trigrams[trigram_size].word3, w3, WORD_LEN - 1);
                    trigrams[trigram_size].word3[WORD_LEN - 1] = '\0';
                    trigrams[trigram_size].count = c;
                    trigram_size++;
                }
            }
        }
    }
    fclose(file);
}

/*------------------------------------------
  Prediction Functions
------------------------------------------*/

// Try to predict the next word using a trigram model (given the previous two words).
const char* predict_trigram(const char *prev_word, const char *last_word) {
    int max_count = 0;
    const char *best = NULL;
    for (int i = 0; i < trigram_size; i++) {
        if (strcmp(trigrams[i].word1, prev_word) == 0 &&
            strcmp(trigrams[i].word2, last_word) == 0) {
            if (trigrams[i].count > max_count) {
                max_count = trigrams[i].count;
                best = trigrams[i].word3;
            }
        }
    }
    return best;
}

// Fall back to a bigram model (given a single word).
const char* predict_bigram(const char *word) {
    int max_count = 0;
    const char *best = NULL;
    for (int i = 0; i < bigram_size; i++) {
        if (strcmp(bigrams[i].word1, word) == 0) {
            if (bigrams[i].count > max_count) {
                max_count = bigrams[i].count;
                best = bigrams[i].word2;
            }
        }
    }
    return best;
}

/*------------------------------------------
  API Functions for Teaching and Running
------------------------------------------*/

// Teach mode: loads models, accepts input to update the models, and saves upon exit.
void cmd_teach_sv(char *filename) {
    char input[1000];

    init_bigrams();
    init_trigrams();
    load_models(filename);

    while (1) {
        printf("teach> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            save_models(filename);
            break;
        }
        process_input(input);
    }
    // Optionally free memory if no further processing is needed.
    // free_bigrams();
    // free_trigrams();
}

// Run mode: loads models and uses the most recent context (trigram if available, else bigram) to predict additional words.
void cmd_run_sv(char *filename) {
    char input[1000];
    char generated_sentence[1000] = {0};

    init_bigrams();
    init_trigrams();
    load_models(filename);

    while (1) {
        printf("run> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0)
            break;

        strcpy(generated_sentence, input);

        // Tokenize input to determine the context.
        char input_copy[1000];
        strcpy(input_copy, input);
        char *words[1000];
        int count = 0;
        char *token = strtok(input_copy, " ");
        while (token && count < 1000) {
            normalize_word(token);
            if (strlen(token) > 0) {
                words[count++] = token;
            }
            token = strtok(NULL, " ");
        }

        if (count == 0) {
            printf("No valid input detected.\n");
            continue;
        }

        const char *next_word = NULL;
        // If at least 2 words are present, try trigram prediction.
        if (count >= 2) {
            next_word = predict_trigram(words[count - 2], words[count - 1]);
        }
        // If no trigram prediction is found, fall back to bigram using the last word.
        if (!next_word) {
            next_word = predict_bigram(words[count - 1]);
        }
        // If still no prediction, output the current sentence.
        if (!next_word) {
            printf("Prediction: %s\n", generated_sentence);
            continue;
        }

        // Prepare context for iterative prediction.
        char current_prev[WORD_LEN];
        char current_last[WORD_LEN];
        if (count >= 2) {
            strncpy(current_prev, words[count - 2], WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, words[count - 1], WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        } else {
            strcpy(current_prev, words[count - 1]);
            strcpy(current_last, next_word);
        }

        // Append the first predicted word.
        strcat(generated_sentence, " ");
        strcat(generated_sentence, next_word);

        // Generate up to 10 additional words.
        for (int i = 1; i < 10; i++) {
            next_word = predict_trigram(current_prev, current_last);
            if (!next_word) {
                next_word = predict_bigram(current_last);
            }
            if (!next_word)
                break;
            strcat(generated_sentence, " ");
            strcat(generated_sentence, next_word);
            // Update context: shift window by one.
            strncpy(current_prev, current_last, WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, next_word, WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        }
        printf("Prediction: %s\n", generated_sentence);
    }
    // Optionally free memory if no further processing is needed.
    // free_bigrams();
    // free_trigrams();
}
