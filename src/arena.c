/* pvp -- arena implementation: a small JSON reader plus Tiled .tmj loading.
 *
 * The parser builds a tiny DOM from the whole document, we pick out the
 * fields we care about, then throw the DOM away. It covers the full JSON
 * grammar (numbers, strings with escapes incl. surrogate pairs, bools,
 * null, arrays, objects) with a nesting cap, because Tiled maps can nest
 * arbitrarily through user-chosen layer names.
 */

#include "arena.h"

#include <stdlib.h> /* strtod */

/* ------------------------------------------------------------------ */
/* minimal JSON DOM                                                    */
/* ------------------------------------------------------------------ */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
        JType t;
        bool b;
        double num;
        char *str;   /* J_STR */
        char **keys; /* J_OBJ: parallel to items */
        JVal **items;
        size_t n, cap;
};

typedef struct {
        const char *s;
        size_t len, pos;
        int depth;
        char err[96];
} JP;

static void jfail(JP *p, const char *msg)
{
        if (!p->err[0]) {
                SDL_snprintf(p->err, sizeof(p->err), "%s (byte %lu)",
                             msg, (unsigned long)p->pos);
        }
}

static JVal *jnew(JType t)
{
        JVal *v = SDL_calloc(1, sizeof(*v));
        if (v) {
                v->t = t;
        }
        return v;
}

static void jfree(JVal *v)
{
        if (!v) {
                return;
        }
        SDL_free(v->str);
        for (size_t i = 0; i < v->n; i++) {
                if (v->keys) {
                        SDL_free(v->keys[i]);
                }
                jfree(v->items[i]);
        }
        SDL_free(v->keys);
        SDL_free(v->items);
        SDL_free(v);
}

/* growable push; key is copied, item ownership passes to v */
static bool jpush(JVal *v, const char *key, JVal *item)
{
        if (!v || !item) {
                return false;
        }
        if (v->n == v->cap) {
                const size_t cap = v->cap ? v->cap * 2 : 8;
                JVal **items = SDL_realloc(v->items, cap * sizeof(*items));
                if (!items) {
                        return false;
                }
                v->items = items;
                if (v->t == J_OBJ) {
                        char **keys = SDL_realloc(v->keys, cap * sizeof(*keys));
                        if (!keys) {
                                return false;
                        }
                        v->keys = keys;
                }
                v->cap = cap;
        }
        if (v->t == J_OBJ) {
                char *k = SDL_strdup(key);
                if (!k) {
                        return false;
                }
                v->keys[v->n] = k;
        }
        v->items[v->n] = item;
        v->n++;
        return true;
}

static void jws(JP *p)
{
        while (p->pos < p->len) {
                const char c = p->s[p->pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                        p->pos++;
                } else {
                        break;
                }
        }
}

/* append one byte to a growing buffer; returns false on OOM */
static bool jput(char **buf, size_t *n, size_t *cap, char c)
{
        if (*n + 2 > *cap) {
                const size_t nc = *cap ? *cap * 2 : 16;
                char *nb = SDL_realloc(*buf, nc);
                if (!nb) {
                        return false;
                }
                *buf = nb;
                *cap = nc;
        }
        (*buf)[(*n)++] = c;
        return true;
}

/* parse a JSON string body (opening quote already consumed), returns a
 * NUL-terminated UTF-8 heap string */
static char *jstring(JP *p)
{
        char *out = NULL;
        size_t n = 0, cap = 0;
        while (p->pos < p->len) {
                const unsigned char c = (unsigned char)p->s[p->pos++];
                if (c == '"') {
                        if (!jput(&out, &n, &cap, '\0')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        return out;
                }
                if (c != '\\') {
                        if (c < 0x20) {
                                jfail(p, "control char in string");
                                SDL_free(out);
                                return NULL;
                        }
                        if (!jput(&out, &n, &cap, (char)c)) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        continue;
                }
                if (p->pos >= p->len) {
                        break;
                }
                const char e = p->s[p->pos++];
                switch (e) {
                case '"':
                case '\\':
                case '/':
                        if (!jput(&out, &n, &cap, e)) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 'b':
                        if (!jput(&out, &n, &cap, '\b')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 'f':
                        if (!jput(&out, &n, &cap, '\f')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 'n':
                        if (!jput(&out, &n, &cap, '\n')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 'r':
                        if (!jput(&out, &n, &cap, '\r')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 't':
                        if (!jput(&out, &n, &cap, '\t')) {
                                jfail(p, "out of memory");
                                SDL_free(out);
                                return NULL;
                        }
                        break;
                case 'u': {
                        if (p->pos + 4 > p->len) {
                                jfail(p, "bad \\u escape");
                                SDL_free(out);
                                return NULL;
                        }
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++) {
                                const char h = p->s[p->pos++];
                                unsigned d;
                                if (h >= '0' && h <= '9') {
                                        d = (unsigned)(h - '0');
                                } else if (h >= 'a' && h <= 'f') {
                                        d = (unsigned)(h - 'a' + 10);
                                } else if (h >= 'A' && h <= 'F') {
                                        d = (unsigned)(h - 'A' + 10);
                                } else {
                                        jfail(p, "bad \\u escape");
                                        SDL_free(out);
                                        return NULL;
                                }
                                cp = cp * 16 + d;
                        }
                        /* surrogate pair -> one code point */
                        if (cp >= 0xD800 && cp <= 0xDBFF && p->pos + 1 < p->len &&
                            p->s[p->pos] == '\\' && p->s[p->pos + 1] == 'u') {
                                p->pos += 2;
                                if (p->pos + 4 > p->len) {
                                        jfail(p, "bad surrogate pair");
                                        SDL_free(out);
                                        return NULL;
                                }
                                unsigned lo = 0;
                                for (int i = 0; i < 4; i++) {
                                        const char h = p->s[p->pos++];
                                        unsigned d;
                                        if (h >= '0' && h <= '9') {
                                                d = (unsigned)(h - '0');
                                        } else if (h >= 'a' && h <= 'f') {
                                                d = (unsigned)(h - 'a' + 10);
                                        } else if (h >= 'A' && h <= 'F') {
                                                d = (unsigned)(h - 'A' + 10);
                                        } else {
                                                jfail(p, "bad surrogate pair");
                                                SDL_free(out);
                                                return NULL;
                                        }
                                        lo = lo * 16 + d;
                                }
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                        cp = 0x10000 + ((cp - 0xD800) << 10) +
                                             (lo - 0xDC00);
                                } else {
                                        jfail(p, "bad surrogate pair");
                                        SDL_free(out);
                                        return NULL;
                                }
                        }
                        if (cp >= 0xD800 && cp <= 0xDFFF) {
                                jfail(p, "lone surrogate");
                                SDL_free(out);
                                return NULL;
                        }
                        /* UTF-8 encode */
                        if (cp < 0x80) {
                                if (!jput(&out, &n, &cap, (char)cp)) {
                                        jfail(p, "out of memory");
                                        SDL_free(out);
                                        return NULL;
                                }
                        } else if (cp < 0x800) {
                                if (!jput(&out, &n, &cap, (char)(0xC0 | (cp >> 6))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | (cp & 0x3F)))) {
                                        jfail(p, "out of memory");
                                        SDL_free(out);
                                        return NULL;
                                }
                        } else if (cp < 0x10000) {
                                if (!jput(&out, &n, &cap, (char)(0xE0 | (cp >> 12))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | ((cp >> 6) & 0x3F))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | (cp & 0x3F)))) {
                                        jfail(p, "out of memory");
                                        SDL_free(out);
                                        return NULL;
                                }
                        } else {
                                if (!jput(&out, &n, &cap, (char)(0xF0 | (cp >> 18))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | ((cp >> 12) & 0x3F))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | ((cp >> 6) & 0x3F))) ||
                                    !jput(&out, &n, &cap, (char)(0x80 | (cp & 0x3F)))) {
                                        jfail(p, "out of memory");
                                        SDL_free(out);
                                        return NULL;
                                }
                        }
                        break;
                }
                default:
                        jfail(p, "unknown escape");
                        SDL_free(out);
                        return NULL;
                }
        }
        jfail(p, "unterminated string");
        SDL_free(out);
        return NULL;
}

static JVal *jvalue(JP *p); /* forward */

static JVal *jobject(JP *p)
{
        p->depth++;
        if (p->depth > 64) {
                jfail(p, "nesting too deep");
                return NULL;
        }
        p->pos++; /* '{' */
        JVal *o = jnew(J_OBJ);
        if (!o) {
                jfail(p, "out of memory");
                return NULL;
        }
        jws(p);
        if (p->pos < p->len && p->s[p->pos] == '}') {
                p->pos++;
                p->depth--;
                return o;
        }
        for (;;) {
                jws(p);
                if (p->pos >= p->len || p->s[p->pos] != '"') {
                        jfail(p, "expected object key");
                        jfree(o);
                        return NULL;
                }
                p->pos++;
                char *key = jstring(p);
                if (!key) {
                        jfree(o);
                        return NULL;
                }
                jws(p);
                if (p->pos >= p->len || p->s[p->pos] != ':') {
                        jfail(p, "expected ':'");
                        SDL_free(key);
                        jfree(o);
                        return NULL;
                }
                p->pos++;
                JVal *item = jvalue(p);
                const bool ok = item && jpush(o, key, item);
                SDL_free(key);
                if (!ok) {
                        jfree(item);
                        jfree(o);
                        return NULL;
                }
                jws(p);
                if (p->pos < p->len && p->s[p->pos] == ',') {
                        p->pos++;
                        continue;
                }
                if (p->pos < p->len && p->s[p->pos] == '}') {
                        p->pos++;
                        p->depth--;
                        return o;
                }
                jfail(p, "expected ',' or '}'");
                jfree(o);
                return NULL;
        }
}

static JVal *jarray(JP *p)
{
        p->depth++;
        if (p->depth > 64) {
                jfail(p, "nesting too deep");
                return NULL;
        }
        p->pos++; /* '[' */
        JVal *a = jnew(J_ARR);
        if (!a) {
                jfail(p, "out of memory");
                return NULL;
        }
        jws(p);
        if (p->pos < p->len && p->s[p->pos] == ']') {
                p->pos++;
                p->depth--;
                return a;
        }
        for (;;) {
                JVal *item = jvalue(p);
                if (!item || !jpush(a, "", item)) {
                        jfree(item);
                        jfree(a);
                        return NULL;
                }
                jws(p);
                if (p->pos < p->len && p->s[p->pos] == ',') {
                        p->pos++;
                        continue;
                }
                if (p->pos < p->len && p->s[p->pos] == ']') {
                        p->pos++;
                        p->depth--;
                        return a;
                }
                jfail(p, "expected ',' or ']'");
                jfree(a);
                return NULL;
        }
}

static JVal *jvalue(JP *p)
{
        jws(p);
        if (p->pos >= p->len) {
                jfail(p, "unexpected end of input");
                return NULL;
        }
        const char c = p->s[p->pos];
        if (c == '{') {
                return jobject(p);
        }
        if (c == '[') {
                return jarray(p);
        }
        if (c == '"') {
                p->pos++;
                char *s = jstring(p);
                if (!s) {
                        return NULL;
                }
                JVal *v = jnew(J_STR);
                if (!v) {
                        SDL_free(s);
                        jfail(p, "out of memory");
                        return NULL;
                }
                v->str = s;
                return v;
        }
        if (p->pos + 4 <= p->len && SDL_strncmp(p->s + p->pos, "true", 4) == 0) {
                p->pos += 4;
                JVal *v = jnew(J_BOOL);
                if (v) {
                        v->b = true;
                }
                return v;
        }
        if (p->pos + 5 <= p->len && SDL_strncmp(p->s + p->pos, "false", 5) == 0) {
                p->pos += 5;
                return jnew(J_BOOL);
        }
        if (p->pos + 4 <= p->len && SDL_strncmp(p->s + p->pos, "null", 4) == 0) {
                p->pos += 4;
                return jnew(J_NULL);
        }
        /* number: copy the token so strtod cannot read past the buffer */
        char tok[64];
        size_t tn = 0;
        while (p->pos < p->len && tn < sizeof(tok) - 1) {
                const char h = p->s[p->pos];
                if ((h >= '0' && h <= '9') || h == '-' || h == '+' || h == '.' ||
                    h == 'e' || h == 'E') {
                        tok[tn++] = h;
                        p->pos++;
                } else {
                        break;
                }
        }
        if (tn == 0) {
                jfail(p, "unexpected character");
                return NULL;
        }
        tok[tn] = '\0';
        char *end = NULL;
        const double d = strtod(tok, &end);
        if (!end || *end != '\0') {
                jfail(p, "bad number");
                return NULL;
        }
        JVal *v = jnew(J_NUM);
        if (v) {
                v->num = d;
        }
        return v;
}

static const JVal *jget(const JVal *o, const char *key)
{
        if (!o || o->t != J_OBJ) {
                return NULL;
        }
        for (size_t i = 0; i < o->n; i++) {
                if (SDL_strcmp(o->keys[i], key) == 0) {
                        return o->items[i];
                }
        }
        return NULL;
}

static bool jint(const JVal *v, int *out)
{
        if (v && v->t == J_NUM) {
                *out = (int)v->num;
                return true;
        }
        return false;
}

static bool jstr(const JVal *v, const char **out)
{
        if (v && v->t == J_STR) {
                *out = v->str;
                return true;
        }
        return false;
}

/* ------------------------------------------------------------------ */
/* arena loading                                                       */
/* ------------------------------------------------------------------ */

static void arena_default_spawns(Arena *a)
{
        const float cy = (PD_VIEW_H - PD_PLAYER_SIZE) * 0.5f;
        a->p1_spawn.x = 40.0f;
        a->p1_spawn.y = cy;
        a->p1_spawn.w = PD_PLAYER_SIZE;
        a->p1_spawn.h = PD_PLAYER_SIZE;
        a->p2_spawn.x = PD_VIEW_W - 40.0f - PD_PLAYER_SIZE;
        a->p2_spawn.y = cy;
        a->p2_spawn.w = PD_PLAYER_SIZE;
        a->p2_spawn.h = PD_PLAYER_SIZE;
}

bool arena_init_default(Arena *a)
{
        SDL_zerop(a);
        a->tiled = false;
        a->legacy_bounds.x = PD_WALL;
        a->legacy_bounds.y = PD_WALL;
        a->legacy_bounds.w = PD_VIEW_W - 2.0f * PD_WALL;
        a->legacy_bounds.h = PD_VIEW_H - 2.0f * PD_WALL;
        arena_default_spawns(a);
        return true;
}

void arena_free(Arena *a)
{
        if (a) {
                SDL_free(a->solid);
        }
        SDL_zerop(a);
}

bool arena_load_tmj_buf(Arena *a, const char *json, size_t len,
                        char *err, size_t errlen)
{
        SDL_zerop(a);
        if (err && errlen) {
                err[0] = '\0';
        }

        JP p = { json, len, 0, 0, { 0 } };
        JVal *root = jvalue(&p);
        bool ok = false;
        bool *solid = NULL;

        if (!root) {
                goto fail;
        }
        if (root->t != J_OBJ) {
                jfail(&p, "map root is not an object");
                goto fail;
        }

        int w = 0, h = 0, tw = 0, th = 0;
        if (!jint(jget(root, "width"), &w) || !jint(jget(root, "height"), &h) ||
            !jint(jget(root, "tilewidth"), &tw) || !jint(jget(root, "tileheight"), &th)) {
                jfail(&p, "map needs width/height/tilewidth/tileheight");
                goto fail;
        }
        if (w < 1 || w > 128 || h < 1 || h > 128) {
                jfail(&p, "map size out of range (1..128 tiles)");
                goto fail;
        }
        if (tw < 4 || tw > 128 || th < 4 || th > 128) {
                jfail(&p, "tile size out of range (4..128 px)");
                goto fail;
        }

        const JVal *layers = jget(root, "layers");
        if (!layers || layers->t != J_ARR || layers->n == 0) {
                jfail(&p, "map has no layers array");
                goto fail;
        }

        /* fill the map geometry first: spawns and collision both live in
         * view space, and the map is centered inside the 320x180 view */
        a->tiled = true;
        a->tw = tw;
        a->th = th;
        a->w = w;
        a->h = h;
        a->offx = (PD_VIEW_W - (float)(w * tw)) * 0.5f;
        a->offy = (PD_VIEW_H - (float)(h * th)) * 0.5f;

        /* first tile layer = collision */
        bool have_layer = false;
        for (size_t i = 0; i < layers->n && !have_layer; i++) {
                const JVal *L = layers->items[i];
                const char *type = NULL;
                if (!jstr(jget(L, "type"), &type) || SDL_strcmp(type, "tilelayer") != 0) {
                        continue;
                }
                const JVal *data = jget(L, "data");
                if (!data || data->t != J_ARR) {
                        jfail(&p, "tile layer has no data array (export uncompressed JSON)");
                        goto fail;
                }
                if (data->n != (size_t)(w * h)) {
                        jfail(&p, "tile layer data size mismatch");
                        goto fail;
                }
                solid = SDL_malloc((size_t)w * (size_t)h);
                if (!solid) {
                        jfail(&p, "out of memory");
                        goto fail;
                }
                for (size_t j = 0; j < data->n; j++) {
                        const JVal *d = data->items[j];
                        solid[j] = d && d->t == J_NUM && d->num > 0.5;
                }
                have_layer = true;
        }
        if (!have_layer) {
                jfail(&p, "map needs a tile layer (walls)");
                goto fail;
        }

        /* object layers = spawns (center of the p1/p2 objects) */
        bool have_p1 = false, have_p2 = false;
        for (size_t i = 0; i < layers->n; i++) {
                const JVal *L = layers->items[i];
                const char *type = NULL;
                if (!jstr(jget(L, "type"), &type) || SDL_strcmp(type, "objectgroup") != 0) {
                        continue;
                }
                const JVal *objects = jget(L, "objects");
                if (!objects || objects->t != J_ARR) {
                        continue;
                }
                for (size_t k = 0; k < objects->n; k++) {
                        const JVal *ob = objects->items[k];
                        const char *name = NULL;
                        if (!jstr(jget(ob, "name"), &name)) {
                                continue;
                        }
                        const JVal *jx = jget(ob, "x");
                        const JVal *jy = jget(ob, "y");
                        if (!jx || !jy || jx->t != J_NUM || jy->t != J_NUM) {
                                continue;
                        }
                        const JVal *jw = jget(ob, "width");
                        const JVal *jh = jget(ob, "height");
                        const float ow = (jw && jw->t == J_NUM) ? (float)jw->num : 0.0f;
                        const float oh = (jh && jh->t == J_NUM) ? (float)jh->num : 0.0f;
                        /* map pixel space -> view space via the map offset */
                        const float cx = (float)jx->num + ow * 0.5f + a->offx;
                        const float cy = (float)jy->num + oh * 0.5f + a->offy;
                        SDL_FRect *spawn = NULL;
                        if (SDL_strcmp(name, "p1") == 0 && !have_p1) {
                                spawn = &a->p1_spawn;
                                have_p1 = true;
                        } else if (SDL_strcmp(name, "p2") == 0 && !have_p2) {
                                spawn = &a->p2_spawn;
                                have_p2 = true;
                        }
                        if (spawn) {
                                spawn->x = cx - PD_PLAYER_SIZE * 0.5f;
                                spawn->y = cy - PD_PLAYER_SIZE * 0.5f;
                                spawn->w = PD_PLAYER_SIZE;
                                spawn->h = PD_PLAYER_SIZE;
                        }
                }
        }

        a->solid = solid;
        if (!have_p1 || !have_p2) {
                if (!have_p1) {
                        a->p1_spawn = (SDL_FRect){ 40.0f, (PD_VIEW_H - PD_PLAYER_SIZE) * 0.5f,
                                                   PD_PLAYER_SIZE, PD_PLAYER_SIZE };
                }
                if (!have_p2) {
                        a->p2_spawn = (SDL_FRect){ PD_VIEW_W - 40.0f - PD_PLAYER_SIZE,
                                                   (PD_VIEW_H - PD_PLAYER_SIZE) * 0.5f,
                                                   PD_PLAYER_SIZE, PD_PLAYER_SIZE };
                }
                SDL_Log("arena: missing %s%s object, using default spawn",
                        have_p1 ? "" : "p1", (have_p1 || have_p2) ? "" : "/p2");
        }
        ok = true;
        goto done;

fail:
        SDL_free(solid);
        if (err && errlen) {
                SDL_snprintf(err, errlen, "%s", p.err[0] ? p.err : "unknown error");
        }

done:
        jfree(root);
        return ok;
}

bool arena_load_tmj_file(Arena *a, const char *path, char *err, size_t errlen)
{
        SDL_zerop(a);
        size_t n = 0;
        char *text = (char *)SDL_LoadFile(path, &n);
        if (!text) {
                if (err && errlen) {
                        SDL_snprintf(err, errlen, "cannot read %s: %s",
                                     path, SDL_GetError());
                }
                return false;
        }
        const bool ok = arena_load_tmj_buf(a, text, n, err, errlen);
        SDL_free(text);
        return ok;
}

bool arena_rect_solid(const Arena *a, SDL_FRect r)
{
        if (r.w <= 0.0f || r.h <= 0.0f) {
                return false;
        }
        if (!a->tiled) {
                const SDL_FRect *b = &a->legacy_bounds;
                return r.x < b->x || r.y < b->y ||
                       r.x + r.w > b->x + b->w || r.y + r.h > b->y + b->h;
        }
        /* anything outside the map counts as solid */
        const int tx0 = (int)SDL_floorf((r.x - a->offx) / (float)a->tw);
        const int ty0 = (int)SDL_floorf((r.y - a->offy) / (float)a->th);
        const int tx1 = (int)SDL_floorf((r.x + r.w - 1.0f - a->offx) / (float)a->tw);
        const int ty1 = (int)SDL_floorf((r.y + r.h - 1.0f - a->offy) / (float)a->th);
        for (int ty = ty0; ty <= ty1; ty++) {
                for (int tx = tx0; tx <= tx1; tx++) {
                        if (tx < 0 || ty < 0 || tx >= a->w || ty >= a->h) {
                                return true;
                        }
                        if (a->solid[ty * a->w + tx]) {
                                return true;
                        }
                }
        }
        return false;
}

/* --- placeholder art (palette from game.c, Sweetie-16 inspired) --- */

static const SDL_Color C_BG = { 0x0f, 0x10, 0x19, 255 };
static const SDL_Color C_FLOOR_A = { 0x20, 0x24, 0x38, 255 };
static const SDL_Color C_FLOOR_B = { 0x24, 0x28, 0x3e, 255 };
static const SDL_Color C_WALL = { 0x5a, 0x6b, 0x86, 255 };
static const SDL_Color C_WALL_DARK = { 0x3c, 0x4a, 0x5e, 255 };

static void fill(SDL_Renderer *r, SDL_FRect q, SDL_Color c)
{
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        (void)SDL_RenderFillRect(r, &q);
}

void arena_render(const Arena *a, SDL_Renderer *r)
{
        if (!a->tiled) {
                /* legacy ring: background + checkerboard + 8px walls */
                fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_VIEW_H }, C_BG);
                fill(r, (SDL_FRect){ PD_WALL, PD_WALL, PD_VIEW_W - 2.0f * PD_WALL,
                                     PD_VIEW_H - 2.0f * PD_WALL },
                     C_FLOOR_B);
                for (int y = PD_WALL; y < PD_VIEW_H - PD_WALL; y += 16) {
                        for (int x = PD_WALL; x < PD_VIEW_W - PD_WALL; x += 16) {
                                if (((x / 16) + (y / 16)) % 2 == 0) {
                                        fill(r, (SDL_FRect){ (float)x, (float)y, 16.0f, 16.0f },
                                             C_FLOOR_A);
                                }
                        }
                }
                fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_WALL }, C_WALL);
                fill(r, (SDL_FRect){ 0.0f, PD_VIEW_H - PD_WALL, PD_VIEW_W, PD_WALL }, C_WALL);
                fill(r, (SDL_FRect){ 0.0f, PD_WALL, PD_WALL, PD_VIEW_H - 2.0f * PD_WALL }, C_WALL);
                fill(r, (SDL_FRect){ PD_VIEW_W - PD_WALL, PD_WALL, PD_WALL,
                                     PD_VIEW_H - 2.0f * PD_WALL },
                     C_WALL);
                return;
        }

        /* tile map: dark backdrop (letterbox bands), then one rect per tile */
        fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_VIEW_H }, C_BG);
        for (int ty = 0; ty < a->h; ty++) {
                for (int tx = 0; tx < a->w; tx++) {
                        SDL_FRect q = { a->offx + (float)(tx * a->tw),
                                        a->offy + (float)(ty * a->th),
                                        (float)a->tw, (float)a->th };
                        if (a->solid[ty * a->w + tx]) {
                                fill(r, q, C_WALL);
                                /* 2px darker skirt for a hint of depth */
                                q.y = q.y + q.h - 2.0f;
                                q.h = 2.0f;
                                fill(r, q, C_WALL_DARK);
                        } else {
                                fill(r, q, ((tx + ty) % 2 == 0) ? C_FLOOR_A : C_FLOOR_B);
                        }
                }
        }
}
