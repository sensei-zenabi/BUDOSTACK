#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define WORD_LEN 50
#define MAX_INPUT_SIZE 1000
#define MAX_TOKENS 1000
#define BIGRAM_TABLE_SIZE 10007   /* A prime number for hash table buckets */
#define TRIGRAM_TABLE_SIZE 10007  /* A prime number for hash table buckets */

/*------------------------------------------
   Data Structures for Bigrams and Trigrams
------------------------------------------*/

typedef struct BigramEntry {
    char key[WORD_LEN * 2 + 2];  // Format: "word1#word2"
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    int count;
    struct BigramEntry *next;
} BigramEntry;

typedef struct TrigramEntry {
    char key[WORD_LEN * 3 + 3];  // Format: "word1#word2#word3"
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    char word3[WORD_LEN];
    int count;
    struct TrigramEntry *next;
} TrigramEntry;

/*------------------------------------------
          Global Hash Table Variables
------------------------------------------*/

/* These arrays hold pointers to the first element of each chain */
BigramEntry *bigramTable[BIGRAM_TABLE_SIZE];
TrigramEntry *trigramTable[TRIGRAM_TABLE_SIZE];

/*------------------------------------------
          Hash Function
------------------------------------------*/

/* djb2 hash function */
unsigned long hash_djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    return hash;
}

/*------------------------------------------
          Utility Functions for String Processing
------------------------------------------*/

/* Remove leading and trailing whitespace from str in place */
void trim_whitespace(char *str) {
    char *start = str;
    /* Skip leading whitespace */
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != str)
        memmove(str, start, strlen(start) + 1);
    /* Remove trailing whitespace */
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

/* Normalize a word: convert to lowercase and remove leading/trailing punctuation */
void normalize_word(char *word) {
    for (int i = 0; word[i]; i++) {
        word[i] = tolower((unsigned char)word[i]);
    }
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

/* Tokenize the input string into words (tokens). Returns the number of tokens.
   The tokens (pointers within input) are stored in the provided words array. */
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

/*------------------------------------------
          Initialization of Hash Tables
------------------------------------------*/

/* Set all hash table buckets to NULL */
void init_tables(void) {
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++)
        bigramTable[i] = NULL;
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++)
        trigramTable[i] = NULL;
}

/*------------------------------------------
          Updating the Models
------------------------------------------*/

/* Update bigram model: if the bigram exists, increment count; else, create a new entry */
void update_bigram(const char *word1, const char *word2) {
    char key[WORD_LEN * 2 + 2];
    snprintf(key, sizeof(key), "%s#%s", word1, word2);
    unsigned long idx = hash_djb2(key) % BIGRAM_TABLE_SIZE;
    BigramEntry *entry = bigramTable[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->count++;
            return;
        }
        entry = entry->next;
    }
    /* Not found: create a new entry */
    entry = malloc(sizeof(BigramEntry));
    if (!entry) {
        fprintf(stderr, "Memory allocation failed for bigram\n");
        exit(1);
    }
    strncpy(entry->word1, word1, WORD_LEN - 1);
    entry->word1[WORD_LEN - 1] = '\0';
    strncpy(entry->word2, word2, WORD_LEN - 1);
    entry->word2[WORD_LEN - 1] = '\0';
    entry->count = 1;
    strncpy(entry->key, key, sizeof(entry->key));
    entry->next = bigramTable[idx];
    bigramTable[idx] = entry;
}

/* Update trigram model: if the trigram exists, increment count; else, create a new entry */
void update_trigram(const char *word1, const char *word2, const char *word3) {
    char key[WORD_LEN * 3 + 3];
    snprintf(key, sizeof(key), "%s#%s#%s", word1, word2, word3);
    unsigned long idx = hash_djb2(key) % TRIGRAM_TABLE_SIZE;
    TrigramEntry *entry = trigramTable[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->count++;
            return;
        }
        entry = entry->next;
    }
    /* Not found: create a new entry */
    entry = malloc(sizeof(TrigramEntry));
    if (!entry) {
        fprintf(stderr, "Memory allocation failed for trigram\n");
        exit(1);
    }
    strncpy(entry->word1, word1, WORD_LEN - 1);
    entry->word1[WORD_LEN - 1] = '\0';
    strncpy(entry->word2, word2, WORD_LEN - 1);
    entry->word2[WORD_LEN - 1] = '\0';
    strncpy(entry->word3, word3, WORD_LEN - 1);
    entry->word3[WORD_LEN - 1] = '\0';
    entry->count = 1;
    strncpy(entry->key, key, sizeof(entry->key));
    entry->next = trigramTable[idx];
    trigramTable[idx] = entry;
}

/* Process a line of input by tokenizing and updating both bigram and trigram models */
void process_input(char *input) {
    char *words[MAX_TOKENS];
    int count = tokenize(input, words, MAX_TOKENS);
    for (int i = 0; i < count - 1; i++) {
        update_bigram(words[i], words[i + 1]);
    }
    for (int i = 0; i < count - 2; i++) {
        update_trigram(words[i], words[i + 1], words[i + 2]);
    }
}

/*------------------------------------------
          Model Persistence: Saving and Loading
------------------------------------------*/

/* Save both models to a file in text format */
void save_models(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s for writing\n", filename);
        return;
    }
    /* Count bigrams */
    int bigramCount = 0;
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            bigramCount++;
            entry = entry->next;
        }
    }
    fprintf(file, "BIGRAMS %d\n", bigramCount);
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            fprintf(file, "%s %s %d\n", entry->word1, entry->word2, entry->count);
            entry = entry->next;
        }
    }
    /* Count trigrams */
    int trigramCount = 0;
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            trigramCount++;
            entry = entry->next;
        }
    }
    fprintf(file, "TRIGRAMS %d\n", trigramCount);
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            fprintf(file, "%s %s %s %d\n", entry->word1, entry->word2, entry->word3, entry->count);
            entry = entry->next;
        }
    }
    fclose(file);
}

/* Load models from a file (expects the same format as save_models) */
void load_models(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return;
    char header[20];
    int count;
    /* Load bigrams */
    if (fscanf(file, "%s %d", header, &count) == 2 && strcmp(header, "BIGRAMS") == 0) {
        for (int i = 0; i < count; i++) {
            char w1[WORD_LEN], w2[WORD_LEN];
            int c;
            if (fscanf(file, "%49s %49s %d", w1, w2, &c) == 3) {
                normalize_word(w1);
                normalize_word(w2);
                char key[WORD_LEN * 2 + 2];
                snprintf(key, sizeof(key), "%s#%s", w1, w2);
                unsigned long idx = hash_djb2(key) % BIGRAM_TABLE_SIZE;
                BigramEntry *entry = bigramTable[idx];
                int found = 0;
                while (entry) {
                    if (strcmp(entry->key, key) == 0) {
                        entry->count += c;
                        found = 1;
                        break;
                    }
                    entry = entry->next;
                }
                if (!found) {
                    entry = malloc(sizeof(BigramEntry));
                    if (!entry) {
                        fprintf(stderr, "Memory allocation failed during bigram loading\n");
                        exit(1);
                    }
                    strncpy(entry->word1, w1, WORD_LEN - 1);
                    entry->word1[WORD_LEN - 1] = '\0';
                    strncpy(entry->word2, w2, WORD_LEN - 1);
                    entry->word2[WORD_LEN - 1] = '\0';
                    entry->count = c;
                    strncpy(entry->key, key, sizeof(entry->key));
                    entry->next = bigramTable[idx];
                    bigramTable[idx] = entry;
                }
            }
        }
    }
    /* Load trigrams */
    if (fscanf(file, "%s %d", header, &count) == 2 && strcmp(header, "TRIGRAMS") == 0) {
        for (int i = 0; i < count; i++) {
            char w1[WORD_LEN], w2[WORD_LEN], w3[WORD_LEN];
            int c;
            if (fscanf(file, "%49s %49s %49s %d", w1, w2, w3, &c) == 4) {
                normalize_word(w1);
                normalize_word(w2);
                normalize_word(w3);
                char key[WORD_LEN * 3 + 3];
                snprintf(key, sizeof(key), "%s#%s#%s", w1, w2, w3);
                unsigned long idx = hash_djb2(key) % TRIGRAM_TABLE_SIZE;
                TrigramEntry *entry = trigramTable[idx];
                int found = 0;
                while (entry) {
                    if (strcmp(entry->key, key) == 0) {
                        entry->count += c;
                        found = 1;
                        break;
                    }
                    entry = entry->next;
                }
                if (!found) {
                    entry = malloc(sizeof(TrigramEntry));
                    if (!entry) {
                        fprintf(stderr, "Memory allocation failed during trigram loading\n");
                        exit(1);
                    }
                    strncpy(entry->word1, w1, WORD_LEN - 1);
                    entry->word1[WORD_LEN - 1] = '\0';
                    strncpy(entry->word2, w2, WORD_LEN - 1);
                    entry->word2[WORD_LEN - 1] = '\0';
                    strncpy(entry->word3, w3, WORD_LEN - 1);
                    entry->word3[WORD_LEN - 1] = '\0';
                    entry->count = c;
                    strncpy(entry->key, key, sizeof(entry->key));
                    entry->next = trigramTable[idx];
                    trigramTable[idx] = entry;
                }
            }
        }
    }
    fclose(file);
}

/*------------------------------------------
          Memory Cleanup Functions
------------------------------------------*/

void free_bigrams(void) {
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            BigramEntry *tmp = entry;
            entry = entry->next;
            free(tmp);
        }
        bigramTable[i] = NULL;
    }
}

void free_trigrams(void) {
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            TrigramEntry *tmp = entry;
            entry = entry->next;
            free(tmp);
        }
        trigramTable[i] = NULL;
    }
}

/*------------------------------------------
          Prediction Functions
------------------------------------------*/

/* Predict the next word using the trigram model with weighted random selection.
   Given the previous two words, sum the counts for matching trigrams and choose one at random. */
const char* predict_trigram(const char *prev_word, const char *last_word) {
    int total = 0;
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            if (strcmp(entry->word1, prev_word) == 0 &&
                strcmp(entry->word2, last_word) == 0) {
                total += entry->count;
            }
            entry = entry->next;
        }
    }
    if (total == 0)
        return NULL;
    int r = rand() % total;
    int cumulative = 0;
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            if (strcmp(entry->word1, prev_word) == 0 &&
                strcmp(entry->word2, last_word) == 0) {
                cumulative += entry->count;
                if (r < cumulative)
                    return entry->word3;
            }
            entry = entry->next;
        }
    }
    return NULL;  // Should not reach here.
}

/* Predict the next word using the bigram model with weighted random selection.
   Given a word, sum the counts for matching bigrams and choose one at random. */
const char* predict_bigram(const char *word) {
    int total = 0;
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            if (strcmp(entry->word1, word) == 0)
                total += entry->count;
            entry = entry->next;
        }
    }
    if (total == 0)
        return NULL;
    int r = rand() % total;
    int cumulative = 0;
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            if (strcmp(entry->word1, word) == 0) {
                cumulative += entry->count;
                if (r < cumulative)
                    return entry->word2;
            }
            entry = entry->next;
        }
    }
    return NULL;  // Should not reach here.
}

/*------------------------------------------
          API Functions for Teaching and Running
------------------------------------------*/

/* Teach mode: loads models, accepts input to update the models, and saves upon exit. */
void cmd_teach_sv(char *filename) {
    char input[MAX_INPUT_SIZE];

    init_tables();
    load_models(filename);

    printf("Entering teach mode. Type input sentences to update the model.\n");
    printf("Type 'exit' to save the model and quit.\n");
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
    /* Optionally free memory if no further processing is needed.
       free_bigrams();
       free_trigrams();
    */
}

/* Run mode: loads models and predicts additional words based on the input context. */
void cmd_run_sv(char *filename) {
    char input[MAX_INPUT_SIZE];
    char generated_sentence[MAX_INPUT_SIZE] = {0};

    srand((unsigned int)time(NULL));

    init_tables();
    load_models(filename);

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

        /* Copy input into generated_sentence */
        strncpy(generated_sentence, input, sizeof(generated_sentence) - 1);
        generated_sentence[sizeof(generated_sentence) - 1] = '\0';

        /* Tokenize the input to determine context */
        char input_copy[MAX_INPUT_SIZE];
        strncpy(input_copy, input, sizeof(input_copy) - 1);
        input_copy[sizeof(input_copy) - 1] = '\0';
        char *words[MAX_TOKENS];
        int count = tokenize(input_copy, words, MAX_TOKENS);

        if (count == 0) {
            printf("No valid input detected.\n");
            continue;
        }

        const char *next_word = NULL;
        /* Use trigram prediction if at least two words are available */
        if (count >= 2)
            next_word = predict_trigram(words[count - 2], words[count - 1]);
        /* Fallback to bigram prediction */
        if (!next_word)
            next_word = predict_bigram(words[count - 1]);
        if (!next_word) {
            printf("Prediction: %s\n", generated_sentence);
            continue;
        }

        strncat(generated_sentence, " ", sizeof(generated_sentence) - strlen(generated_sentence) - 1);
        strncat(generated_sentence, next_word, sizeof(generated_sentence) - strlen(generated_sentence) - 1);

        /* Set up context for iterative prediction */
        char current_prev[WORD_LEN];
        char current_last[WORD_LEN];
        if (count >= 2) {
            strncpy(current_prev, words[count - 2], WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, words[count - 1], WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        } else {
            strncpy(current_prev, words[count - 1], WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, next_word, WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        }

        /* Generate up to 10 additional words */
        for (int i = 1; i < 10; i++) {
            next_word = predict_trigram(current_prev, current_last);
            if (!next_word)
                next_word = predict_bigram(current_last);
            if (!next_word)
                break;
            strncat(generated_sentence, " ", sizeof(generated_sentence) - strlen(generated_sentence) - 1);
            strncat(generated_sentence, next_word, sizeof(generated_sentence) - strlen(generated_sentence) - 1);
            /* Update context: shift window by one word */
            strncpy(current_prev, current_last, WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, next_word, WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        }
        printf("Prediction: %s\n", generated_sentence);
    }
    /* Optionally free memory if no further processing is needed.
       free_bigrams();
       free_trigrams();
    */
}

/*------------------------------------------
          (Optional) Main Function for Testing
------------------------------------------*/
/*
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
    
    free_bigrams();
    free_trigrams();
    return 0;
}
*/
