#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <termio.h>
#include <sys/wait.h>
#include <sys/stat.h>

// Terminal-related functions
void clear() {
	printf("\033[r\033[H\033[J");
	fflush(stdout);
}

static struct termio savemodes;
static int havemodes = 0;
const char* my_tty = 0;

// Allow the terminal to read 1 char at a time
// Be sure to call tty_fix after, otherwise things may be left broken
int tty_break()
{
	struct termio modmodes;
	if(ioctl(fileno(stdin), TCGETA, &savemodes) < 0)
		return -1;
	havemodes = 1;
	modmodes = savemodes;
	modmodes.c_lflag &= ~ICANON;
	modmodes.c_cc[VMIN] = 1;
	modmodes.c_cc[VTIME] = 0;
	return ioctl(fileno(stdin), TCSETAW, &modmodes);
}

int tty_fix()
{
	if(!havemodes)
		return 0;
	return ioctl(fileno(stdin), TCSETAW, &savemodes);
}
int fexists(const char* file) {
	int fd = open(file, O_CREAT | O_WRONLY | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		if (errno == EEXIST) {
			return 1;
		}
	} else
		close(fd);
	return 0;
}

void usage() {
	printf("ttylm - teletype terminal login manager\n");
	printf("\tUsage: ttylm [-p [USER]]\n");
	printf("\n");
	printf("\t-p, --primary    Define a 'primary' user that we should login to at bootup. This should only be defined for a single tty\n");
	exit(0);
}

//Ask the user
int user_sure(const char* what) {
	if (what) {
		printf("%s [y/n]: ", what);
	} else {
		printf("Are you sure? [y/n]: ");
	}
	tty_break();
	char res = getchar();
	tty_fix();
	if (res == 'y' || res == 'Y') return 1;
	return 0;
}
int get_user_uid(const char* uname) {
	struct passwd pwd;
	struct passwd* result;
	char buf[1024];
	int res = getpwnam_r(uname, &pwd, buf, sizeof buf, &result);
	if (res == 0 && result == &pwd) {
		return pwd.pw_uid;
	} else return -1;
}


// We check to see if we've already ran (and have just been re-ran for some reason)
// Using the flag-file /tmp/init_ttylm_primary
int check_if_rerun() {
	return fexists("/tmp/init_ttylm_primary");
}
void run_terminal() {
	clear();
	execvp("/bin/login", (const char*[]){"login", 0});
	fprintf(stderr, "Fatal erroring executing /bin/login...\n");
	sleep(3);
	return;
}
void run_graphical(const char* uname, int noauth) {
	int ttylm_uid, user_uid;
	char user[512];
	if ((ttylm_uid = get_user_uid("ttylm")) < 0) {
		// Attempt to create the ttylm account
		int ret;
		if ((ret = system("/usr/sbin/useradd --system -M ttylm -s /sbin/nologin -G wheel")) == 0) {
			if ((ttylm_uid = get_user_uid("ttylm")) > 0) {
				goto ttylm_uid_ok;
			}	
		}
		fprintf(stderr, "Unable to load into user ttylm..."); 
		sleep(3);
		return;
	}
ttylm_uid_ok:
	// Get user if not provided
	if (uname == 0) {
		// More than large enough for any username. 
		clear();
		printf("    Grahipcal Environment Login\n\n");
		printf("Username: ");
		fflush(stdout);
		int srd = read(STDIN_FILENO, user, 512);
		if (srd <= 0) {
			exit(-1);
		}
		//Remove the trailing '\n', and replace it with the null terminator
		user[srd - 1] = 0;

		uname = user;
	} else strcpy(user, uname);
	if ((user_uid = get_user_uid(user)) < 0) {
		fprintf(stderr, "Unable to identify user %s\n", user, errno);
		sleep(3);
		return;
	}
	// The user exists
	// Get out tty device
	char* tty = ttyname(0);
	if (tty == NULL) {
		// not a tty. Hmmm
		fprintf(stderr, "Not a tty!\n");
		sleep(3);
		return;
	}
	// Give our tty '777' permissions so we (hopefully) don't have a problem
	chmod(tty, /* octal */ 0777);

	int tty_n;
	sscanf(tty, "/dev/tty%d", &tty_n);
	char startx_tty_str[64];
	sprintf(startx_tty_str, "vt%d", tty_n);

	// double-fork so we can call su in a lower permissions level
	// us -> setuid(ttylm_uid) -> 'su'
	// the ttylm_uid fork will just return whatever su does
	if (!noauth) {
		if (fork() == 0) {
			// de-escalate to ttylm_uid
			// Now run 'su' into the user to ask for the desired user's password
			setuid(ttylm_uid);
			// Second fork() to exec su
			if (fork() == 0) {
				execvp("su", (const char*[]) {"su", user, "-c", "exit 0", 0});
			}
			int status;
			wait(&status);
			exit(WEXITSTATUS(status));
		}
		int status;
		wait(&status);
		noauth = WEXITSTATUS(status) == 0;
	}
	if (noauth) {
		// we are root, so su should not ask for noauthentication
		char cmd[512];
		sprintf(cmd, "echo \"exec startx -- %s\" | su %s -l", startx_tty_str, user);
		system(cmd);
		return;
	} else {
		sleep(1);
		// Just return here. We could exit(), but I'm not that paranoid (yet)
		return;
	}
}
void run_shutdown() {
	if (user_sure("Are you sure you want to shutdown?")) {
		system("shutdown -P 0");
	}
}
void run_reboot() {
	if (user_sure("Are you sure you want to reboot?"))
		system("shutdown -r 0");
}
void run_self_reinit() {
	// Exit. init will (hopefully) restart us.
	execvp("/bin/ttylm", (char*[]) {"/bin/ttylm", 0});
}
// Default user on initial bootup.
// If defined, and the file '/tmp/init_ttylm_primary' does not exist
// We will create that file then boot into that user, without requiring a password
const char* init_user = 0;
const struct option long_opts[] = {
	{"primary", 1, 0, 'p'},
	{0, 0, 0, 0}
};


int main(int argc, char** argv) {
	if (getuid() != 0) {
		fprintf(stderr, "Must run as root\n");
		sleep(3);
		exit(-1);
	}
	int opt;
	while ((opt = getopt_long(argc, argv, "p:", &long_opts[0], 0)) != -1) {
		switch(opt) {
			case 'p':
				init_user = optarg;
				break;
			case '?':
				usage();
				break;
			default:
				{}
		}
	}
	// Check if rerun iff we are a primary ttylm
	if (init_user && check_if_rerun()) {
		// We are a re-run. Do not init into a user
		init_user = 0;
	}
	if (init_user) {
		run_graphical(init_user, 1);
	}
	while (1) {
		clear();
		const char* tty = ttyname(0);
		printf("  ttylm on %s\n\n", tty);
		printf("Options:\n");
		printf("  (T) Login to a terminal\n");
		printf("  (G) Login to a graphical environment\n");
		printf("  (S) Shutdown the system\n");
		printf("  (R) Reboot the system\n");
		printf("  (Z) Re-initialize the login manager\n");
		printf("Selected Option: ");
		tty_break();
		char opt = getchar();
		tty_fix();
		printf("\n");
		switch(opt) {
			case 'T':
			case 't':
				run_terminal();
				break;
			case 'G':
			case 'g':
				run_graphical(0, 0);
				break;
			case 'S':
			case 's':
				run_shutdown();
				break;
			case 'R':
			case 'r':
				run_reboot();
				break;
			case 'Z':
			case 'z':
				run_self_reinit();
				break;
		}
		usleep(200 * 1000);
	}

}
