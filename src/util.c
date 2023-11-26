#include "util.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int read_password(char *dest, size_t len)
{
	struct termios old_terminal;

	if (tcgetattr(STDIN_FILENO, &old_terminal) == -1) {
		return -1;
	}

	struct termios new_terminal = old_terminal;
	new_terminal.c_lflag &= ~(ECHO);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &new_terminal) == -1) {
		return -1;
	}
	// the \n is stored, we replace it with \0
	if (fgets(dest, len, stdin) == NULL) {
		dest[0] = '\0';
	} else {
		dest[strlen(dest) - 1] = '\0';
	}

	if (tcsetattr(STDIN_FILENO, TCSANOW, &old_terminal)) {
		return -1;
	}

	return 0;
}
