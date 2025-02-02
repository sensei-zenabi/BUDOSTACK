/* teach.c - A simple teaching/prediction tool using bigrams and trigrams
 * 
 * This version includes improvements to the run mode:
 *   1. The prompt is no longer repeated as part of the output.
 *   2. A simple heuristic distinguishes questions (by checking for '?')
 *      and, if so, prepends a fixed conversational phrase.
 *   3. The final generated response is “humanized” by capitalizing its first letter
 *      and appending appropriate punctuation.
 *
 * Author: Your Name
 * Date: 2025-02-02
 */

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

#define START_TOKEN "<s>"
#define END_TOKEN   "</s>"

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

/* Normalize a word: convert to lowercase and remove leading/trailing punctuation.
   Special tokens (START_TOKEN and END_TOKEN) are preserved unchanged. */
void normalize_word(char *word) {
    if (strcmp(word, START_TOKEN) == 0 || strcmp(word, END_TOKEN) == 0)
        return;
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

/* Process a line of input by inserting sentence boundaries, tokenizing,
   and updating both bigram and trigram models */
void process_input(char *input) {
    char buffer[MAX_INPUT_SIZE * 2];
    /* Prepend START_TOKEN and append END_TOKEN */
    snprintf(buffer, sizeof(buffer), "%s %s %s", START_TOKEN, input, END_TOKEN);
    char *words[MAX_TOKENS];
    int count = tokenize(buffer, words, MAX_TOKENS);
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
    return NULL;  /* Should not reach here. */
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
    return NULL;  /* Should not reach here. */
}

/*------------------------------------------
      Helper Functions for Performance Metrics
------------------------------------------*/
int count_bigrams(void) {
    int total = 0;
    for (int i = 0; i < BIGRAM_TABLE_SIZE; i++) {
        BigramEntry *entry = bigramTable[i];
        while (entry) {
            total++;
            entry = entry->next;
        }
    }
    return total;
}

int count_trigrams(void) {
    int total = 0;
    for (int i = 0; i < TRIGRAM_TABLE_SIZE; i++) {
        TrigramEntry *entry = trigramTable[i];
        while (entry) {
            total++;
            entry = entry->next;
        }
    }
    return total;
}

/*------------------------------------------
      Additional Helper Functions for Run Mode
------------------------------------------*/
/* Check if the input prompt ends with a '?' */
int is_question(const char *input) {
    size_t len = strlen(input);
    while (len > 0 && isspace((unsigned char)input[len - 1]))
        len--;
    return (len > 0 && input[len - 1] == '?');
}

/* Humanize the generated response:
   - Capitalize the first letter.
   - Append a period if no ending punctuation exists.
*/
void humanize_response(char *response) {
    if (response[0] != '\0') {
        response[0] = toupper((unsigned char)response[0]);
    }
    size_t len = strlen(response);
    if (len > 0 && response[len - 1] != '.' &&
        response[len - 1] != '!' && response[len - 1] != '?') {
        strncat(response, ".", sizeof(response) - strlen(response) - 1);
    }
}

/*------------------------------------------
          API Functions for Teaching and Running
------------------------------------------*/

/* Teaching mode: interactive or automatic teaching of the model */
void cmd_teach_sv(char *filename) {
    char input[MAX_INPUT_SIZE];
    
    init_tables();
    load_models(filename);
    
    printf("Welcome to the SV Teaching Tool.\n");
    printf("Would you like to use manual teaching mode?\n");
    printf("Type 'y' (followed by Enter) for manual mode,\n"
           "or simply press Enter (or any key other than 'y') to use automatic mode.\n");
    printf("Your choice: ");
    if (!fgets(input, sizeof(input), stdin)) {
        fprintf(stderr, "Input error.\n");
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    trim_whitespace(input);
    
    /* If user entered exactly "y", go to manual teaching mode */
    if (strcmp(input, "y") == 0 || strcmp(input, "Y") == 0) {
        printf("Manual teaching mode selected.\n");
        printf("Enter sentences to update the model. Type 'exit' to save and quit.\n");
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
    }
    /* Otherwise, run automatic teaching mode */
    else {
        char materialFilename[256];
        char promptsFilename[256];
        FILE *materialFile, *promptsFile, *resultsFile;
        clock_t teach_start, teach_end, predict_start, predict_end;
        double teach_time, predict_time;
        
        printf("Automatic teaching mode selected.\n");
        printf("You will now be prompted to enter the filenames for the teaching material and run prompts.\n\n");
        
        /* Get the teaching material filename */
        printf("Enter the filename for teaching material (e.g., material.txt): ");
        if (!fgets(materialFilename, sizeof(materialFilename), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        materialFilename[strcspn(materialFilename, "\n")] = '\0';
        trim_whitespace(materialFilename);
        if (strlen(materialFilename) == 0) {
            fprintf(stderr, "No filename provided for teaching material.\n");
            return;
        }
        
        /* Get the run prompts filename */
        printf("Enter the filename for run prompts (e.g., prompts.txt): ");
        if (!fgets(promptsFilename, sizeof(promptsFilename), stdin)) {
            fprintf(stderr, "Input error.\n");
            return;
        }
        promptsFilename[strcspn(promptsFilename, "\n")] = '\0';
        trim_whitespace(promptsFilename);
        if (strlen(promptsFilename) == 0) {
            fprintf(stderr, "No filename provided for run prompts.\n");
            return;
        }
        
        /* Teaching Phase */
        teach_start = clock();
        materialFile = fopen(materialFilename, "r");
        if (!materialFile) {
            fprintf(stderr, "Error: Could not open teaching material file %s\n", materialFilename);
            return;
        }
        printf("\nProcessing teaching material from %s...\n", materialFilename);
        while (fgets(input, sizeof(input), materialFile)) {
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strlen(input) > 0) {
                process_input(input);
            }
        }
        fclose(materialFile);
        teach_end = clock();
        teach_time = ((double)(teach_end - teach_start)) / CLOCKS_PER_SEC;
        printf("Teaching material processed in %.2f seconds.\n", teach_time);
        
        /* Prediction (Validation) Phase */
        predict_start = clock();
        promptsFile = fopen(promptsFilename, "r");
        if (!promptsFile) {
            fprintf(stderr, "Error: Could not open run prompts file %s\n", promptsFilename);
            return;
        }
        resultsFile = fopen("results.txt", "w");
        if (!resultsFile) {
            fprintf(stderr, "Error: Could not open results.txt for writing.\n");
            fclose(promptsFile);
            return;
        }
        printf("Processing run prompts from %s and saving predictions to results.txt...\n", promptsFilename);
        
        while (fgets(input, sizeof(input), promptsFile)) {
            char prompt[MAX_INPUT_SIZE];
            char generated_sentence[MAX_INPUT_SIZE];
            char input_copy[MAX_INPUT_SIZE];
            char *words[MAX_TOKENS];
            int count;
            const char *next_word = NULL;
            
            /* Remove newline and trim the prompt */
            input[strcspn(input, "\n")] = '\0';
            trim_whitespace(input);
            if (strlen(input) == 0)
                continue;
            
            /* Copy the prompt for processing */
            strncpy(prompt, input, sizeof(prompt) - 1);
            prompt[sizeof(prompt) - 1] = '\0';
            strncpy(generated_sentence, prompt, sizeof(generated_sentence) - 1);
            generated_sentence[sizeof(generated_sentence) - 1] = '\0';
            
            /* Tokenize the prompt */
            strncpy(input_copy, prompt, sizeof(input_copy) - 1);
            input_copy[sizeof(input_copy) - 1] = '\0';
            count = tokenize(input_copy, words, MAX_TOKENS);
            
            if (count == 0) {
                fprintf(resultsFile, "Prompt: %s\nNo valid input detected.\n\n", prompt);
                continue;
            }
            
            /* First prediction: try trigram then bigram */
            if (count >= 2)
                next_word = predict_trigram(words[count - 2], words[count - 1]);
            if (!next_word)
                next_word = predict_bigram(words[count - 1]);
            if (!next_word) {
                fprintf(resultsFile, "Prompt: %s\nPrediction: %s\n\n", prompt, generated_sentence);
                continue;
            }
            
            /* If a boundary token is predicted, do not append it */
            if (strcmp(next_word, START_TOKEN) == 0 || strcmp(next_word, END_TOKEN) == 0) {
                fprintf(resultsFile, "Prompt: %s\nPrediction: %s\n\n", prompt, generated_sentence);
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
                if (strcmp(next_word, START_TOKEN) == 0 || strcmp(next_word, END_TOKEN) == 0)
                    break;
                strncat(generated_sentence, " ", sizeof(generated_sentence) - strlen(generated_sentence) - 1);
                strncat(generated_sentence, next_word, sizeof(generated_sentence) - strlen(generated_sentence) - 1);
                /* Update context: shift window by one word */
                strncpy(current_prev, current_last, WORD_LEN - 1);
                current_prev[WORD_LEN - 1] = '\0';
                strncpy(current_last, next_word, WORD_LEN - 1);
                current_last[WORD_LEN - 1] = '\0';
            }
            
            fprintf(resultsFile, "Prompt: %s\nPrediction: %s\n\n", prompt, generated_sentence);
        }
        fclose(promptsFile);
        predict_end = clock();
        predict_time = ((double)(predict_end - predict_start)) / CLOCKS_PER_SEC;
        
        /* Append performance metrics */
        int total_bigrams = count_bigrams();
        int total_trigrams = count_trigrams();
        int total_parameters = total_bigrams + total_trigrams;
        fprintf(resultsFile, "----- Performance Metrics -----\n");
        fprintf(resultsFile, "Total number of parameters (bigrams + trigrams): %d\n", total_parameters);
        fprintf(resultsFile, "Teaching time: %.2f seconds\n", teach_time);
        fprintf(resultsFile, "Prediction time: %.2f seconds\n", predict_time);
        fclose(resultsFile);
        printf("Automatic teaching and prediction complete. Results (including performance metrics) are saved in results.txt\n");
        
        /* Save the updated model */
        save_models(filename);
    }
}

/* 
 * Improved Run Mode:
 *
 * The run mode now:
 *  - Uses the input only for context, not as part of the output.
 *  - Detects if the prompt ends with a question and, if so, prepends a fixed conversational phrase.
 *  - Post-processes the generated sentence to capitalize its first letter and append a period if needed.
 */
void cmd_run_sv(char *filename) {
    char input[MAX_INPUT_SIZE];
    char response[MAX_INPUT_SIZE] = {0};

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

        /* Determine if input is a question */
        int input_is_question = is_question(input);

        /* Tokenize the input for context (do not echo prompt in response) */
        char input_copy[MAX_INPUT_SIZE];
        strncpy(input_copy, input, sizeof(input_copy) - 1);
        input_copy[sizeof(input_copy) - 1] = '\0';
        char *words[MAX_TOKENS];
        int count = tokenize(input_copy, words, MAX_TOKENS);

        if (count == 0) {
            printf("No valid input detected.\n");
            continue;
        }

        /* Start with an empty response. If the input is a question, add a conversational prefix. */
        response[0] = '\0';
        if (input_is_question) {
            const char *question_prefixes[] = {"I think", "Well", "Perhaps", "In my opinion"};
            int num_prefixes = sizeof(question_prefixes) / sizeof(question_prefixes[0]);
            int idx = rand() % num_prefixes;
            strncat(response, question_prefixes[idx], sizeof(response) - strlen(response) - 1);
        }

        /* Predict the next word using context from the prompt */
        const char *next_word = NULL;
        if (count >= 2)
            next_word = predict_trigram(words[count - 2], words[count - 1]);
        if (!next_word)
            next_word = predict_bigram(words[count - 1]);
        if (!next_word ||
            strcmp(next_word, START_TOKEN) == 0 ||
            strcmp(next_word, END_TOKEN) == 0) {
            printf("No valid continuation predicted.\n");
            continue;
        }

        /* Append the first predicted word */
        strncat(response, " ", sizeof(response) - strlen(response) - 1);
        strncat(response, next_word, sizeof(response) - strlen(response) - 1);

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
            if (!next_word ||
                strcmp(next_word, START_TOKEN) == 0 ||
                strcmp(next_word, END_TOKEN) == 0)
                break;
            strncat(response, " ", sizeof(response) - strlen(response) - 1);
            strncat(response, next_word, sizeof(response) - strlen(response) - 1);
            /* Shift context: update the sliding window */
            strncpy(current_prev, current_last, WORD_LEN - 1);
            current_prev[WORD_LEN - 1] = '\0';
            strncpy(current_last, next_word, WORD_LEN - 1);
            current_last[WORD_LEN - 1] = '\0';
        }

        /* Final cosmetic touches to humanize the output */
        humanize_response(response);
        printf("Prediction: %s\n", response);
    }
}

/*------------------------------------------
          (Optional) Main Function for Testing
------------------------------------------*/
#if 0
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
#endif
