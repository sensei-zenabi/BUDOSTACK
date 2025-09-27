#define _XOPEN_SOURCE 600  // Feature test macro to expose usleep
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>

// Board dimensions. Match width and height so the playfield remains square when
// rendered with the 8x8 terminal font.
#define WIDTH 20
#define HEIGHT 20
// Maximum snake length
#define MAX_SNAKE_LENGTH 100

// Minimum delay (in microseconds) to ensure game remains playable
#define MIN_DELAY 30000

// Global delay variable (in microseconds) controlling game speed
unsigned int delay_time = 100000;

// Enum for snake movement directions
enum Direction { UP, DOWN, LEFT, RIGHT };

// Structure to represent a point on the board
typedef struct {
    int x;
    int y;
} Point;

// Global snake array, its current length, and direction
Point snake[MAX_SNAKE_LENGTH];
int snake_length = 3;
enum Direction dir = RIGHT;

// Global fruit position
Point fruit;

// Flag to indicate game over state
int game_over = 0;

// Terminal settings storage so we can restore them
struct termios orig_termios;

// Function to restore the original terminal settings
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Function to set terminal to raw mode for non-blocking key input
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode); // restore settings on exit
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); // disable echo and canonical mode
    raw.c_cc[VMIN] = 0;  // no minimum characters for non-blocking input
    raw.c_cc[VTIME] = 1; // timeout (0.1 seconds) for read
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Initialize or restart the game: reset snake and fruit positions, direction, game_over flag, and delay
void initGame() {
    // Reset snake length, direction, and delay
    snake_length = 3;
    dir = RIGHT;
    delay_time = 100000;
    // Start snake at center going right
    snake[0].x = WIDTH / 2;
    snake[0].y = HEIGHT / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;
    
    // Seed random and place the first fruit
    srand(time(NULL));
    fruit.x = rand() % WIDTH;
    fruit.y = rand() % HEIGHT;
    
    game_over = 0;
}

// Read a single character from input (non-blocking)
char getInput() {
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    if(n == 1)
        return c;
    return 0;
}

// Update the snake's movement direction based on user input.
// Supports arrow keys and WASD controls. 'q' quits and 'r' restarts.
void updateDirection() {
    char c = getInput();
    if(c == 0)
        return;
    // Check for escape sequence (arrow keys)
    if(c == '\033') {
        char seq[2];
        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return;
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return;
        if(seq[0] == '[') {
            if(seq[1] == 'A' && dir != DOWN)       // Up arrow
                dir = UP;
            else if(seq[1] == 'B' && dir != UP)      // Down arrow
                dir = DOWN;
            else if(seq[1] == 'C' && dir != LEFT)    // Right arrow
                dir = RIGHT;
            else if(seq[1] == 'D' && dir != RIGHT)   // Left arrow
                dir = LEFT;
        }
    } else {
        // WASD controls, plus r to restart and q to quit
        if((c == 'w' || c == 'W') && dir != DOWN)
            dir = UP;
        else if((c == 's' || c == 'S') && dir != UP)
            dir = DOWN;
        else if((c == 'a' || c == 'A') && dir != RIGHT)
            dir = LEFT;
        else if((c == 'd' || c == 'D') && dir != LEFT)
            dir = RIGHT;
        else if(c == 'q' || c == 'Q')
            exit(0);
        else if(c == 'r' || c == 'R') {
            initGame();
        }
    }
}

// Update the snake position based on the current direction and check for collisions.
// Instead of exiting on collision, set game_over to 1.
void updateSnake() {
    // Calculate new head position
    Point new_head = snake[0];
    switch(dir) {
        case UP:    new_head.y--; break;
        case DOWN:  new_head.y++; break;
        case LEFT:  new_head.x--; break;
        case RIGHT: new_head.x++; break;
    }
    // Check collision with walls
    if(new_head.x < 0 || new_head.x >= WIDTH || new_head.y < 0 || new_head.y >= HEIGHT) {
        game_over = 1;
        return;
    }
    // Check collision with itself
    for (int i = 0; i < snake_length; i++) {
        if(snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = 1;
            return;
        }
    }
    // Move snake segments: shift each segment to the position of the previous one
    for (int i = snake_length; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = new_head;
    
    // Check if fruit is eaten
    if(new_head.x == fruit.x && new_head.y == fruit.y) {
        snake_length++;
        if(snake_length >= MAX_SNAKE_LENGTH)
            snake_length = MAX_SNAKE_LENGTH;
        // Calculate score (number of apples eaten)
        int score = snake_length - 3;
        // Speed up after every 5 apples, reducing delay by 10000 microseconds until a minimum delay is reached
        if(score % 5 == 0 && delay_time > MIN_DELAY) {
            delay_time -= 10000;
        }
        // Place new fruit ensuring it does not appear on the snake
        int valid = 0;
        while(!valid) {
            fruit.x = rand() % WIDTH;
            fruit.y = rand() % HEIGHT;
            valid = 1;
            for (int i = 0; i < snake_length; i++) {
                if(snake[i].x == fruit.x && snake[i].y == fruit.y) {
                    valid = 0;
                    break;
                }
            }
        }
    }
}

// Draw the game board using line-drawing characters for the borders,
// and display the snake, fruit, score, and instructions.
void drawBoard() {
    // Clear the screen and move cursor to home position
    printf("\033[2J\033[H");
    
    // Draw top border using line-drawing characters
    printf("┌");
    for (int x = 0; x < WIDTH; x++) {
        printf("─");
    }
    printf("┐\n");
    
    // Draw board rows with left/right borders
    for (int y = 0; y < HEIGHT; y++) {
        printf("│"); // left border
        for (int x = 0; x < WIDTH; x++) {
            int printed = 0;
            // Draw fruit if at this position
            if(fruit.x == x && fruit.y == y) {
                printf("*");
                printed = 1;
            } else {
                // Check if snake occupies this cell
                for (int k = 0; k < snake_length; k++) {
                    if(snake[k].x == x && snake[k].y == y) {
                        if(k == 0)
                            // Unicode full block for snake head
                            printf("\u2588");
                        else
                            // Unicode dark shade block for snake body
                            printf("\u2593");
                        printed = 1;
                        break;
                    }
                }
            }
            if(!printed)
                printf(" ");
        }
        printf("│\n"); // right border
    }
    
    // Draw bottom border using line-drawing characters
    printf("└");
    for (int x = 0; x < WIDTH; x++) {
        printf("─");
    }
    printf("┘\n");
    
    // Display game status, score, and instructions
    if(game_over)
        printf("Game Over!\n");
    printf("Score: %d\n", snake_length - 3);
    printf("Press 'r' to restart, 'q' to quit.\n");
}

// Main game loop: if game_over is set, wait for 'r' (restart) or 'q' (quit)
int main() {
    enableRawMode();
    initGame();
    
    while(1) {
        if(!game_over) {
            updateDirection();  // process user input
            updateSnake();      // update snake's position and check collisions
        }
        
        drawBoard();        // render the game board
        
        // If game over, wait for restart or quit input
        if(game_over) {
            char c = getInput();
            if(c == 'r' || c == 'R') {
                initGame();
            } else if(c == 'q' || c == 'Q') {
                break;
            }
        }
        
        usleep(delay_time); // delay to control game speed
    }
    
    return 0;
}
