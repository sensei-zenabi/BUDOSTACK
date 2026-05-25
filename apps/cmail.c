#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMAIL_CONFIG "cmail.ini"
#define CMAIL_MAX_LINE 512
#define CMAIL_MAX_VALUE 256
#define CMAIL_MAX_CMD 4096
#define CMAIL_MAX_SUBJECT 160
#define CMAIL_TMP_FILE "/tmp/budostack_cmail_message.txt"

typedef struct {
    char imap_host[CMAIL_MAX_VALUE];
    char smtp_host[CMAIL_MAX_VALUE];
    char email[CMAIL_MAX_VALUE];
    char password[CMAIL_MAX_VALUE];
    int imap_port;
    int smtp_port;
} CmailConfig;

static void trim_whitespace(char *text)
{
    size_t len = 0;
    char *start = NULL;

    if (text == NULL) {
        return;
    }

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }

    len = strlen(text);
    while (len > 0U && isspace((unsigned char)text[len - 1U])) {
        text[len - 1U] = '\0';
        len--;
    }
}

static int parse_int(const char *value, int fallback)
{
    char *end = NULL;
    long parsed = 0;

    if (value == NULL || *value == '\0') {
        return fallback;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1L || parsed > 65535L) {
        return fallback;
    }

    return (int)parsed;
}

static int load_config(CmailConfig *cfg)
{
    FILE *fp = NULL;
    char line[CMAIL_MAX_LINE];

    if (cfg == NULL) {
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->imap_port = 993;
    cfg->smtp_port = 465;

    fp = fopen(CMAIL_CONFIG, "r");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s: %s\n", CMAIL_CONFIG, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *equals = NULL;
        char *key = NULL;
        char *value = NULL;

        trim_whitespace(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        key = line;
        value = equals + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        if (strcmp(key, "imap_host") == 0) {
            snprintf(cfg->imap_host, sizeof(cfg->imap_host), "%s", value);
        } else if (strcmp(key, "smtp_host") == 0) {
            snprintf(cfg->smtp_host, sizeof(cfg->smtp_host), "%s", value);
        } else if (strcmp(key, "email") == 0) {
            snprintf(cfg->email, sizeof(cfg->email), "%s", value);
        } else if (strcmp(key, "password") == 0) {
            snprintf(cfg->password, sizeof(cfg->password), "%s", value);
        } else if (strcmp(key, "imap_port") == 0) {
            cfg->imap_port = parse_int(value, cfg->imap_port);
        } else if (strcmp(key, "smtp_port") == 0) {
            cfg->smtp_port = parse_int(value, cfg->smtp_port);
        }
    }

    fclose(fp);

    if (cfg->imap_host[0] == '\0' || cfg->smtp_host[0] == '\0' || cfg->email[0] == '\0' || cfg->password[0] == '\0') {
        fprintf(stderr, "Missing required config values in %s.\n", CMAIL_CONFIG);
        return -1;
    }

    return 0;
}


static void print_gmail_setup(void)
{
    printf("Gmail setup (as of May 25, 2026):\n");
    printf("  1) Turn on 2-Step Verification in your Google Account.\n");
    printf("  2) Create a Google App Password for Mail.\n");
    printf("  3) Copy apps/cmail.ini.example to cmail.ini and fill values.\n");
    printf("  4) Use imap.gmail.com:993 and smtp.gmail.com:465.\n");
    printf("  5) Put the 16-character App Password in the password field.\n\n");
}

static void print_help(void)
{
    printf("cmail - ASCII email app\n\n");
    printf("Config file: %s\n", CMAIL_CONFIG);
    printf("Required keys: imap_host, smtp_host, email, password\n");
    printf("Optional keys: imap_port (default 993), smtp_port (default 465)\n\n");
    printf("Commands:\n");
    printf("  l - list inbox headers\n");
    printf("  r - read message by UID\n");
    printf("  s - send message\n");
    printf("  q - quit\n\n");
    printf("Tip: Use app passwords for Gmail/Outlook IMAP+SMTP.\n");
}

static int run_and_stream(const char *cmd)
{
    FILE *pipe = NULL;
    char line[CMAIL_MAX_LINE];
    int status = 0;

    pipe = popen(cmd, "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    while (fgets(line, sizeof(line), pipe) != NULL) {
        fputs(line, stdout);
    }

    status = pclose(pipe);
    if (status != 0) {
        fprintf(stderr, "Command failed with status %d\n", status);
        return -1;
    }
    return 0;
}

static int list_messages(const CmailConfig *cfg)
{
    char cmd[CMAIL_MAX_CMD];

    if (snprintf(cmd, sizeof(cmd),
        "curl --silent --show-error --url \"imaps://%s:%d/INBOX\" --user \"%s:%s\" --request \"FETCH 1:* (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])\"",
        cfg->imap_host,
        cfg->imap_port,
        cfg->email,
        cfg->password) >= (int)sizeof(cmd)) {
        fprintf(stderr, "IMAP command too long.\n");
        return -1;
    }

    return run_and_stream(cmd);
}

static int read_message(const CmailConfig *cfg, const char *uid)
{
    char cmd[CMAIL_MAX_CMD];

    if (snprintf(cmd, sizeof(cmd),
        "curl --silent --show-error --url \"imaps://%s:%d/INBOX;UID=%s\" --user \"%s:%s\"",
        cfg->imap_host,
        cfg->imap_port,
        uid,
        cfg->email,
        cfg->password) >= (int)sizeof(cmd)) {
        fprintf(stderr, "IMAP read command too long.\n");
        return -1;
    }

    return run_and_stream(cmd);
}

static int send_message(const CmailConfig *cfg)
{
    FILE *msg = NULL;
    char to[CMAIL_MAX_VALUE];
    char subject[CMAIL_MAX_SUBJECT];
    char body[CMAIL_MAX_LINE];
    char cmd[CMAIL_MAX_CMD];

    printf("To: ");
    if (fgets(to, sizeof(to), stdin) == NULL) {
        return -1;
    }
    trim_whitespace(to);

    printf("Subject: ");
    if (fgets(subject, sizeof(subject), stdin) == NULL) {
        return -1;
    }
    trim_whitespace(subject);

    msg = fopen(CMAIL_TMP_FILE, "w");
    if (msg == NULL) {
        perror("fopen");
        return -1;
    }

    fprintf(msg, "From: %s\n", cfg->email);
    fprintf(msg, "To: %s\n", to);
    fprintf(msg, "Subject: %s\n", subject);
    fprintf(msg, "\n");

    printf("Body (single dot '.' on line to finish):\n");
    while (fgets(body, sizeof(body), stdin) != NULL) {
        trim_whitespace(body);
        if (strcmp(body, ".") == 0) {
            break;
        }
        fprintf(msg, "%s\n", body);
    }

    fclose(msg);

    if (snprintf(cmd, sizeof(cmd),
        "curl --silent --show-error --url \"smtps://%s:%d\" --ssl-reqd --mail-from \"%s\" --mail-rcpt \"%s\" --upload-file \"%s\" --user \"%s:%s\"",
        cfg->smtp_host,
        cfg->smtp_port,
        cfg->email,
        to,
        CMAIL_TMP_FILE,
        cfg->email,
        cfg->password) >= (int)sizeof(cmd)) {
        fprintf(stderr, "SMTP command too long.\n");
        remove(CMAIL_TMP_FILE);
        return -1;
    }

    if (run_and_stream(cmd) != 0) {
        remove(CMAIL_TMP_FILE);
        return -1;
    }

    remove(CMAIL_TMP_FILE);
    printf("Message sent.\n");
    return 0;
}

int main(void)
{
    CmailConfig cfg;
    char input[CMAIL_MAX_LINE];

    print_help();
    print_gmail_setup();

    if (load_config(&cfg) != 0) {
        fprintf(stderr, "Hint: cp apps/cmail.ini.example cmail.ini\n");
        fprintf(stderr, "Create %s and try again.\n", CMAIL_CONFIG);
        return 1;
    }

    for (;;) {
        printf("\ncmail> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        trim_whitespace(input);

        if (strcmp(input, "q") == 0) {
            break;
        }
        if (strcmp(input, "l") == 0) {
            (void)list_messages(&cfg);
            continue;
        }
        if (strcmp(input, "r") == 0) {
            char uid[32];
            printf("UID: ");
            if (fgets(uid, sizeof(uid), stdin) == NULL) {
                continue;
            }
            trim_whitespace(uid);
            if (uid[0] != '\0') {
                (void)read_message(&cfg, uid);
            }
            continue;
        }
        if (strcmp(input, "s") == 0) {
            (void)send_message(&cfg);
            continue;
        }

        printf("Unknown command '%s'. Use l/r/s/q.\n", input);
    }

    return 0;
}
