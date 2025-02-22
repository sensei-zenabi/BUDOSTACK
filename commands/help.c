#include <stdio.h>

int main() {
	printf("\n");
    printf("Available Commands:\n");
    printf("\n");
    printf("  help    : Display this help message.\n");
    printf("  list    : List contents of a directory (default is current directory).\n");
    printf("  display : Display the contents of a file.\n");
    printf("  copy    : Copy a file from source to destination.\n");
    printf("  move    : Move (rename) a file from source to destination.\n");
    printf("  remove  : Remove a file.\n");
    printf("  update  : Create an empty file or update its modification time.\n");
    printf("  makedir : Create a new directory.\n");
    printf("  rmdir   : Remove an empty directory.\n");
    printf("  runtask : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("            Type: runtask -help for more details.\n");
	printf("  stats   : Displays basic hardware stats.\n");
    printf("  exit    : Exit the terminal.\n");
	printf("\n");
    printf("  p.s.\n");
    printf("  If you want to start the app faster... type: ./terminal -f\n");
	printf("\n");
    
    return 0;
}
