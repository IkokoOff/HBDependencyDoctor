/**
 * checks.h — Shared types, constants, and declarations for HB Dependency Doctor.
 *
 * Defines the Issue/Report data model, severity and auto-fix enums,
 * and declares all check-module, auto-fix, and utility functions.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <3ds.h>

#define MAX_ISSUES  256
#define MAX_STR     256
#define MAX_CAT      32
#define MAX_ARG     320

typedef enum { SEV_OK=0, SEV_INFO, SEV_WARN, SEV_ERROR, SEV_FATAL } Severity;

typedef enum {
    AUTOFIX_NONE = 0,
    AUTOFIX_CREATE_DIR,   // arg1 = path
    AUTOFIX_DELETE_FILE,  // arg1 = path
    AUTOFIX_DELETE_DIR,   // arg1 = directory (recursive)
    AUTOFIX_MOVE_FILE,    // arg1 = src, arg2 = dst
    AUTOFIX_INI_SET,      // arg1 = key, arg2 = value  → /luma/config.ini
    AUTOFIX_INI_REMOVE,   // arg1 = key (sets key=0)
    AUTOFIX_DELETE_ORPHAN_TICKETS, // batch: delete all tickets with no matching title
} AutoFixType;

typedef struct {
    Severity    severity;
    char        category[MAX_CAT];
    char        description[MAX_STR];
    char        fix_hint[MAX_STR];
    AutoFixType autofix;
    char        autofix_arg1[MAX_ARG];
    char        autofix_arg2[MAX_ARG];
    bool        fixed;
    bool        fix_failed;
    char        fix_result_msg[MAX_STR];
} Issue;

typedef struct {
    Issue issues[MAX_ISSUES];
    int   count, ok_count, info_count, warn_count, error_count, fatal_count;
} Report;

void report_add(Report* r, Severity sev, const char* cat,
                const char* desc, const char* fix_hint);
void report_set_autofix(Report* r, AutoFixType t, const char* a1, const char* a2);

// ── Check modules ─────────────────────────────────────────────────────────
void check_luma(Report* r);
void check_plugins(Report* r);
void check_tickets(Report* r);
void check_cheats(Report* r);
void check_homebrews(Report* r);
void check_system(Report* r);
void check_layeredfs(Report* r);
void check_modules(Report* r);
void check_hardware(Report* r);

// ── Utilities ─────────────────────────────────────────────────────────────
bool autofix_run(Issue* issue);
bool export_report(const Report* r, const char* path);
void get_sd_space(u64* free_bytes, u64* total_bytes);
