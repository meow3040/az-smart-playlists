/* SPDX-License-Identifier: GPL-3.0-only
 * Included after the shared page helpers. All functions here run on the UI thread. */
enum { S_SLOT,
       S_SHOW,
       S_LOW,
       S_HIGH,
       S_GENRE,
       S_KEY,
       S_LENGTH,
       S_SORT,
       S_ARTIST,
       S_RESET,
       S_COUNT };
static const char *const length_names[] = { "Any", "Under 3 minutes", "3 to 6 minutes", "6 minutes or more" };
static const char *const sort_names[] = { "BPM ascending", "BPM descending", "Title", "Artist" };

static int bounded(int n, int max) { return n < 0 ? 0 : n > max ? max
                                                                : n; }

static int setting_get(int slot, const char *name, int def, int max)
{
    char key[32];
    snprintf(key, sizeof key, "smart%d_%s", slot + 1, name);
    return bounded(C->cfg_get(MOD_ID, key, def), max);
}

static void setting_set(int slot, const char *name, int value)
{
    char key[32];
    snprintf(key, sizeof key, "smart%d_%s", slot + 1, name);
    C->cfg_set(MOD_ID, key, value);
}

static void default_rule(int slot)
{
    rules[slot] = (struct smart_rule){ 0 };
    if (slot == 0) rules[slot].high = 109;
    else if (slot == 1) {
        rules[slot].low = 120;
        rules[slot].high = 129;
    } else rules[slot].low = 140;
}

static void save_rule(void)
{
    const struct smart_rule *r = &rules[smart_slot];
    setting_set(smart_slot, "low", r->low);
    setting_set(smart_slot, "high", r->high);
    setting_set(smart_slot, "key", r->key);
    setting_set(smart_slot, "length", r->length);
    setting_set(smart_slot, "sort", r->sort);
    setting_set(smart_slot, "artist", r->artist_deck);
    char key[32];
    snprintf(key, sizeof key, "smart%d_genre", smart_slot + 1);
    C->cfg_set_str(MOD_ID, key, r->genre);
}

static void load_rules(void)
{
    for (int i = 0; i < SMART_SLOTS; i++) {
        default_rule(i);
        struct smart_rule *r = &rules[i];
        r->low = setting_get(i, "low", r->low, 999);
        r->high = setting_get(i, "high", r->high, 999);
        r->key = setting_get(i, "key", 0, 24);
        r->length = setting_get(i, "length", 0, 3);
        r->sort = setting_get(i, "sort", 0, 3);
        r->artist_deck = setting_get(i, "artist", 0, 4);
        char key[32];
        snprintf(key, sizeof key, "smart%d_genre", i + 1);
        C->cfg_get_str(MOD_ID, key, r->genre, sizeof r->genre, "");
        r->genre[sizeof r->genre - 1] = 0;
    }
}

static void cancel_tracks(void)
{
    pthread_mutex_lock(&g_mu);
    g_generation++;
    g_request.pending = 0;
    pthread_mutex_unlock(&g_mu);
}

static void queue_tracks(struct track_request req)
{
    pthread_mutex_lock(&g_mu);
    req.generation = ++g_generation;
    req.pending = 1;
    g_request = req;
    g_tracks_ready = 0;
    pthread_mutex_unlock(&g_mu);
    showing_tracks = 1;
    tsel_top = 0;
}

static void show_smart_tracks(void)
{
    struct track_request req = { .stick = smart_stick, .smart = 1, .rule = rules[smart_slot] };
    smart_error[0] = 0;
    if (req.rule.low && req.rule.high && req.rule.low > req.rule.high) {
        snprintf(smart_error, sizeof smart_error, "Minimum BPM must not exceed maximum BPM.");
        return;
    }
    if (req.rule.artist_deck) {
        int deck = req.rule.artist_deck - 1;
        az_deck_info info = { .size = sizeof info };
        if (!AZ_HAS(C, deck_info) || deck >= C->deck_count() ||
            C->deck_info(deck, &info) != 0 || !info.loaded || !info.artist[0]) {
            snprintf(smart_error, sizeof smart_error, "Artist unavailable on deck %d. Choose Any or another deck.", deck + 1);
            return;
        }
        snprintf(req.artist, sizeof req.artist, "%s", info.artist);
    }
    snprintf(req.name, sizeof req.name, "Smart %d / USB %d", smart_slot + 1, smart_stick + 1);
    queue_tracks(req);
}

/* Genre choices come from the current USB listing; the reader resolves the name again
 * for each search, so swapping a USB cannot silently reuse an old playlist id. */
static void change_genre(int dir)
{
    struct smart_rule *r = &rules[smart_slot];
    int choices[MAX_ROWS], n = 0, current = -1;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_nrows; i++) {
        const struct row *row = &g_rows[i];
        const char *name = row->stick == smart_stick && row->id > 0 && !row->folder ? genre_name(row->name) : NULL;
        if (!name) continue;
        if (!text_compare(name, r->genre)) current = n;
        choices[n++] = i;
    }
    int next = current + dir;
    if (next < -1) next = n - 1;
    if (next >= n) next = -1;
    snprintf(r->genre, sizeof r->genre, "%s", next < 0 ? "" : genre_name(g_rows[choices[next]].name));
    pthread_mutex_unlock(&g_mu);
    if (!n) snprintf(smart_error, sizeof smart_error, "No genres. Export playlists named Genre: House, Genre: Techno, ...");
}

static void adjust_smart(int dir)
{
    struct smart_rule *r = &rules[smart_slot];
    smart_error[0] = 0;
    switch (smart_sel) {
    case S_SLOT: smart_slot = (smart_slot + SMART_SLOTS + dir) % SMART_SLOTS; break;
    case S_LOW: r->low = bounded(r->low + dir, 999); break;
    case S_HIGH: r->high = bounded(r->high + dir, 999); break;
    case S_GENRE: change_genre(dir); break;
    case S_KEY: r->key = (r->key + 25 + dir) % 25; break;
    case S_LENGTH: r->length = (r->length + 4 + dir) % 4; break;
    case S_SORT: r->sort = (r->sort + 4 + dir) % 4; break;
    case S_ARTIST: r->artist_deck = (r->artist_deck + 5 + dir) % 5; break;
    }
    if (smart_sel != S_SLOT) save_rule();
}

static void draw_smart(az_draw *d, az_rect a, int row, int px)
{
    const struct smart_rule *r = &rules[smart_slot];
    const int pad = row / 4, lines = (a.h - 2 * row) / row;
    status(d, a, row, px, smart_error[0] ? smart_error : smart_edit ? "Turn to change; click or BACK to finish. Saved automatically."
                                                                    : "Click to edit. Genre uses Genre: playlists. Results are view-only.");
    if (smart_sel < smart_top) smart_top = smart_sel;
    if (lines > 0 && smart_sel >= smart_top + lines) smart_top = smart_sel - lines + 1;
    for (int i = 0; i < lines && smart_top + i < S_COUNT; i++) {
        int index = smart_top + i, on = index == smart_sel;
        char s[192] = "", f[192];
        switch (index) {
        case S_SLOT: snprintf(s, sizeof s, "Playlist: Smart %d / USB %d", smart_slot + 1, smart_stick + 1); break;
        case S_SHOW: snprintf(s, sizeof s, "Show matching tracks >"); break;
        case S_LOW:
        case S_HIGH: {
            int bpm = index == S_LOW ? r->low : r->high;
            char val[24] = "Any";
            if (bpm) snprintf(val, sizeof val, "%d", bpm);
            snprintf(s, sizeof s, "BPM %s: %s", index == S_LOW ? "minimum" : "maximum", val);
            break;
        }
        case S_GENRE: snprintf(s, sizeof s, "Genre: %s", r->genre[0] ? r->genre : "Any"); break;
        case S_KEY:
            if (r->key) snprintf(s, sizeof s, "Key: %d%c", (r->key - 1) % 12 + 1, r->key <= 12 ? 'A' : 'B');
            else snprintf(s, sizeof s, "Key: Any");
            break;
        case S_LENGTH: snprintf(s, sizeof s, "Length: %s", length_names[r->length]); break;
        case S_SORT: snprintf(s, sizeof s, "Sort: %s", sort_names[r->sort]); break;
        case S_ARTIST:
            if (r->artist_deck) snprintf(s, sizeof s, "Artist: same as deck %d", r->artist_deck);
            else snprintf(s, sizeof s, "Artist: Any");
            break;
        case S_RESET: snprintf(s, sizeof s, "Reset this playlist's filters"); break;
        }
        int y = a.y + row * (i + 2);
        if (on) C->fill_rect(d, a.x, y, a.w, row, C->color(AZ_COL_HI));
        fit(f, sizeof f, s, px, a.w - 2 * pad);
        if (f[0]) C->draw_text(d, a.x + pad, y + (row - px * 3 / 2) / 2, f, px,
                               C->color(on ? AZ_COL_HI_TEXT : AZ_COL_TEXT));
    }
}

static int smart_input(const az_event *ev)
{
    switch (ev->kind) {
    case AZ_EV_KNOB:
        if (smart_edit) adjust_smart(ev->dir < 0 ? -1 : 1);
        else smart_sel = bounded(smart_sel + (ev->dir < 0 ? -1 : 1), S_COUNT - 1);
        break;
    case AZ_EV_CLICK:
        if (smart_sel == S_SHOW) show_smart_tracks();
        else if (smart_sel == S_RESET) {
            default_rule(smart_slot);
            save_rule();
            smart_error[0] = 0;
        } else smart_edit = !smart_edit;
        break;
    case AZ_EV_BACK:
        if (smart_edit) smart_edit = 0;
        else showing_smart = 0;
        break;
    default: return 0;
    }
    C->request_redraw();
    return 1;
}
