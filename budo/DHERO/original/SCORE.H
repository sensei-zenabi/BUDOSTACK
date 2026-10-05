#ifndef SCORE_H
#define SCORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <conio.h>
#include <sys/movedata.h>

#define SCORE_FILE       "DATA\\GAME\\SCORES.DAT"
#define SCORE_TOP_COUNT  10
#define SCORE_NAME_LEN   21
#define PRESENT_VGA      0xA0000
#define PRESENT_W        320
#define PRESENT_H        200

typedef struct
{
    char name[SCORE_NAME_LEN];
    long score;
} ScoreEntry;

static int score_presented = 0;

static void score_set_mode(int mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = (unsigned short)mode;
    __dpmi_int(0x10, &r);
}

static void present_set_palette(void)
{
    outportb(0x3C8, 0);
    outportb(0x3C9, 0);  outportb(0x3C9, 0);  outportb(0x3C9, 0);
    outportb(0x3C9, 16); outportb(0x3C9, 16); outportb(0x3C9, 16);
    outportb(0x3C9, 38); outportb(0x3C9, 38); outportb(0x3C9, 38);
    outportb(0x3C9, 63); outportb(0x3C9, 63); outportb(0x3C9, 63);
}

static void present_put_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || y < 0 || x >= PRESENT_W || y >= PRESENT_H) return;
    dosmemput(&color, 1, PRESENT_VGA + y * PRESENT_W + x);
}

static void present_fill_rect(int x, int y, int w, int h,
                              unsigned char color)
{
    unsigned char row[PRESENT_W];
    int i;
    int yy;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > PRESENT_W || y + h > PRESENT_H) return;
    for (i = 0; i < w; i++) row[i] = color;
    for (yy = 0; yy < h; yy++)
        dosmemput(row, w, PRESENT_VGA + (y + yy) * PRESENT_W + x);
}

static void present_hline(int x1, int x2, int y, unsigned char color)
{
    int x;
    for (x = x1; x <= x2; x++) present_put_pixel(x, y, color);
}

static void present_vline(int x, int y1, int y2, unsigned char color)
{
    int y;
    for (y = y1; y <= y2; y++) present_put_pixel(x, y, color);
}

static void present_frame(int x, int y, int w, int h,
                          unsigned char color)
{
    present_hline(x, x + w - 1, y, color);
    present_hline(x, x + w - 1, y + h - 1, color);
    present_vline(x, y, y + h - 1, color);
    present_vline(x + w - 1, y, y + h - 1, color);
}

static const unsigned char *present_glyph(char ch)
{
    static const unsigned char A[7] = { 14,17,17,31,17,17,17 };
    static const unsigned char B[7] = { 30,17,17,30,17,17,30 };
    static const unsigned char C[7] = { 14,17,16,16,16,17,14 };
    static const unsigned char D[7] = { 30,17,17,17,17,17,30 };
    static const unsigned char E[7] = { 31,16,16,30,16,16,31 };
    static const unsigned char K[7] = { 17,18,20,24,20,18,17 };
    static const unsigned char N[7] = { 17,25,21,19,17,17,17 };
    static const unsigned char O[7] = { 14,17,17,17,17,17,14 };
    static const unsigned char P[7] = { 30,17,17,30,16,16,16 };
    static const unsigned char R[7] = { 30,17,17,30,20,18,17 };
    static const unsigned char S[7] = { 15,16,16,14,1,1,30 };
    static const unsigned char T[7] = { 31,4,4,4,4,4,4 };
    static const unsigned char U[7] = { 17,17,17,17,17,17,14 };
    static const unsigned char blank[7] = { 0,0,0,0,0,0,0 };

    switch (ch)
    {
        case 'A': return A;
        case 'B': return B;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'K': return K;
        case 'N': return N;
        case 'O': return O;
        case 'P': return P;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
    }
    return blank;
}

static void present_draw_char(int x, int y, char ch, int scale,
                              unsigned char color)
{
    const unsigned char *glyph;
    int row;
    int col;

    glyph = present_glyph(ch);
    for (row = 0; row < 7; row++)
        for (col = 0; col < 5; col++)
            if (glyph[row] & (1 << (4 - col)))
                present_fill_rect(x + col * scale, y + row * scale,
                                  scale, scale, color);
}

static void present_draw_text(int center_x, int y, const char *text,
                              int scale, int spacing,
                              unsigned char color)
{
    int len;
    int total_w;
    int x;
    int i;

    len = (int)strlen(text);
    total_w = len * (5 * scale + spacing) - spacing;
    x = center_x - total_w / 2;
    for (i = 0; i < len; i++)
    {
        present_draw_char(x, y, text[i], scale, color);
        x += 5 * scale + spacing;
    }
}

static void present_diamond(int cx, int cy, unsigned char color)
{
    int d;
    for (d = 0; d <= 4; d++)
    {
        present_put_pixel(cx - d, cy - 4 + d, color);
        present_put_pixel(cx + d, cy - 4 + d, color);
        present_put_pixel(cx - d, cy + 4 - d, color);
        present_put_pixel(cx + d, cy + 4 - d, color);
    }
}

static void score_show_presents_once(void)
{
    if (score_presented) return;
    score_presented = 1;

    score_set_mode(0x0013);
    present_set_palette();
    present_fill_rect(0, 0, PRESENT_W, PRESENT_H, 0);
    present_frame(22, 28, 276, 144, 1);
    present_frame(26, 32, 268, 136, 2);
    present_hline(48, 272, 58, 1);
    present_hline(72, 248, 60, 2);
    present_diamond(46, 59, 3);
    present_diamond(274, 59, 3);
    present_draw_text(160, 74, "BUDOSTACK", 4, 4, 3);
    present_hline(84, 236, 119, 1);
    present_draw_text(160, 130, "PRESENTS", 2, 3, 2);
    present_hline(84, 236, 150, 1);
    present_put_pixel(30, 36, 3);
    present_put_pixel(289, 36, 3);
    present_put_pixel(30, 163, 3);
    present_put_pixel(289, 163, 3);
    sound_presents();
    delay(2500);
    speaker_stop();
}

static void score_trim_name(char *name)
{
    int i;
    int len;

    len = (int)strlen(name);
    while (len > 0 &&
           (name[len - 1] == '\n' || name[len - 1] == '\r' ||
            name[len - 1] == ' ' || name[len - 1] == '\t'))
    {
        name[len - 1] = '\0';
        len--;
    }
    i = 0;
    while (name[i] == ' ' || name[i] == '\t') i++;
    if (i > 0) memmove(name, name + i, strlen(name + i) + 1);
    for (i = 0; name[i] != '\0'; i++)
        if (name[i] == '\t') name[i] = ' ';
    if (name[0] == '\0')
    {
        strncpy(name, lang_get("SCORE_ANON"), SCORE_NAME_LEN - 1);
        name[SCORE_NAME_LEN - 1] = '\0';
    }
}

static int score_load(ScoreEntry *entries)
{
    FILE *file;
    char line[96];
    char name[SCORE_NAME_LEN];
    long score;
    int count;

    count = 0;
    file = fopen(SCORE_FILE, "r");
    if (file == NULL) return 0;
    while (count < SCORE_TOP_COUNT && fgets(line, sizeof(line), file) != NULL)
    {
        name[0] = '\0';
        if (sscanf(line, "%ld\t%20[^\r\n]", &score, name) == 2)
        {
            score_trim_name(name);
            strcpy(entries[count].name, name);
            entries[count].score = score;
            count++;
        }
    }
    fclose(file);
    return count;
}

static void score_sort(ScoreEntry *entries, int count)
{
    ScoreEntry temp;
    int i;
    int j;

    for (i = 0; i < count - 1; i++)
        for (j = i + 1; j < count; j++)
            if (entries[j].score > entries[i].score)
            {
                temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
}

static void score_save(const ScoreEntry *entries, int count)
{
    FILE *file;
    int i;

    file = fopen(SCORE_FILE, "w");
    if (file == NULL) return;
    for (i = 0; i < count; i++)
        fprintf(file, "%ld\t%s\n", entries[i].score, entries[i].name);
    fclose(file);
}

static void score_read_name(char *name)
{
    int key;
    int len;

    len = 0;
    name[0] = '\0';
    fputs(lang_get("SCORE_NAME"), stdout);
    fflush(stdout);

    while (1)
    {
        key = getch();
        if (key == 13)
        {
            putchar('\n');
            break;
        }
        if (key == 8)
        {
            if (len > 0)
            {
                len--;
                name[len] = '\0';
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }
        if (key == 0 || key == 0xE0)
        {
            getch();
            continue;
        }
        if (key >= 32 && key <= 126 && len < SCORE_NAME_LEN - 1)
        {
            name[len++] = (char)key;
            name[len] = '\0';
            putchar(key);
            fflush(stdout);
        }
    }
    score_trim_name(name);
}

static void scoreboard_end_run(long score, long seconds,
                               int gold, int hero_level,
                               const char *weapon, const char *armor,
                               int victory)
{
    ScoreEntry entries[SCORE_TOP_COUNT + 1];
    char player_name[SCORE_NAME_LEN];
    int count;
    int i;

    score_set_mode(0x0003);
    printf("%s\n", lang_get("END_TITLE"));
    printf("============\n\n");
    printf("%s\n\n", lang_get(victory ? "END_VICTORY" : "END_DEFEAT"));
    printf(lang_get("END_TIME"), seconds); printf("\n");
    printf(lang_get("END_GOLD"), gold); printf("\n");
    printf(lang_get("END_LEVEL"), hero_level); printf("\n");
    printf(lang_get("END_WEAPON"), weapon); printf("\n");
    printf(lang_get("END_ARMOR"), armor); printf("\n");
    printf(lang_get("END_SCORE"), score); printf("\n\n");
    printf("%s\n\n", lang_get("END_FORMULA"));

    score_read_name(player_name);
    count = score_load(entries);
    strcpy(entries[count].name, player_name);
    entries[count].score = score;
    count++;
    score_sort(entries, count);
    if (count > SCORE_TOP_COUNT) count = SCORE_TOP_COUNT;
    score_save(entries, count);

    score_set_mode(0x0003);
    printf("%s\n", lang_get("SCORE_TITLE"));
    printf("=====================\n\n");
    printf("%s\n", lang_get("SCORE_HEADER"));
    printf("%s\n", lang_get("SCORE_RULE"));
    for (i = 0; i < count; i++)
        printf("%2d  %-20s  %10ld\n",
               i + 1, entries[i].name, entries[i].score);

    printf("\n");
    printf(lang_get("SCORE_YOURS"), score, player_name);
    printf("\n\n%s\n", lang_get("SCORE_EXIT"));
    printf("%s\n", lang_get("SCORE_REMAIN"));
    fflush(stdout);

    getch();
    speaker_stop();
    exit(0);
}

#endif
