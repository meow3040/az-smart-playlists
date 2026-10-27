/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise the real reader and UI with deterministic USB and settings services. */
#include <assert.h>
#include "../library-browser/library-browser.c"

static int checks;
#define CHECK(expr)                                                 \
    do {                                                            \
        checks++;                                                   \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            exit(1);                                                \
        }                                                           \
    } while (0)
static int fixture_mode, fixture_port, genre_id = 20, tracks_called, last_playlist;
static int fixture_deck_loaded = 1;
static int fixture_live, fixture_view = AZ_VIEW_BROWSE, fixture_focus, fixture_refuse_focus, fixture_cancel_live;
static az_rect fixture_surface_rect;
static az_deck_info fixture_live_decks[4];
static struct {
    char key[32], value[128];
} config[32];
static int config_n;

static int mock_get_str(const char *id, const char *key, char *out, int n, const char *def)
{
    (void)id;
    for (int i = 0; i < config_n; i++)
        if (!strcmp(config[i].key, key)) return snprintf(out, (size_t)n, "%s", config[i].value);
    return snprintf(out, (size_t)n, "%s", def);
}
static void mock_set_str(const char *id, const char *key, const char *value)
{
    (void)id;
    int i;
    for (i = 0; i < config_n; i++)
        if (!strcmp(config[i].key, key)) break;
    assert(i < 32);
    if (i == config_n) config_n++;
    snprintf(config[i].key, sizeof config[i].key, "%s", key);
    snprintf(config[i].value, sizeof config[i].value, "%s", value);
}
static int mock_get(const char *id, const char *key, int def)
{
    char val[32];
    mock_get_str(id, key, val, sizeof val, "");
    return val[0] ? atoi(val) : def;
}
static void mock_set(const char *id, const char *key, int val)
{
    char s[32];
    snprintf(s, sizeof s, "%d", val);
    mock_set_str(id, key, s);
}
static int mock_playlists(const char *id, int stick, int (*fn)(int, int, const char *, int, void *), void *user)
{
    (void)id;
    if ((stick != fixture_port && fixture_mode != 7) || fixture_mode == 1) return -1;
    if (fixture_mode == 7) {
        for (int i = 0; i < 600; i++)
            if (fn(i + 1, 0, "Playlist", 0, user)) return 0;
        return 0;
    }
    fn(5, 0, "Ordinary", 0, user);
    if (fixture_mode != 2) fn(genre_id, 0, "Genre: House", 0, user);
    if (fixture_mode == 3) fn(21, 0, "genre: House", 0, user);
    return fixture_mode == 4 ? 1 : 0;
}
static int mock_tracks(const char *id, int stick, int playlist, int (*fn)(const az_track *, void *), void *user)
{
    (void)id;
    tracks_called++;
    last_playlist = playlist;
    if ((stick != fixture_port && fixture_mode != 9) || fixture_mode == 1) return -1;
    for (int i = 0; i < (fixture_mode == 5 || fixture_mode == 8 ? 300 : 4); i++) {
        char title[64];
        snprintf(title, sizeof title, "Track %03d", i);
        az_track t = { .size = sizeof t, .id = (uint32_t)i + 1, .title = title, .artist = i % 2 ? "B" : "A", .key = i % 2 ? "9A" : "Am", .bpm = fixture_mode == 5 ? 400 - i : 120 + i, .len_s = 200 };
        if (fixture_mode == 8) t.bpm = 125;
        if (fn(&t, user)) break;
    }
    if (fixture_cancel_live) {
        fixture_cancel_live = 0;
        pthread_mutex_lock(&live_mu);
        live_request = (struct live_request){ .version = ++live_version };
        live_pending = 1;
        live_ready = 0;
        pthread_mutex_unlock(&live_mu);
    }
    return fixture_mode == 6 ? 1 : 0;
}
static void mock_redraw(void) {}
static int mock_deck_count(void) { return fixture_live ? 4 : 2; }
static int mock_deck(int deck, az_deck_info *out)
{
    if (fixture_live) {
        *out = fixture_live_decks[deck];
        return 0;
    }
    if (deck >= 2) return -1;
    out->loaded = fixture_deck_loaded;
    snprintf(out->artist, sizeof out->artist, "A");
    return 0;
}
static int mock_view(void) { return fixture_view; }
static az_surface *mock_surface(const char *id, const az_surface_v1 *s)
{
    (void)id;
    assert(s->views == AZ_VIEW_BIT(AZ_VIEW_BROWSE));
    return (az_surface *)(uintptr_t)1;
}
static void mock_place(az_surface *s, az_rect r)
{
    (void)s;
    fixture_surface_rect = r;
}
static void mock_show(az_surface *s, int on)
{
    (void)s;
    (void)on;
}
static void mock_surface_redraw(az_surface *s) { (void)s; }
static int mock_focus(az_surface *s, int on)
{
    (void)s;
    if (on && fixture_refuse_focus) return 0;
    fixture_focus = on;
    return 1;
}
static const az_core_v1 core = {
    .abi = AZ_MOD_ABI,
    .size = sizeof core,
    .cfg_get = mock_get,
    .cfg_set = mock_set,
    .cfg_get_str = mock_get_str,
    .cfg_set_str = mock_set_str,
    .lib_playlists = mock_playlists,
    .lib_tracks = mock_tracks,
    .request_redraw = mock_redraw,
    .deck_count = mock_deck_count,
    .deck_info = mock_deck,
    .view = mock_view,
    .surface_create = mock_surface,
    .surface_place = mock_place,
    .surface_show = mock_show,
    .surface_redraw = mock_surface_redraw,
    .surface_focus = mock_focus,
};

static void test_predicates(void)
{
    az_track t = { .size = sizeof t, .bpm = 125, .len_s = 180, .key = "Am", .artist = "Artist" };
    struct smart_rule r = { .low = 120, .high = 130, .key = 8, .length = 2, .artist_deck = 1 };
    CHECK(smart_matches(&t, &r, "artist"));
    CHECK(!smart_matches(&t, &r, "Someone else"));
    CHECK(!smart_matches(&t, &r, ""));
    t.bpm = 120;
    CHECK(smart_matches(&t, &r, "Artist"));
    t.bpm = 130;
    CHECK(smart_matches(&t, &r, "Artist"));
    t.bpm = 130.01;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.bpm = 119.99;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.bpm = NAN;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.bpm = INFINITY;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.bpm = -1;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.bpm = 125;
    t.len_s = 360;
    CHECK(!smart_matches(&t, &r, "Artist"));
    r.length = 3;
    CHECK(smart_matches(&t, &r, "Artist"));
    t.len_s = NAN;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.len_s = 179.9;
    r.length = 1;
    CHECK(smart_matches(&t, &r, "Artist"));
    t.len_s = 180;
    CHECK(!smart_matches(&t, &r, "Artist"));
    t.key = NULL;
    CHECK(!smart_matches(&t, &r, "Artist"));
    r = (struct smart_rule){ 0 };
    t.bpm = NAN;
    t.len_s = -1;
    CHECK(smart_matches(&t, &r, NULL));
    CHECK(key_number("8a") == 8 && key_number("C") == 20);
    CHECK(key_number("G#m") == 1 && key_number("Abm") == 1);
    CHECK(key_number("0A") == 0 && key_number("13B") == 0 && key_number("") == 0);
    CHECK(!genre_name("House") && !genre_name("Genre: "));
    CHECK(!strcmp(genre_name("gEnRe:  House"), "House"));
}

static void test_reader(void)
{
    struct track_request req = { .generation = ++g_generation, .smart = 1, .rule = { .low = 121, .high = 123, .key = 9 } };
    snprintf(req.name, sizeof req.name, "Smart test");
    read_tracks(&req);
    CHECK(g_tracks_ready && g_ntracks == 2 && g_tracks_total == 2);
    CHECK(!strcmp(g_tracks[0].title, "Track 001") && !strcmp(g_tracks[1].title, "Track 003"));
    CHECK(last_playlist == 0);
    req.rule.sort = 1;
    read_tracks(&req);
    CHECK(!strcmp(g_tracks[0].title, "Track 003"));
    snprintf(req.rule.genre, sizeof req.rule.genre, "House");
    read_tracks(&req);
    CHECK(last_playlist == 20 && g_ntracks == 2);
    genre_id = 30000000;
    read_tracks(&req);
    CHECK(last_playlist == 30000000); /* no 24-bit truncation */
    fixture_mode = 2;
    tracks_called = 0;
    read_tracks(&req);
    CHECK(g_ntracks == 0 && strstr(g_track_error, "missing") && !tracks_called);
    fixture_mode = 3;
    read_tracks(&req);
    CHECK(strstr(g_track_error, "Duplicate") && !tracks_called);
    fixture_mode = 4;
    read_tracks(&req);
    CHECK(strstr(g_track_error, "incomplete") && !tracks_called);
    fixture_mode = 1;
    read_tracks(&req);
    CHECK(strstr(g_track_error, "unavailable") && !tracks_called);
    fixture_mode = 5;
    req.rule = (struct smart_rule){ 0 };
    read_tracks(&req);
    CHECK(g_tracks_total == 300 && g_ntracks == 256);
    CHECK(g_tracks[0].bpm == 101 && g_tracks[255].bpm == 356); /* sort BEFORE display cap */
    fixture_mode = 6;
    read_tracks(&req);
    CHECK(g_tracks_cut && g_ntracks == 4 && !g_track_error[0]);
    fixture_mode = 0;
    req.rule.low = 900;
    read_tracks(&req);
    CHECK(!g_ntracks && !g_tracks_total && !g_track_error[0] && !g_tracks_cut);
    queue_tracks(req);
    struct track_request old = g_request;
    queue_tracks(req);
    read_tracks(&old);
    CHECK(!g_tracks_ready); /* superseded worker results cannot replace the new request */
    cancel_tracks();
    read_tracks(&g_request);
    CHECK(!g_tracks_ready && !g_request.pending);
    fixture_port = 1;
    req.stick = 1;
    req.generation = g_generation;
    req.rule.low = 0;
    read_tracks(&req);
    CHECK(g_ntracks == 4);
    req.stick = 0;
    read_tracks(&req);
    CHECK(strstr(g_track_error, "unavailable"));
    fixture_port = 0;
    fixture_mode = 7;
    read_list();
    CHECK(g_nrows == MAX_ROWS && g_rows[255].id == -1 && g_rows[511].id == -1);
    CHECK(g_rows[255].stick == 0 && g_rows[511].stick == 1);
    fixture_mode = 0;
}

static void test_ui_and_settings(void)
{
    load_rules();
    CHECK(rules[0].high == 109 && rules[1].low == 120 && rules[2].low == 140);
    smart_slot = 1;
    smart_sel = S_LOW;
    adjust_smart(1);
    CHECK(rules[1].low == 121 && rules[0].low == 0);
    load_rules();
    CHECK(rules[1].low == 121);
    read_list();
    CHECK(g_nrows == 4 && g_rows[3].id == -1);
    showing_tracks = showing_smart = 0;
    sel = 3;
    az_event click = { .kind = AZ_EV_CLICK }, back = { .kind = AZ_EV_BACK };
    on_input(&click, NULL);
    CHECK(showing_smart && !showing_tracks);
    smart_sel = S_GENRE;
    smart_edit = 1;
    adjust_smart(1);
    CHECK(!strcmp(rules[1].genre, "House"));
    load_rules();
    CHECK(!strcmp(rules[1].genre, "House"));
    on_input(&back, NULL);
    CHECK(!smart_edit && showing_smart);
    smart_sel = S_SHOW;
    on_input(&click, NULL);
    CHECK(showing_tracks && g_request.smart);
    on_input(&back, NULL);
    CHECK(!showing_tracks && showing_smart && !g_request.pending);
    rules[1].artist_deck = 4;
    show_smart_tracks();
    CHECK(!showing_tracks && strstr(smart_error, "unavailable"));
    rules[1].artist_deck = 1;
    fixture_deck_loaded = 0;
    show_smart_tracks();
    CHECK(!showing_tracks && strstr(smart_error, "unavailable"));
    fixture_deck_loaded = 1;
    show_smart_tracks();
    CHECK(showing_tracks && !strcmp(g_request.artist, "A"));
    on_input(&back, NULL);
    rules[1].low = 150;
    rules[1].high = 120;
    show_smart_tracks();
    CHECK(!showing_tracks && strstr(smart_error, "Minimum"));
    on_input(&back, NULL);
    CHECK(!showing_smart);
    CHECK(on_input(&back, NULL) == 0);
    /* Restore a saved genre absent from a newly inserted USB: keep the rule, report it. */
    fixture_mode = 2;
    read_list();
    rules[1].low = rules[1].high = 0;
    rules[1].artist_deck = 0;
    show_smart_tracks();
    read_tracks(&g_request);
    CHECK(strstr(g_track_error, "missing"));
    fixture_mode = 0;
}

static void test_live(void)
{
    CHECK(live_keys_match(1, 12) && live_keys_match(12, 1));
    CHECK(live_keys_match(8, 20) && live_keys_match(20, 8));
    CHECK(!live_keys_match(8, 21) && !live_keys_match(8, 10));
    CHECK(!live_keys_match(0, 8) && !live_keys_match(25, 1));
    fixture_live = 1;
    fixture_live_decks[0] = (az_deck_info){ .size = sizeof(az_deck_info), .loaded = 1, .playing = 1, .bpm = 122, .content_id = 1 };
    snprintf(fixture_live_decks[0].title, sizeof fixture_live_decks[0].title, "Track 000");
    snprintf(fixture_live_decks[0].artist, sizeof fixture_live_decks[0].artist, "A");
    CHECK(live_init() == 0);
    CHECK(fixture_surface_rect.w == LIVE_BADGE.w && !fixture_focus);
    struct live_request r = live_capture();
    CHECK(r.reference == 0 && !r.notice[0]);
    fixture_live_decks[1] = fixture_live_decks[0];
    r = live_capture();
    CHECK(r.reference == -1 && strstr(r.notice, "Several"));
    live_deck_choice = 2;
    r = live_capture();
    CHECK(r.reference == 1);
    live_deck_choice = 4;
    r = live_capture();
    CHECK(r.reference == -1 && strstr(r.notice, "not playing"));
    live_deck_choice = 0;
    memset(&fixture_live_decks[1], 0, sizeof fixture_live_decks[1]);
    fixture_live_decks[0].bpm = NAN;
    r = live_capture();
    CHECK(strstr(r.notice, "no known BPM"));
    fixture_live_decks[0].bpm = 122;

    struct live_track candidates[2] = {
        { .row = { .title = "Track 000", .artist = "A", .key = "Am", .bpm = 122 }, .id = 1, .complete = 1 },
        { .row = { .title = "Track 001", .artist = "B", .key = "9A", .bpm = 126 }, .id = 2, .complete = 1 },
    };
    r = live_capture();
    CHECK(live_reference_key(candidates, 2, &r.decks[0]) == 8);
    CHECK(!live_matches(&candidates[0], &r, 8) && live_matches(&candidates[1], &r, 8));
    candidates[1].row.bpm = 126.01;
    CHECK(!live_matches(&candidates[1], &r, 8));
    candidates[1].row.bpm = INFINITY;
    CHECK(!live_matches(&candidates[1], &r, 8));
    candidates[1].row.bpm = 124;
    candidates[1].row.key[0] = 0;
    CHECK(!live_matches(&candidates[1], &r, 8));
    CHECK(live_matches(&candidates[1], &r, 0)); /* explicitly reported BPM-only fallback */
    candidates[1] = candidates[0];
    snprintf(candidates[1].row.key, sizeof candidates[1].row.key, "6A");
    CHECK(live_reference_key(candidates, 2, &r.decks[0]) == 0); /* conflicting duplicates */
    candidates[0].complete = 0;
    candidates[1].complete = 0;
    CHECK(live_reference_key(candidates, 2, &r.decks[0]) == 0);

    az_event tap = { .kind = AZ_EV_TAP }, back = { .kind = AZ_EV_BACK };
    fixture_refuse_focus = 1;
    live_input(&tap, NULL);
    CHECK(!live_open && !fixture_focus);
    fixture_refuse_focus = 0;
    live_input(&tap, NULL);
    CHECK(live_open && fixture_focus);
    tracks_called = 0;
    live_read_pending();
    CHECK(live_ready && live_result.n == 3 && live_result.reference_key == 8);
    CHECK(tracks_called == 2 && live_result.ports == 1);
    CHECK(!strcmp(live_result.tracks[live_result.order[0]].row.title, "Track 002"));
    int before = tracks_called;
    fixture_live_decks[0].bpm = 123;
    live_submit(0);
    live_read_pending();
    CHECK(tracks_called == before && live_result.bpm == 123); /* cached pitch update */
    CHECK(!strcmp(live_result.tracks[live_result.order[0]].row.title, "Track 003"));
    live_submit(0);
    CHECK(!live_pending); /* no repeated unchanged scans */
    fixture_mode = 8;
    fixture_live_decks[0].bpm = 125;
    live_submit(1);
    live_read_pending();
    CHECK(live_result.n == 299 && live_result.scanned == 300); /* all matches, not first 256 */
    fixture_mode = 9;
    live_submit(1);
    live_read_pending();
    CHECK(live_result.ports == 3 && live_result.scanned == 8);
    fixture_mode = 6;
    live_submit(1);
    live_read_pending();
    CHECK(live_result.limited);
    fixture_mode = 1;
    live_submit(1);
    live_read_pending();
    CHECK(live_result.n == 0 && strstr(live_result.notice, "No readable"));
    fixture_live_decks[0].bpm = 126;
    live_submit(0);
    live_read_pending();
    CHECK(strstr(live_result.notice, "No readable"));
    fixture_mode = 0;
    live_submit(1);
    fixture_cancel_live = 1;
    live_read_pending();
    CHECK(!live_ready && live_pending); /* a cancelled scan cannot replace the request */
    live_read_pending();
    CHECK(!live_result.tracks && !live_result.order);
    live_input(&back, NULL);
    live_read_pending();
    CHECK(!live_open && !fixture_focus && !live_result.tracks);
    live_input(&tap, NULL);
    live_read_pending();
    fixture_view = AZ_VIEW_SOURCE;
    live_tick();
    live_read_pending();
    CHECK(!live_open && !fixture_focus && !live_result.tracks); /* navigation releases the knob */
    fixture_view = AZ_VIEW_BROWSE;
    fixture_live = 0;
}

int main(void)
{
    C = &core;
    test_predicates();
    test_reader();
    test_ui_and_settings();
    test_live();
    printf("Smart playlists: %d checks passed\n", checks);
    return 0;
}
