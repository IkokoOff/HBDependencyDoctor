/**
 * report.c — Issue reporting helpers.
 *
 * report_add()        Appends a new issue to the report and increments the
 *                     matching severity counter.
 * report_set_autofix() Attaches an auto-fix descriptor to the most recently
 *                     added issue. Must be called immediately after report_add().
 */
#include "checks.h"
#include <string.h>

void report_add(Report* r, Severity sev, const char* cat,
                const char* desc, const char* fix_hint) {
    if (!r || r->count >= MAX_ISSUES) return;
    Issue* i = &r->issues[r->count++];
    memset(i, 0, sizeof(*i));
    i->severity = sev;
    strncpy(i->category,    cat      ? cat      : "?",  MAX_CAT-1);
    strncpy(i->description, desc     ? desc     : "",   MAX_STR-1);
    strncpy(i->fix_hint,    fix_hint ? fix_hint : "",   MAX_STR-1);
    switch (sev) {
        case SEV_OK:    r->ok_count++;    break;
        case SEV_INFO:  r->info_count++;  break;
        case SEV_WARN:  r->warn_count++;  break;
        case SEV_ERROR: r->error_count++; break;
        case SEV_FATAL: r->fatal_count++; break;
    }
}

void report_set_autofix(Report* r, AutoFixType t, const char* a1, const char* a2) {
    if (!r || r->count == 0) return;
    Issue* i = &r->issues[r->count - 1];
    i->autofix = t;
    strncpy(i->autofix_arg1, a1 ? a1 : "", MAX_ARG-1);
    strncpy(i->autofix_arg2, a2 ? a2 : "", MAX_ARG-1);
}
