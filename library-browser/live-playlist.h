/* SPDX-License-Identifier: GPL-3.0-only
 * A live, read-only overlay on BROWSE (including Playlist and Tag List).
 * The reader owns the immutable library cache; the UI sees it only under live_mu.
 * Cached tracks are filtered again when tempo changes, without another USB read. */
#define LIVE_CAP (NSTICKS * SMART_LIMIT)
#define LIVE_ROW 48
static const az_rect LIVE_BADGE = { 974, 80, 294, 48 };
static const az_rect LIVE_PANEL = { 40, 80, 1200, 656 };

struct live_track {
    struct track_row row;
    uint32_t id;
    int stick, complete;
};
struct live_request {
    unsigned version;
    int active, rescan, reference, tolerance, key_filter;
    az_deck_info decks[4];
    char notice[128];
};
struct live_result {
    struct live_track *tracks;
    int *order;
    int n, scanned, limited, ports, reference_key, reference;
    double bpm;
    char title[128], notice[128];
};
static pthread_mutex_t live_mu = PTHREAD_MUTEX_INITIALIZER;
static struct live_request live_request;
static struct live_result live_result;
static unsigned live_version;
static int live_pending, live_need_scan = 1, live_ready;
static atomic_int live_fresh;

/* UI thread only. */
static az_surface *live_surface;
static int live_open, live_top, live_deck_choice, live_tolerance = 4, live_key_filter = 1;
static int64_t live_poll_at, live_scan_at;
static struct live_request live_seen;
static int live_have_seen;

static int64_t live_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int live_valid_bpm(double bpm) { return isfinite(bpm) && bpm > 0 && bpm < 1000; }

static int live_keys_match(int a, int b)
{
    if (a < 1 || a > 24 || b < 1 || b > 24) return 0;
    int an = (a - 1) % 12, bn = (b - 1) % 12;
    if (an == bn) return 1; /* same key or relative major/minor */
    return (a <= 12) == (b <= 12) && ((an + 1) % 12 == bn || (bn + 1) % 12 == an);
}

/* Content ids are local to a USB; no source-port field is available in deck_info.
 * Never match id alone. Truncated metadata cannot establish track identity either. */
static int live_same_track(const struct live_track *t, const az_deck_info *d)
{
    if (!t->complete || !d->title[0] || !d->artist[0] ||
        strlen(d->title) >= NAME_MAX_B || strlen(d->artist) >= NAME_MAX_B) return 0;
    return !text_compare(t->row.title, d->title) && !text_compare(t->row.artist, d->artist) &&
           (!d->content_id || d->content_id == t->id);
}

static int live_reference_key(const struct live_track *tracks, int n, const az_deck_info *d)
{
    int key = key_number(d->key);
    if (key) return key;
    for (int i = 0; i < n; i++) {
        if (!live_same_track(&tracks[i], d)) continue;
        int candidate = key_number(tracks[i].row.key);
        if (!candidate || (key && key != candidate)) return 0;
        key = candidate;
    }
    return key;
}

static int live_matches(const struct live_track *t, const struct live_request *r, int key)
{
    if (r->reference < 0 || !live_valid_bpm(t->row.bpm)) return 0;
    double delta = t->row.bpm - r->decks[r->reference].bpm;
    if (!live_valid_bpm(r->decks[r->reference].bpm) || delta < -r->tolerance || delta > r->tolerance) return 0;
    if (r->key_filter && key && !live_keys_match(key, key_number(t->row.key))) return 0;
    for (int i = 0; i < 4; i++)
        if (r->decks[i].loaded == 1 && r->decks[i].playing == 1 && live_same_track(t, &r->decks[i])) return 0;
    return 1;
}

static struct live_request live_capture(void)
{
    struct live_request r = { .active = 1, .reference = -1, .tolerance = live_tolerance, .key_filter = live_key_filter };
    int playing = 0;
    /* deck_count describes the current view, not all playing decks. Query all four. */
    for (int i = 0; i < 4; i++) {
        az_deck_info *d = &r.decks[i];
        d->size = sizeof *d;
        if (C->deck_info(i, d)) {
            memset(d, 0, sizeof *d);
            continue;
        }
        d->title[sizeof d->title - 1] = d->artist[sizeof d->artist - 1] = d->key[sizeof d->key - 1] = 0;
        d->bpm = live_valid_bpm(d->bpm) ? (double)(int)(d->bpm * 10 + 0.5) / 10 : 0;
        if (d->loaded == 1 && d->playing == 1) {
            playing++;
            r.reference = i;
        }
    }
    if (live_deck_choice) {
        int deck = live_deck_choice - 1;
        r.reference = r.decks[deck].loaded == 1 && r.decks[deck].playing == 1 ? deck : -1;
        if (r.reference < 0) snprintf(r.notice, sizeof r.notice, "Deck %d is not playing. Choose another reference.", deck + 1);
    } else if (playing != 1) {
        r.reference = -1;
        snprintf(r.notice, sizeof r.notice, "%s", playing ? "Several decks are playing. Tap Reference to choose one." : "Start a track to see live matches.");
    }
    if (r.reference >= 0 && !live_valid_bpm(r.decks[r.reference].bpm))
        snprintf(r.notice, sizeof r.notice, "The reference track has no known BPM.");
    return r;
}

static int live_track_changed(const struct live_request *a, const struct live_request *b)
{
    if (a->reference != b->reference) return 1;
    for (int i = 0; i < 4; i++)
        if (a->decks[i].content_id != b->decks[i].content_id ||
            strcmp(a->decks[i].title, b->decks[i].title) || strcmp(a->decks[i].artist, b->decks[i].artist)) return 1;
    return 0;
}

static int live_request_changed(const struct live_request *a, const struct live_request *b)
{
    if (live_track_changed(a, b) || a->tolerance != b->tolerance || a->key_filter != b->key_filter || strcmp(a->notice, b->notice)) return 1;
    for (int i = 0; i < 4; i++)
        if (a->decks[i].loaded != b->decks[i].loaded || a->decks[i].playing != b->decks[i].playing ||
            a->decks[i].bpm != b->decks[i].bpm || strcmp(a->decks[i].key, b->decks[i].key)) return 1;
    return 0;
}

static void live_submit(int force_scan)
{
    struct live_request req = live_capture();
    if (!force_scan && live_have_seen && !live_request_changed(&req, &live_seen)) return;
    pthread_mutex_lock(&live_mu);
    live_need_scan |= force_scan || !live_have_seen || live_track_changed(&req, &live_seen);
    req.version = ++live_version;
    live_request = req;
    live_pending = 1;
    live_ready = 0;
    pthread_mutex_unlock(&live_mu);
    live_seen = req;
    live_have_seen = 1;
    if (force_scan) live_scan_at = live_ms();
    live_top = 0;
    C->surface_redraw(live_surface);
}

struct live_scan {
    struct live_track *tracks;
    int n, stick, limited;
};
static int live_on_track(const az_track *t, void *user)
{
    struct live_scan *s = user;
    if (s->n == LIVE_CAP) {
        s->limited = 1;
        return 1;
    }
    struct live_track *out = &s->tracks[s->n++];
    out->id = t->id;
    out->stick = s->stick;
    out->row.bpm = t->bpm;
    snprintf(out->row.title, sizeof out->row.title, "%s", t->title ? t->title : "");
    snprintf(out->row.artist, sizeof out->row.artist, "%s", t->artist ? t->artist : "");
    snprintf(out->row.key, sizeof out->row.key, "%s", t->key ? t->key : "");
    out->complete = t->title && t->artist && strlen(t->title) < sizeof out->row.title && strlen(t->artist) < sizeof out->row.artist;
    return 0;
}

/* Reader-only comparator state. The UI never uses it. */
static const struct live_track *live_sort_tracks;
static double live_sort_bpm;
static int live_compare(const void *a, const void *b)
{
    const struct live_track *ta = &live_sort_tracks[*(const int *)a], *tb = &live_sort_tracks[*(const int *)b];
    double da = fabs(ta->row.bpm - live_sort_bpm), db = fabs(tb->row.bpm - live_sort_bpm);
    int result = (da > db) - (da < db);
    if (!result) result = text_compare(ta->row.title, tb->row.title);
    if (!result) result = text_compare(ta->row.artist, tb->row.artist);
    if (!result) result = ta->stick - tb->stick;
    if (!result) result = (ta->id > tb->id) - (ta->id < tb->id);
    return result;
}

static void live_read_pending(void)
{
    static struct live_track *cache;
    static int cache_n, cache_ports, cache_limited;
    pthread_mutex_lock(&live_mu);
    if (!live_pending) {
        pthread_mutex_unlock(&live_mu);
        return;
    }
    struct live_request req = live_request;
    req.rescan = live_need_scan || !cache || !cache_ports;
    live_pending = 0;
    pthread_mutex_unlock(&live_mu);

    struct live_result result = { .tracks = cache, .scanned = cache_n, .ports = cache_ports, .limited = cache_limited, .reference = req.reference };
    snprintf(result.notice, sizeof result.notice, "%s", req.notice);
    if (req.active && !req.notice[0] && req.reference >= 0) {
        const az_deck_info *d = &req.decks[req.reference];
        result.bpm = d->bpm;
        snprintf(result.title, sizeof result.title, "%s", d->title);
        if (req.rescan) {
            result.tracks = calloc(LIVE_CAP, sizeof *result.tracks);
            result.scanned = result.ports = result.limited = 0;
            if (!result.tracks) snprintf(result.notice, sizeof result.notice, "Not enough memory. Close and try again.");
            else {
                struct live_scan s = { .tracks = result.tracks };
                for (s.stick = 0; s.stick < NSTICKS; s.stick++) {
                    int before = s.n;
                    int rc = C->lib_tracks(MOD_ID, s.stick, 0, live_on_track, &s);
                    if (rc < 0) s.n = before;
                    else {
                        result.ports |= 1 << s.stick;
                        if (rc > 0) s.limited = 1;
                    }
                }
                result.scanned = s.n;
                result.limited = s.limited;
                if (!result.ports) snprintf(result.notice, sizeof result.notice, "No readable rekordbox USB library. Insert a USB and tap Rescan.");
            }
        }
        if (!result.notice[0]) {
            result.reference_key = live_reference_key(result.tracks, result.scanned, d);
            result.order = malloc(sizeof *result.order * (size_t)(result.scanned ? result.scanned : 1));
            if (!result.order) snprintf(result.notice, sizeof result.notice, "Not enough memory. Close and try again.");
            else {
                for (int i = 0; i < result.scanned; i++)
                    if (live_matches(&result.tracks[i], &req, result.reference_key)) result.order[result.n++] = i;
                live_sort_tracks = result.tracks;
                live_sort_bpm = d->bpm;
                qsort(result.order, (size_t)result.n, sizeof *result.order, live_compare);
            }
        }
    }
    if (!req.active) {
        result.tracks = NULL;
        result.scanned = result.ports = result.limited = 0;
    }
    pthread_mutex_lock(&live_mu);
    if (req.version != live_version) {
        pthread_mutex_unlock(&live_mu);
        if (result.tracks != cache) free(result.tracks);
        free(result.order);
        return;
    }
    struct live_track *old_cache = cache;
    int *old_order = live_result.order;
    live_result = result;
    cache = result.tracks;
    cache_n = result.scanned;
    cache_ports = result.ports;
    cache_limited = result.limited;
    if (req.rescan && req.active && !req.notice[0] && result.tracks && result.ports) live_need_scan = 0;
    live_ready = 1;
    pthread_mutex_unlock(&live_mu);
    if (old_cache != cache) free(old_cache);
    free(old_order);
    atomic_store(&live_fresh, 1);
}

static void live_close(void)
{
    live_open = 0;
    live_have_seen = 0;
    C->surface_focus(live_surface, 0);
    C->surface_place(live_surface, LIVE_BADGE);
    pthread_mutex_lock(&live_mu);
    live_request = (struct live_request){ .version = ++live_version };
    live_pending = live_need_scan = 1;
    live_ready = 0;
    pthread_mutex_unlock(&live_mu);
    C->surface_redraw(live_surface);
}

static void live_text(az_draw *d, int x, int y, int width, const char *text, int px, int role)
{
    char clipped[256];
    fit(clipped, sizeof clipped, text, px, width);
    if (clipped[0]) C->draw_text(d, x, y, clipped, px, C->color(role));
}

static void live_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    if (!live_open) {
        C->fill_rect(d, a.x, a.y, a.w, a.h, C->color(AZ_COL_HI));
        if (a.w >= 24 && a.h >= 42) live_text(d, a.x + 12, a.y + 6, a.w - 24, "SMART PLAYLIST", 24, AZ_COL_HI_TEXT);
        return;
    }
    if (a.w < 400 || a.h < 240) return;
    live_text(d, a.x + 16, a.y + 10, a.w - 160, "SMART PLAYLIST", 26, AZ_COL_TEXT);
    live_text(d, a.x + a.w - 120, a.y + 10, 104, "BACK / X", 20, AZ_COL_TEXT_DIM);
    char controls[4][64];
    if (live_deck_choice) snprintf(controls[0], sizeof controls[0], "Reference: Deck %d", live_deck_choice);
    else snprintf(controls[0], sizeof controls[0], "Reference: Auto");
    snprintf(controls[1], sizeof controls[1], "BPM: +/- %d", live_tolerance);
    snprintf(controls[2], sizeof controls[2], "Key: %s", live_key_filter ? "Compatible" : "Any");
    snprintf(controls[3], sizeof controls[3], "Rescan USBs");
    int button_w = (a.w - 32) / 4;
    for (int i = 0; i < 4; i++) {
        int x = a.x + 16 + i * button_w;
        C->fill_rect(d, x, a.y + 52, button_w - 8, 40, C->color(AZ_COL_HI));
        live_text(d, x + 8, a.y + 58, button_w - 24, controls[i], 18, AZ_COL_HI_TEXT);
    }
    pthread_mutex_lock(&live_mu);
    const struct live_result *r = &live_result;
    if (!live_ready || r->notice[0]) {
        live_text(d, a.x + 16, a.y + 108, a.w - 32, live_ready ? r->notice : "Finding related tracks...", 22, AZ_COL_TEXT_DIM);
    } else {
        char line[256], key[16] = "";
        if (r->reference_key) snprintf(key, sizeof key, "%d%c", (r->reference_key - 1) % 12 + 1, r->reference_key <= 12 ? 'A' : 'B');
        snprintf(line, sizeof line, "Deck %d: %s | %.1f BPM%s%s", r->reference + 1, r->title, r->bpm, key[0] ? " | " : "", key);
        live_text(d, a.x + 16, a.y + 104, a.w - 32, line, 20, AZ_COL_TEXT);
        snprintf(line, sizeof line, "%d matches / %d scanned%s%s", r->n, r->scanned,
                 r->limited ? " [SCAN LIMITED]" : "", live_key_filter && !r->reference_key ? " | BPM only: reference key unknown" : "");
        live_text(d, a.x + 16, a.y + 136, a.w - 32, line, 18, AZ_COL_TEXT_DIM);
        int lines = (a.h - 204) / LIVE_ROW;
        if (live_top > r->n - lines) live_top = r->n - lines;
        if (live_top < 0) live_top = 0;
        if (!r->n) live_text(d, a.x + 16, a.y + 184, a.w - 32, "No matches. Widen BPM or set Key to Any.", 22, AZ_COL_TEXT_DIM);
        for (int i = 0; i < lines && live_top + i < r->n; i++) {
            const struct live_track *t = &r->tracks[r->order[live_top + i]];
            int y = a.y + 180 + i * LIVE_ROW;
            char details[64];
            snprintf(details, sizeof details, "USB %d | %.1f | %s", t->stick + 1, t->row.bpm, t->row.key);
            snprintf(line, sizeof line, "%s%s%s", t->row.title, t->row.artist[0] ? " - " : "", t->row.artist);
            live_text(d, a.x + 16, y, a.w - 330, line, 20, AZ_COL_TEXT);
            live_text(d, a.x + a.w - 298, y, 282, details, 18, AZ_COL_TEXT_DIM);
        }
    }
    pthread_mutex_unlock(&live_mu);
    live_text(d, a.x + 16, a.y + a.h - 32, a.w - 32, "LIVE | Turn knob to scroll | Load tracks in the native browser | BACK closes", 16, AZ_COL_TEXT_DIM);
}

static int live_input(const az_event *ev, void *user)
{
    (void)user;
    if (!live_open) {
        if (ev->kind != AZ_EV_TAP) return 0;
        if (!C->surface_focus(live_surface, 1)) return 1;
        live_open = 1;
        C->surface_place(live_surface, LIVE_PANEL);
        live_poll_at = live_ms();
        live_submit(1);
        return 1;
    }
    if (ev->kind == AZ_EV_BACK) {
        live_close();
        return 1;
    }
    if (ev->kind == AZ_EV_KNOB) {
        live_top = bounded(live_top + (ev->dir < 0 ? -1 : 1), LIVE_CAP - 1);
        C->surface_redraw(live_surface);
        return 1;
    }
    if (ev->kind == AZ_EV_CLICK) return 1; /* no unsupported native load action */
    if (ev->kind != AZ_EV_TAP) return 0;
    int x = ev->x - LIVE_PANEL.x, y = ev->y - LIVE_PANEL.y;
    if (y >= 0 && y < 48 && x >= LIVE_PANEL.w - 144) {
        live_close();
        return 1;
    }
    if (y >= 52 && y < 92 && x >= 16 && x < LIVE_PANEL.w - 16) {
        int button = (x - 16) / ((LIVE_PANEL.w - 32) / 4);
        if (button == 0) {
            live_deck_choice = (live_deck_choice + 1) % 5;
            C->cfg_set(MOD_ID, "live_deck", live_deck_choice);
        } else if (button == 1) {
            live_tolerance = live_tolerance == 2 ? 4 : live_tolerance == 4 ? 6
                                                   : live_tolerance == 6   ? 10
                                                                           : 2;
            C->cfg_set(MOD_ID, "live_bpm", live_tolerance);
        } else if (button == 2) {
            live_key_filter = !live_key_filter;
            C->cfg_set(MOD_ID, "live_key", live_key_filter);
        }
        live_submit(button == 3);
    }
    return 1;
}

static void live_tick(void)
{
    if (!live_surface) return;
    if (live_open && C->view() != AZ_VIEW_BROWSE) {
        live_close();
        return;
    }
    if (atomic_exchange(&live_fresh, 0)) C->surface_redraw(live_surface);
    if (!live_open) return;
    int64_t now = live_ms();
    if (now < live_poll_at || now - live_poll_at >= 1000) {
        live_poll_at = now;
        live_submit(now < live_scan_at || now - live_scan_at >= 30000);
    }
}

static int live_init(void)
{
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, surface_focus) || !AZ_HAS(C, view) || !AZ_HAS(C, deck_info)) return 1;
    live_deck_choice = bounded(C->cfg_get(MOD_ID, "live_deck", 0), 4);
    live_tolerance = C->cfg_get(MOD_ID, "live_bpm", 4);
    if (live_tolerance != 2 && live_tolerance != 4 && live_tolerance != 6 && live_tolerance != 10) live_tolerance = 4;
    live_key_filter = C->cfg_get(MOD_ID, "live_key", 1) != 0;
    static const az_surface_v1 desc = {
        .size = sizeof desc,
        .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_BROWSE),
        .on_paint = live_paint,
        .on_input = live_input,
    };
    live_surface = C->surface_create(MOD_ID, &desc);
    if (!live_surface) return 1;
    C->surface_place(live_surface, LIVE_BADGE);
    C->surface_show(live_surface, 1);
    return 0;
}
