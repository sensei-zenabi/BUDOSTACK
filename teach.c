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
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
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

// Function to load the model from a file
void load_model(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;
    
    char word1[WORD_LEN], word2[WORD_LEN];
    int count;
    while (fscanf(file, "%49s %49s %d", word1, word2, &count) == 3) {
        strcpy(model[model_size].word1, word1);
        strcpy(model[model_size].word2, word2);
        model[model_size].count = count;
        model_size++;
    }
    fclose(file);
}

// Function to process user input and update the model
void process_input(char *input) {
    char *words[MAX_WORDS];
    int count = 0;

    char *token = strtok(input, " ");
    while (token && count < MAX_WORDS) {
        words[count++] = token;
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
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            save_model(filename);
            break;
        }

        process_input(input);
    }
}

// Function to predict the most frequent next word
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
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
        trim_whitespace(input);

        if (strcmp(input, "exit") == 0) {
            break;
        }

        strcpy(generated_sentence, input);
        char *last_word = strtok(input, " ");
        char *current_word = last_word;

        // Find the last word in the input to predict from
        while (last_word != NULL) {
            current_word = last_word;
            last_word = strtok(NULL, " ");
        }

        if (!current_word) {
            printf("No input detected.\n");
            continue;
        }

        // Generate words based on predictions
        for (int i = 0; i < 10; i++) { // Limits the output to 10 additional words
            const char *next_word = predict_next_word(current_word);
            if (!next_word) break;

            strcat(generated_sentence, " ");
            strcat(generated_sentence, next_word);
            current_word = (char *)next_word;
        }

        printf("Prediction: %s\n", generated_sentence);
    }
}
