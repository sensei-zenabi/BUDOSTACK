#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 10000
#define WORD_LEN 50

typedef struct {
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    int count;
} WordPair;

// Global dynamic model
WordPair *model = NULL;
int model_size = 0;
int model_capacity = 0;

/*------------------------------------------
  Initialization and Memory Management
------------------------------------------*/

// Initialize the model if it has not been allocated yet.
void init_model() {
    if (model == NULL) {
        model = malloc(INITIAL_CAPACITY * sizeof(WordPair));
        if (model == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }
        model_capacity = INITIAL_CAPACITY;
    }
}

// Ensure there is enough capacity for a new WordPair; if not, double the capacity.
void ensure_capacity() {
    if (model_size >= model_capacity) {
        int new_capacity = model_capacity * 2;
        WordPair *temp = realloc(model, new_capacity * sizeof(WordPair));
        if (temp == NULL) {
            fprintf(stderr, "Memory allocation failed during expansion\n");
            exit(1);
        }
        model = temp;
        model_capacity = new_capacity;
    }
}

// Free the allocated model memory.
void free_model() {
    if (model) {
        free(model);
        model = NULL;
    }
}

/*------------------------------------------
  String Utility Functions
------------------------------------------*/

// Trim leading and trailing whitespace from a string.
void trim_whitespace(char *str) {
    char *end;
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

// Normalize a word: convert to lowercase and remove leading/trailing punctuation.
void normalize_word(char *word) {
    // Convert to lowercase
    for (int i = 0; word[i]; i++) {
        word[i] = tolower((unsigned char) word[i]);
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
  Model Update and Persistence Functions
------------------------------------------*/

// Find an existing word pair and update its count; otherwise, add a new pair.
void update_model(const char *word1, const char *word2) {
    // Linear search for the word pair.
    for (int i = 0; i < model_size; i++) {
        if (strcmp(model[i].word1, word1) == 0 && strcmp(model[i].word2, word2) == 0) {
            model[i].count++;
            return;
        }
    }
    // Not found: ensure capacity and add the new pair.
    ensure_capacity();
    strncpy(model[model_size].word1, word1, WORD_LEN - 1);
    model[model_size].word1[WORD_LEN - 1] = '\0';
    strncpy(model[model_size].word2, word2, WORD_LEN - 1);
    model[model_size].word2[WORD_LEN - 1] = '\0';
    model[model_size].count = 1;
    model_size++;
}

// Save the model (word pairs and counts) to a file.
void save_model(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s for writing\n", filename);
        return;
    }
    for (int i = 0; i < model_size; i++) {
        fprintf(file, "%s %s %d\n", model[i].word1, model[i].word2, model[i].count);
    }
    fclose(file);
}

// Load the model from a file (with normalization).
void load_model(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;
    
    char word1[WORD_LEN], word2[WORD_LEN];
    int count;
    while (fscanf(file, "%49s %49s %d", word1, word2, &count) == 3) {
        normalize_word(word1);
        normalize_word(word2);
        ensure_capacity();
        strncpy(model[model_size].word1, word1, WORD_LEN - 1);
        model[model_size].word1[WORD_LEN - 1] = '\0';
        strncpy(model[model_size].word2, word2, WORD_LEN - 1);
        model[model_size].word2[WORD_LEN - 1] = '\0';
        model[model_size].count = count;
        model_size++;
    }
    fclose(file);
}

/*------------------------------------------
  Input Processing and Prediction
------------------------------------------*/

// Process a line of user input by tokenizing and updating the model with word pairs.
void process_input(char *input) {
    // Allocate a temporary array of pointers. (We use INITIAL_CAPACITY for simplicity.)
    char *words[INITIAL_CAPACITY];
    int count = 0;

    char *token = strtok(input, " ");
    while (token && count < INITIAL_CAPACITY) {
        normalize_word(token);
        if (strlen(token) > 0) {  // Skip tokens that become empty after normalization.
            words[count++] = token;
        }
        token = strtok(NULL, " ");
    }

    for (int i = 0; i < count - 1; i++) {
        update_model(words[i], words[i + 1]);
    }
}

// Predict the most frequent next word for a given normalized word.
const char* predict_next_word(const char *word) {
    int max_count = 0;
    const char *best_match = NULL;

    for (int i = 0; i < model_size; i++) {
        if (strcmp(model[i].word1, word) == 0) {
            if (model[i].count > max_count) {
                max_count = model[i].count;
                best_match = model[i].word2;
            }
        }
    }

    return best_match;
}

/*------------------------------------------
  API Functions for Main (cmd_teach_sv and cmd_run_sv)
------------------------------------------*/

// This function serves as the API for teaching/training the model.
// It loads an existing model (if available), processes user input,
// and saves the updated model upon exit.
void cmd_teach_sv(char *filename) {
    char input[1000];

    init_model();
    load_model(filename);

    while (1) {
        printf("teach> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            save_model(filename);
            break;
        }

        process_input(input);
    }
    // Optionally free memory if no further processing is required.
    // free_model();
}

// This function serves as the API for running/testing the model.
// It loads an existing model and allows the user to get sentence completions.
void cmd_run_sv(char *filename) {
    char input[1000];
    char generated_sentence[1000] = {0};

    init_model();
    load_model(filename);

    while (1) {
        printf("run> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            break;
        }

        // Start the generated sentence with the original input.
        strcpy(generated_sentence, input);

        // Make a copy of the input for tokenization.
        char input_copy[1000];
        strcpy(input_copy, input);
        char *token = strtok(input_copy, " ");
        char *last_token = NULL;
        while (token) {
            normalize_word(token);
            last_token = token;
            token = strtok(NULL, " ");
        }

        if (!last_token || strlen(last_token) == 0) {
            printf("No valid input detected.\n");
            continue;
        }

        // Use the normalized last token for prediction.
        char current_word[WORD_LEN];
        strncpy(current_word, last_token, WORD_LEN - 1);
        current_word[WORD_LEN - 1] = '\0';

        // Generate up to 10 additional words.
        for (int i = 0; i < 10; i++) {
            const char *next_word = predict_next_word(current_word);
            if (!next_word)
                break;

            strcat(generated_sentence, " ");
            strcat(generated_sentence, next_word);
            strncpy(current_word, next_word, WORD_LEN - 1);
            current_word[WORD_LEN - 1] = '\0';
        }

        printf("Prediction: %s\n", generated_sentence);
    }
    // Optionally free memory if no further processing is required.
    // free_model();
}
