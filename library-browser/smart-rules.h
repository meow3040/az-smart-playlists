/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SMART_RULES_H
#define SMART_RULES_H
#include <ctype.h>
#include <math.h>

#define SMART_SLOTS 3
#define SMART_LIMIT 20000

struct smart_rule {
    int low, high, key, length, sort, artist_deck;
    char genre[96];
};

static int text_compare(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int d = tolower((unsigned char)*a++) - tolower((unsigned char)*b++);
        if (d) return d;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* 1..12 = Camelot A, 13..24 = B. rekordbox may report traditional notation. */
static int key_number(const char *key)
{
    static const char *const minor[] = { "Abm", "Ebm", "Bbm", "Fm", "Cm", "Gm", "Dm", "Am", "Em", "Bm", "F#m", "C#m" };
    static const char *const major[] = { "B", "F#", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E" };
    static const char *const alt_minor[] = { "G#m", "D#m", "A#m", "", "", "", "", "", "", "", "Gbm", "Dbm" };
    static const char *const alt_major[] = { "Cb", "Gb", "C#", "G#", "D#", "A#", "", "", "", "", "", "" };
    if (!key || !*key) return 0;
    for (int i = 0; i < 24; i++) {
        char camelot[8];
        snprintf(camelot, sizeof camelot, "%d%c", i % 12 + 1, i < 12 ? 'A' : 'B');
        if (!text_compare(key, camelot)) return i + 1;
        if (!strcmp(key, i < 12 ? minor[i] : major[i - 12]) ||
            !strcmp(key, i < 12 ? alt_minor[i] : alt_major[i - 12])) return i + 1;
    }
    return 0;
}

static int smart_matches(const az_track *t, const struct smart_rule *r, const char *artist)
{
    if ((r->low || r->high) && (!isfinite(t->bpm) || t->bpm <= 0)) return 0;
    if (r->low && t->bpm < r->low) return 0;
    if (r->high && t->bpm > r->high) return 0;
    if (r->key && key_number(t->key) != r->key) return 0;
    if (r->length && (!isfinite(t->len_s) || t->len_s <= 0)) return 0;
    if (r->length == 1 && t->len_s >= 180) return 0;
    if (r->length == 2 && (t->len_s < 180 || t->len_s >= 360)) return 0;
    if (r->length == 3 && t->len_s < 360) return 0;
    if (r->artist_deck && (!artist || !*artist || text_compare(t->artist, artist))) return 0;
    return 1;
}

/* Genre is explicit playlist membership, never inferred from BPM or a track title. */
static const char *genre_name(const char *name)
{
    if (!name) return NULL;
    const char *prefix = "genre:";
    for (int i = 0; prefix[i]; i++)
        if (!name[i] || tolower((unsigned char)name[i]) != prefix[i]) return NULL;
    name += 6;
    while (*name == ' ') name++;
    return *name ? name : NULL;
}
#endif
