/*
 * How Sagwa announces a message: which kind of cat sound it deserves.
 */

#include "sagwa_notify.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

/* Enough of a message to judge it by */
#define SCAN_MAX 400

/* Important whatever else is going on */
static const char *const STRONG[] = {
    "urgente", "urgent", "urgencia", "emergencia", "emergency", "alarma", "alarm", "auxilio", "socorro",
    NULL,
};

/* Important when it isn't part of a conversation already going */
static const char *const WEAK[] = {
    "importante", "important", "alerta", "alert", "aviso", "warning", "atencion", "attention", "ojo",
    "recordatorio", "recuerda", "reminder", "error", "fallo", "failed", "failure", "caido", "asap",
    "deadline", "vence", "vencimiento", "cita", "llamada perdida", "missed call", "ahora mismo",
    "cuanto antes", "right now", "lo antes posible", NULL,
};

const char *sagwa_notify_kind_name(sagwa_notify_kind_t kind)
{
    switch (kind) {
    case SAGWA_NOTIFY_CHAT: return "chat";
    case SAGWA_NOTIFY_URGENT: return "urgent";
    default: return "notify";
    }
}

/* Lower case ASCII, accents dropped (á -> a, ñ -> n), anything else a space;
 * counts letters and upper-case letters as it goes. */
static size_t fold(const char *in, char *out, size_t cap, int *letters, int *upper, int *bangs)
{
    size_t n = 0;
    *letters = *upper = *bangs = 0;
    out[n++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 2 < cap; p++) {
        unsigned char c = *p;
        char o = ' ';
        if (c == 0xC3 && p[1]) {
            /* Latin-1 supplement: À..ÿ */
            unsigned char d = *++p;
            bool up = d < 0xA0;
            unsigned char base = (unsigned char)(up ? d : d - 0x20);
            static const char MAP[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyps";   /* 0x80..0x9F */
            o = MAP[base - 0x80];
            if (o == '/' || o == 'p' || o == 's') o = ' ';
            if (o != ' ') {
                (*letters)++;
                if (up) (*upper)++;
            }
        } else if (c >= 0x80) {
            /* Other UTF-8: skip continuation bytes, keep a word break */
            while ((p[1] & 0xC0) == 0x80) p++;
        } else if (isalpha(c)) {
            (*letters)++;
            if (isupper(c)) (*upper)++;
            o = (char)tolower(c);
        } else if (isdigit(c)) {
            o = (char)c;
        } else if (c == '!') {
            (*bangs)++;
        }
        if (o == ' ' && out[n - 1] == ' ') continue;
        out[n++] = o;
    }
    if (out[n - 1] != ' ') out[n++] = ' ';
    out[n] = '\0';
    return n;
}

/* A whole word or phrase of the folded text (which starts and ends with a space) */
static bool has_word(const char *folded, const char *const *words)
{
    char pat[40];
    for (; *words; words++) {
        size_t w = strlen(*words);
        if (w + 3 > sizeof(pat)) continue;
        pat[0] = ' ';
        memcpy(pat + 1, *words, w);
        pat[w + 1] = ' ';
        pat[w + 2] = '\0';
        if (strstr(folded, pat)) return true;
    }
    return false;
}

sagwa_notify_kind_t sagwa_notify_classify(bool user_turn, const char *text, int64_t now_us,
                                          int64_t last_exchange_us)
{
    if (user_turn) {
        return SAGWA_NOTIFY_CHAT;
    }
    char folded[SCAN_MAX + 4];
    int letters = 0, upper = 0, bangs = 0;
    fold(text ? text : "", folded, sizeof(folded), &letters, &upper, &bangs);

    if (has_word(folded, STRONG)) {
        return SAGWA_NOTIFY_URGENT;
    }
    if (last_exchange_us > 0 && now_us - last_exchange_us < SAGWA_NOTIFY_CHAT_WINDOW_US) {
        return SAGWA_NOTIFY_CHAT;
    }
    bool shouting = letters >= 8 && upper * 10 >= letters * 7;
    bool exclaims = bangs >= 2 && text && (strstr(text, "!!") || strstr(text, "\xC2\xA1"));
    if (has_word(folded, WEAK) || shouting || exclaims) {
        return SAGWA_NOTIFY_URGENT;
    }
    return SAGWA_NOTIFY_NOTIFY;
}

/* The clip renderer's own, when it is built */
__attribute__((weak)) void sagwa_clips_cue(sagwa_notify_kind_t kind)
{
    (void)kind;
}
