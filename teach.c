#include <stdio.h>
#include <string.h>

// Placeholder for cmd_teach_sv function
void cmd_teach_sv(char *filename) {

	char input[1000];

	while (1) {
		printf("teach> "); // prompt
		fgets(input, sizeof(input), stdin); // read prompt

		input[strcspn(input, "\n")] = '\0';  // Remove newline character

		printf("%s\n", input);

		if (strcmp(input,"exit") == 0) {
			break;
		}
	}
}
