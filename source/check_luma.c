/**
 * check_luma.c — Luma3DS configuration and file validator.
 *
 * Verifies boot.firm, parses config.ini for version, plugin loader,
 * game patching, and DSiWare autoboot settings. Checks for missing
 * payloads, backups directory, and seeddb.bin.
 */
#include "checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool L_exists(const char* p) { struct stat s; return stat(p,&s)==0; }
static long  L_size(const char* p)  { struct stat s; return stat(p,&s)==0?(long)s.st_size:-1L; }

static char* L_readfile(const char* path) {
    FILE* f = fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0||sz>128*1024){fclose(f);return NULL;}
    char* b=(char*)malloc((size_t)sz+1); if(!b){fclose(f);return NULL;}
    fread(b,1,(size_t)sz,f); b[sz]='\0'; fclose(f); return b;
}

static bool L_ini(const char* c, const char* key, char* out, size_t n) {
    char s[128]; snprintf(s,sizeof(s),"%s=",key);
    const char* p=strstr(c,s); if(!p) return false;
    p+=strlen(s);
    const char* e=p; while(*e&&*e!='\n'&&*e!='\r') e++;
    size_t l=(size_t)(e-p); if(l>=n) l=n-1;
    strncpy(out,p,l); out[l]='\0'; return true;
}

void check_luma(Report* r) {

    /* boot.firm ─────────────────────────────────────────────────────────── */
    if (!L_exists("sdmc:/boot.firm")) {
        report_add(r,SEV_FATAL,"LUMA",
            "boot.firm missing from SD root - console will NOT boot CFW",
            "Download Luma3DS latest release and put boot.firm at SD root.");
        // Can't auto-download; direct user to manual fix
    } else {
        long sz = L_size("sdmc:/boot.firm");
        if (sz < 65536) {
            report_add(r,SEV_ERROR,"LUMA",
                "boot.firm is too small - file is likely corrupt",
                "Re-download Luma3DS and replace boot.firm on the SD root.");
        } else {
            report_add(r,SEV_OK,"LUMA","boot.firm present and valid size",NULL);
        }
    }

    /* /luma/config.ini ──────────────────────────────────────────────────── */
    if (!L_exists("sdmc:/luma/config.ini")) {
        report_add(r,SEV_WARN,"LUMA",
            "config.ini not found - Luma has never been configured",
            "Hold SELECT on boot to open the Luma config menu, then save settings.");
        return;
    }

    char* cfg = L_readfile("sdmc:/luma/config.ini");
    if (!cfg) {
        report_add(r,SEV_ERROR,"LUMA",
            "config.ini unreadable - possibly corrupt",
            "Replace config.ini from a fresh Luma3DS archive.");
        return;
    }
    report_add(r,SEV_OK,"LUMA","config.ini readable",NULL);

    /* Luma version ──────────────────────────────────────────────────────── */
    char ver[32]={0};
    if (L_ini(cfg,"luma_version",ver,sizeof(ver))) {
        int maj=0,min=0,patch=0;
        sscanf(ver,"%d.%d.%d",&maj,&min,&patch);
        char desc[128]; snprintf(desc,sizeof(desc),"Luma3DS v%s",ver);
        if (maj < 12) {
            report_add(r,SEV_WARN,"LUMA",desc,
                "Luma v12+ required for full B9S support. Update via GodMode9 or manually.");
        } else if (maj == 12) {
            report_add(r,SEV_INFO,"LUMA",desc,
                "Consider upgrading to Luma v13+ for 3GX plugin support.");
        } else {
            report_add(r,SEV_OK,"LUMA",desc,NULL);
        }
    }

    /* Plugin loader vs. plugins directory ──────────────────────────────── */
    char pl[8]={0}; bool pl_enabled=false;
    L_ini(cfg,"enable_plugin_loader",pl,sizeof(pl));
    pl_enabled=(strcmp(pl,"1")==0);
    bool plugin_dir = L_exists("sdmc:/luma/plugins");

    if (plugin_dir && !pl_enabled) {
        report_add(r,SEV_WARN,"LUMA",
            "/luma/plugins/ exists but plugin loader is DISABLED",
            "Enable plugin loader in Luma config or your plugins will never run.");
        report_set_autofix(r,AUTOFIX_INI_SET,"enable_plugin_loader","1");
    } else if (!plugin_dir && pl_enabled) {
        report_add(r,SEV_INFO,"LUMA",
            "Plugin loader enabled but /luma/plugins/ does not exist",
            "Create /luma/plugins/ and add .3gx files.");
        report_set_autofix(r,AUTOFIX_CREATE_DIR,"sdmc:/luma/plugins",NULL);
    }

    /* Game patching / LayeredFS ─────────────────────────────────────────── */
    char gp[8]={0}; L_ini(cfg,"enable_game_patching",gp,sizeof(gp));
    if (L_exists("sdmc:/luma/titles") && strcmp(gp,"0")==0) {
        report_add(r,SEV_WARN,"LUMA",
            "/luma/titles/ (LayeredFS mods) present but game patching is DISABLED",
            "Enable game patching in Luma config or your mods won't load.");
        report_set_autofix(r,AUTOFIX_INI_SET,"enable_game_patching","1");
    }

    /* DSiWare autoboot misconfiguration ────────────────────────────────── */
    char ab[8]={0}; L_ini(cfg,"autoboot_3ds_homebrew",ab,sizeof(ab));
    if (strcmp(ab,"2")==0) {
        char tid[32]={0}; L_ini(cfg,"autoboot_dsiware_titleid",tid,sizeof(tid));
        bool zero=true;
        for(int i=0;tid[i];i++){if(tid[i]!='0'){zero=false;break;}}
        if(tid[0]=='\0'||zero){
            report_add(r,SEV_ERROR,"LUMA",
                "DSiWare autoboot enabled with no/zero TitleID - console will crash at boot!",
                "Disable autoboot in Luma config immediately.");
            report_set_autofix(r,AUTOFIX_INI_REMOVE,"autoboot_3ds_homebrew",NULL);
        }
    }

    free(cfg);

    /* /luma/payloads/ ───────────────────────────────────────────────────── */
    if (!L_exists("sdmc:/luma/payloads")) {
        report_add(r,SEV_WARN,"LUMA",
            "/luma/payloads/ missing - no emergency recovery access",
            "Create /luma/payloads/ and add GodMode9.firm (hold START on boot).");
        report_set_autofix(r,AUTOFIX_CREATE_DIR,"sdmc:/luma/payloads",NULL);
    } else if (!L_exists("sdmc:/luma/payloads/GodMode9.firm")) {
        report_add(r,SEV_WARN,"LUMA",
            "GodMode9 not in /luma/payloads/ - you have no rescue tool",
            "Download GodMode9 and place GodMode9.firm in /luma/payloads/.");
    } else {
        report_add(r,SEV_OK,"LUMA","GodMode9 recovery payload present",NULL);
    }

    /* /luma/backups/ ────────────────────────────────────────────────────── */
    if (!L_exists("sdmc:/luma/backups")) {
        report_add(r,SEV_WARN,"LUMA",
            "/luma/backups/ missing - Luma has not saved system file backups",
            "Luma creates this on first run; try reinstalling Luma3DS.");
        report_set_autofix(r,AUTOFIX_CREATE_DIR,"sdmc:/luma/backups",NULL);
    } else {
        report_add(r,SEV_OK,"LUMA","Luma backups directory present",NULL);
    }

    /* seeddb.bin ─────────────────────────────────────────────────────────── */
    if(!L_exists("sdmc:/fbi/seed/seeddb.bin")&&!L_exists("sdmc:/seeddb.bin")){
        report_add(r,SEV_INFO,"LUMA",
            "seeddb.bin not found - seed-based eShop games may not install",
            "In FBI: SD/fbi/seed/ then download seeddb, or use Universal-Updater.");
    }
}
