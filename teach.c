#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_WORDS 10000
#define WORD_LEN 50

typedef struct {
    char word1[WORD_LEN];
    char word2[WORD_LEN];
    int count;
} WordPair;

WordPair model[MAX_WORDS];
int model_size = 0;

// Function to trim whitespace from a string
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

// Function to normalize a word: convert to lowercase and remove leading/trailing punctuation/special characters.
void normalize_word(char *word) {
    // Convert to lowercase
    for (int i = 0; word[i]; i++) {
        word[i] = tolower((unsigned char) word[i]);
    }

    // Remove leading non-alphanumeric characters
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
    
    // Remove trailing non-alphanumeric characters
    int len = strlen(word);
    while (len > 0 && !isalnum((unsigned char)word[len - 1])) {
        word[len - 1] = '\0';
        len--;
    }
}

// Function to find or add a word pair in the model
void update_model(const char *word1, const char *word2) {
    for (int i = 0; i < model_size; i++) {
        if (strcmp(model[i].word1, word1) == 0 && strcmp(model[i].word2, word2) == 0) {
            model[i].count++;
            return;
        }
    }
    if (model_size < MAX_WORDS) {
        strcpy(model[model_size].word1, word1);
        strcpy(model[model_size].word2, word2);
        model[model_size].count = 1;
        model_size++;
    }
}

// Function to save the model to a file
void save_model(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return;
    }
    for (int i = 0; i < model_size; i++) {
        fprintf(file, "%s %s %d\n", model[i].word1, model[i].word2, model[i].count);
    }
    fclose(file);
}

// Function to load the model from a file (with normalization)
void load_model(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;
    
    char word1[WORD_LEN], word2[WORD_LEN];
    int count;
    while (fscanf(file, "%49s %49s %d", word1, word2, &count) == 3) {
        normalize_word(word1);
        normalize_word(word2);
        strcpy(model[model_size].word1, word1);
        strcpy(model[model_size].word2, word2);
        model[model_size].count = count;
        model_size++;
    }
    fclose(file);
}

// Function to process user input and update the model (with normalization)
void process_input(char *input) {
    char *words[MAX_WORDS];
    int count = 0;

    char *token = strtok(input, " ");
    while (token && count < MAX_WORDS) {
        normalize_word(token);
        if (strlen(token) > 0) {  // Skip if token becomes empty after normalization.
            words[count++] = token;
        }
        token = strtok(NULL, " ");
    }

    for (int i = 0; i < count - 1; i++) {
        update_model(words[i], words[i + 1]);
    }
}

// Main function to teach the model
void cmd_teach_sv(char *filename) {
    char input[1000];

    load_model(filename);

    while (1) {
        printf("teach> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            save_model(filename);
            break;
        }

        process_input(input);
    }
}

// Function to predict the most frequent next word (expects normalized input)
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

// Function to run the model in test mode with sentence completion
void cmd_run_sv(char *filename) {
    char input[1000];
    char generated_sentence[1000] = {0};

    load_model(filename);

    while (1) {
        printf("run> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            break;
        }

        // Start the generated sentence with the original input.
        strcpy(generated_sentence, input);

        // Use a copy of the input for tokenization (to preserve the original input)
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

        // Use the normalized last token for prediction
        char current_word[WORD_LEN];
        strncpy(current_word, last_token, WORD_LEN - 1);
        current_word[WORD_LEN - 1] = '\0';

        // Generate additional words based on predictions
        for (int i = 0; i < 10; i++) { // Limit output to 10 additional words
            const char *next_word = predict_next_word(current_word);
            if (!next_word) break;

            strcat(generated_sentence, " ");
            strcat(generated_sentence, next_word);
            strncpy(current_word, next_word, WORD_LEN - 1);
            current_word[WORD_LEN - 1] = '\0';
        }

        printf("Prediction: %s\n", generated_sentence);
    }
}

