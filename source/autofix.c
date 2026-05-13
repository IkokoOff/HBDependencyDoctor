/**
 * autofix.c
 * Implements all AUTOFIX_* operations and the report exporter.
 */

#include "checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>   // rmdir()
#include <errno.h>
#include <time.h>

// ─── Internal helpers ─────────────────────────────────────────────────────

/** mkdir -p: create every component of a path */
static bool af_mkdir_p(const char* path) {
    char tmp[MAX_ARG];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len-1] == '/') tmp[--len] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777); // ignore EEXIST
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

/** Recursively delete a directory and all its contents. */
static bool af_rmdir_r(const char* path) {
    DIR* d = opendir(path);
    if (!d) return false;

    struct dirent* entry;
    char full[MAX_ARG * 2];
    bool ok = true;

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!af_rmdir_r(full)) ok = false;
        } else {
            if (remove(full) != 0) ok = false;
        }
    }
    closedir(d);
    if (rmdir(path) != 0) ok = false;
    return ok;
}

/**
 * Read /luma/config.ini, set or add a key=value, write back.
 * If set_zero is true, forces the value to "0" regardless of arg2.
 */
static bool af_ini_set(const char* key, const char* value) {
    const char* INI = "sdmc:/luma/config.ini";

    // ── read ──────────────────────────────────────────────────────────────
    FILE* f = fopen(INI, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 128*1024) { fclose(f); return false; }

    char* orig = (char*)malloc((size_t)sz + 1);
    if (!orig) { fclose(f); return false; }
    fread(orig, 1, (size_t)sz, f);
    orig[sz] = '\0';
    fclose(f);

    // ── locate key ────────────────────────────────────────────────────────
    char search[128];
    snprintf(search, sizeof(search), "%s=", key);
    char* pos = strstr(orig, search);

    // New content buffer: original + room for new line
    char* newbuf = (char*)malloc((size_t)sz + strlen(key) + strlen(value) + 8);
    if (!newbuf) { free(orig); return false; }

    if (pos) {
        // Copy up to and including "key="
        size_t prefix = (size_t)(pos - orig) + strlen(search);
        memcpy(newbuf, orig, prefix);
        newbuf[prefix] = '\0';

        // Append new value
        strcat(newbuf, value);

        // Skip old value (to end-of-line) in orig
        char* eol = pos + strlen(search);
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        strcat(newbuf, eol);
    } else {
        // Append new key=value at end
        strcpy(newbuf, orig);
        int len = (int)strlen(newbuf);
        // Ensure trailing newline before appending
        if (len > 0 && newbuf[len-1] != '\n') strcat(newbuf, "\n");
        strcat(newbuf, key);
        strcat(newbuf, "=");
        strcat(newbuf, value);
        strcat(newbuf, "\n");
    }

    // ── write back ────────────────────────────────────────────────────────
    f = fopen(INI, "wb");
    bool ok = false;
    if (f) {
        fputs(newbuf, f);
        fclose(f);
        ok = true;
    }
    free(orig);
    free(newbuf);
    return ok;
}

/** Ensure the destination directory exists, then rename src to dst. */
static bool af_move_file(const char* src, const char* dst) {
    // Make sure destination directory exists
    char dst_dir[MAX_ARG];
    strncpy(dst_dir, dst, sizeof(dst_dir)-1);
    dst_dir[sizeof(dst_dir)-1] = '\0';
    char* slash = strrchr(dst_dir, '/');
    if (slash) {
        *slash = '\0';
        af_mkdir_p(dst_dir);
    }
    return rename(src, dst) == 0;
}

// ─── Internal: delete all orphaned tickets via AM service ────────────────

static bool is_user_title(u64 tid){
    u32 hi=(u32)(tid>>32);
    return hi==0x00040000||hi==0x0004008C||hi==0x0004000E;
}

static int af_delete_orphan_tickets(void) {
    u32 count = 0;
    if (R_FAILED(AM_GetTicketCount(&count)) || count == 0) return 0;

    u32 rc = (count < 3000) ? count : 3000;
    u64* tids = (u64*)malloc(rc * sizeof(u64));
    if (!tids) return 0;

    u32 tr = 0;
    if (R_FAILED(AM_GetTicketList(&tr, rc, 0, tids)) || tr == 0) {
        free(tids); return 0;
    }

    u32 sc = 0; AM_GetTitleCount(MEDIATYPE_SD, &sc);
    if (sc > 3000) sc = 3000;
    u64* stids = NULL; u32 sr = 0;
    if (sc > 0) {
        stids = (u64*)malloc(sc * sizeof(u64));
        if (stids) AM_GetTitleList(&sr, MEDIATYPE_SD, sc, stids);
    }

    u32 nc = 0; AM_GetTitleCount(MEDIATYPE_NAND, &nc);
    if (nc > 3000) nc = 3000;
    u64* ntids = NULL; u32 nr = 0;
    if (nc > 0) {
        ntids = (u64*)malloc(nc * sizeof(u64));
        if (ntids) AM_GetTitleList(&nr, MEDIATYPE_NAND, nc, ntids);
    }

    int deleted = 0;
    for (u32 i = 0; i < tr; i++) {
        u64 tid = tids[i];
        if (tid == 0 || !is_user_title(tid)) continue;

        bool found = false;
        for (u32 j = 0; j < sr && stids; j++)
            if (stids[j] == tid) { found = true; break; }
        if (!found)
            for (u32 j = 0; j < nr && ntids; j++)
                if (ntids[j] == tid) { found = true; break; }

        if (!found) {
            if (R_SUCCEEDED(AM_DeleteTicket(tid))) deleted++;
        }
    }

    free(tids); if (stids) free(stids); if (ntids) free(ntids);
    return deleted;
}

// ─── Public: run the auto-fix stored in an issue ──────────────────────────

bool autofix_run(Issue* issue) {
    if (!issue) return false;
    if (issue->fixed || issue->autofix == AUTOFIX_NONE) {
        snprintf(issue->fix_result_msg, MAX_STR, "No auto-fix available.");
        issue->fix_failed = true;
        return false;
    }

    bool ok = false;

    switch (issue->autofix) {

    case AUTOFIX_CREATE_DIR:
        ok = af_mkdir_p(issue->autofix_arg1);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Created: %.60s", issue->autofix_arg1);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Failed to create %.60s", issue->autofix_arg1);
        break;

    case AUTOFIX_DELETE_FILE:
        ok = (remove(issue->autofix_arg1) == 0);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Deleted: %.60s", issue->autofix_arg1);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Could not delete %.60s", issue->autofix_arg1);
        break;

    case AUTOFIX_DELETE_DIR:
        ok = af_rmdir_r(issue->autofix_arg1);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Removed dir: %.60s", issue->autofix_arg1);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Partial removal: %.60s",
                     issue->autofix_arg1);
        break;

    case AUTOFIX_MOVE_FILE:
        ok = af_move_file(issue->autofix_arg1, issue->autofix_arg2);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Moved to: %.60s", issue->autofix_arg2);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Move failed: %.60s -> %.60s",
                     issue->autofix_arg1, issue->autofix_arg2);
        break;

    case AUTOFIX_INI_SET:
        ok = af_ini_set(issue->autofix_arg1, issue->autofix_arg2);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Set %.30s=%.30s in config.ini",
                     issue->autofix_arg1, issue->autofix_arg2);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Failed to write config.ini");
        break;

    case AUTOFIX_INI_REMOVE:
        ok = af_ini_set(issue->autofix_arg1, "0");
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Disabled %.60s in config.ini", issue->autofix_arg1);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Failed to write config.ini");
        break;

    case AUTOFIX_DELETE_ORPHAN_TICKETS: {
        int del = af_delete_orphan_tickets();
        ok = (del > 0);
        if (ok)
            snprintf(issue->fix_result_msg, MAX_STR,
                     "Deleted %d orphaned ticket(s)", del);
        else
            snprintf(issue->fix_result_msg, MAX_STR,
                     "No orphaned tickets found to delete");
        break;
    }

    default:
        snprintf(issue->fix_result_msg, MAX_STR, "Unknown fix type.");
        issue->fix_failed = true;
        return false;
    }

    issue->fixed      = ok;
    issue->fix_failed = !ok;
    return ok;
}

// ─── Report export ────────────────────────────────────────────────────────

bool export_report(const Report* r, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return false;

    // Header
    fprintf(f, "==============================================\n");
    fprintf(f, "  HB Dependency Doctor — Diagnostic Report\n");
    fprintf(f, "==============================================\n\n");

    // Summary
    fprintf(f, "Summary:\n");
    fprintf(f, "  OK    : %d\n", r->ok_count);
    fprintf(f, "  INFO  : %d\n", r->info_count);
    fprintf(f, "  WARN  : %d\n", r->warn_count);
    fprintf(f, "  ERROR : %d\n", r->error_count);
    fprintf(f, "  FATAL : %d\n", r->fatal_count);
    fprintf(f, "  TOTAL : %d\n\n", r->count);

    static const char* sev_str[] = { "OK   ", "INFO ", "WARN ", "ERROR", "FATAL" };

    // Issues grouped by severity (FATAL first)
    for (int sev = SEV_FATAL; sev >= SEV_OK; sev--) {
        bool header_printed = false;
        for (int i = 0; i < r->count; i++) {
            const Issue* iss = &r->issues[i];
            if ((int)iss->severity != sev) continue;
            if (!header_printed) {
                fprintf(f, "\n── %s Issues ───────────────────────────\n",
                        sev_str[sev]);
                header_printed = true;
            }
            fprintf(f, "[%s][%-8s] %s\n",
                    sev_str[sev], iss->category, iss->description);
            if (iss->fix_hint[0])
                fprintf(f, "          Fix: %s\n", iss->fix_hint);
            if (iss->fixed)
                fprintf(f, "          AUTO-FIXED: %s\n", iss->fix_result_msg);
        }
    }

    fprintf(f, "\n==============================================\n");
    fprintf(f, "  Generated by HB Dependency Doctor v1.0\n");
    fprintf(f, "==============================================\n");
    fclose(f);
    return true;
}

// ─── SD space ─────────────────────────────────────────────────────────────

void get_sd_space(u64* free_bytes, u64* total_bytes) {
    FS_ArchiveResource res;
    if (R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&res))) {
        u64 cluster = (u64)res.clusterSize;
        if (free_bytes)  *free_bytes  = (u64)res.freeClusters  * cluster;
        if (total_bytes) *total_bytes = (u64)res.totalClusters * cluster;
    } else {
        if (free_bytes)  *free_bytes  = 0;
        if (total_bytes) *total_bytes = 0;
    }
}
