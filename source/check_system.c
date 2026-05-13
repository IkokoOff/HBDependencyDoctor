/**
 * check_system.c — System-level health checks.
 *
 * Checks firmware version, console model, A9LH legacy remnants,
 * boot.3dsx presence, NAND backups, sensitive files at SD root,
 * GodMode9 scripts, and SD card free space.
 */
#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Filesystem helpers */
static bool S_exists(const char* p){struct stat s;return stat(p,&s)==0;}
static long S_size(const char* p)  {struct stat s;return stat(p,&s)==0?(long)s.st_size:-1L;}
static bool S_isdir(const char* p) {struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}

/* Map CFGU model codes to human-readable names */
static const char* model_str(u8 m){
    switch(m){
        case CFG_MODEL_3DS:    return "Old 3DS";
        case CFG_MODEL_3DSXL:  return "Old 3DS XL";
        case CFG_MODEL_N3DS:   return "New 3DS";
        case CFG_MODEL_2DS:    return "2DS";
        case CFG_MODEL_N3DSXL: return "New 3DS XL";
        case CFG_MODEL_N2DSXL: return "New 2DS XL";
        default:               return "Unknown";
    }
}

void check_system(Report* r){

    /* Firmware version */
    {
        u32 fw=osGetFirmVersion();
        u32 maj=GET_VERSION_MAJOR(fw), min=GET_VERSION_MINOR(fw);
        char d[128]; snprintf(d,sizeof(d),"Firmware %lu.%lu.x",(unsigned long)maj,(unsigned long)min);
        if(maj<11) report_add(r,SEV_WARN,"SYSTEM",d,
            "Firmware 11.x recommended. Updating is safe with Luma3DS.");
        else report_add(r,SEV_OK,"SYSTEM",d,NULL);
    }

    /* Console model + N3DS boost check */
    u8 model=CFG_MODEL_3DS;
    if(R_SUCCEEDED(CFGU_GetSystemModel(&model))){
        char d[128]; snprintf(d,sizeof(d),"Console: %s",model_str(model));
        report_add(r,SEV_INFO,"SYSTEM",d,NULL);

        bool is_new=(model==CFG_MODEL_N3DS||model==CFG_MODEL_N3DSXL||model==CFG_MODEL_N2DSXL);
        if(is_new&&S_exists("sdmc:/luma/config.ini")){
            FILE* f=fopen("sdmc:/luma/config.ini","r");
            if(f){ char buf[4096]={0}; fread(buf,1,sizeof(buf)-1,f); fclose(f);
                if(strstr(buf,"n3ds_cpu_speed=1"))
                    report_add(r,SEV_INFO,"SYSTEM",
                        "N3DS CPU boost (804 MHz) enabled",
                        "Can reveal hidden game bugs. Disable in Luma config if games crash.");
            }
        }
    }

    /* A9LH legacy detection */
    if(S_exists("sdmc:/arm9loaderhax.bin")){
        report_add(r,SEV_FATAL,"SYSTEM",
            "arm9loaderhax.bin found - VERY outdated CFW setup!",
            "Follow 3ds.hacks.guide to upgrade from A9LH to boot9strap immediately.");
        report_set_autofix(r,AUTOFIX_NONE,NULL,NULL);
    }

    /* SafeB9SInstaller remnant */
    if(S_exists("sdmc:/SafeB9SInstaller.bin")){
        report_add(r,SEV_INFO,"SYSTEM",
            "SafeB9SInstaller.bin at root - install is done, file is unused",
            "Delete to recover SD space.");
        report_set_autofix(r,AUTOFIX_DELETE_FILE,"sdmc:/SafeB9SInstaller.bin",NULL);
    }

    /* boot.3dsx (Homebrew Launcher) */
    if(!S_exists("sdmc:/boot.3dsx")){
        report_add(r,SEV_WARN,"SYSTEM",
            "boot.3dsx missing - Homebrew Launcher not accessible",
            "Download boot.3dsx from github.com/devkitPro/3ds-hbmenu/releases.");
    } else if(S_size("sdmc:/boot.3dsx")<50000){
        report_add(r,SEV_WARN,"SYSTEM",
            "boot.3dsx is very small - possibly corrupt",
            "Re-download boot.3dsx from devkitPro/3ds-hbmenu.");
    } else {
        report_add(r,SEV_OK,"SYSTEM","Homebrew Launcher (boot.3dsx) present",NULL);
    }

    /* NAND backup */
    {
        bool has=S_exists("sdmc:/nand.bin")||S_exists("sdmc:/nand_min.bin")||
                  S_exists("sdmc:/gm9/out/nand.bin")||S_exists("sdmc:/gm9/out/nand_min.bin");
        if(!has){
            report_add(r,SEV_WARN,"SYSTEM",
                "No NAND backup found - you cannot recover from a brick!",
                "GodMode9 > hold START > Scripts > GM9Megascript > Backup Options > SysNAND Backup.");
        } else {
            const char* backs[]={"sdmc:/nand.bin","sdmc:/nand_min.bin",
                                  "sdmc:/gm9/out/nand.bin","sdmc:/gm9/out/nand_min.bin"};
            bool small_warn=false;
            for(int i=0;i<4;i++){
                long sz=S_size(backs[i]);
                if(sz>0&&sz<64*1024*1024){
                    char d[256]; snprintf(d,sizeof(d),
                        "%s seems too small (%ldMB) for a NAND backup",
                        backs[i]+6, sz/(1024*1024));
                    report_add(r,SEV_WARN,"SYSTEM",d,
                        "Re-create the NAND backup with GodMode9.");
                    small_warn=true;
                }
            }
            if(!small_warn)
                report_add(r,SEV_OK,"SYSTEM","NAND backup found on SD",NULL);
        }
    }

    /* Sensitive files at SD root */
    static const struct{const char* p;const char* n;const char* dst;} SENS[]={
        {"sdmc:/otp.bin",          "otp.bin (unique device key)",  "sdmc:/gm9/out/otp.bin"},
        {"sdmc:/movable.sed",      "movable.sed (device seed)",     "sdmc:/gm9/out/movable.sed"},
        {"sdmc:/secret_sector.bin","secret_sector.bin",             "sdmc:/gm9/out/secret_sector.bin"},
        {"sdmc:/firm0.bin",        "firm0.bin (NAND backup)",       "sdmc:/gm9/out/firm0.bin"},
        {"sdmc:/firm1.bin",        "firm1.bin (NAND backup)",       "sdmc:/gm9/out/firm1.bin"},
    };
    for(size_t i=0;i<sizeof(SENS)/sizeof(SENS[0]);i++){
        if(S_exists(SENS[i].p)){
            char d[256]; snprintf(d,sizeof(d),"Sensitive file at SD root: %s",SENS[i].n);
            report_add(r,SEV_WARN,"SYSTEM",d,
                "Move to /gm9/out/ or a secure subfolder.");
            report_set_autofix(r,AUTOFIX_MOVE_FILE,SENS[i].p,SENS[i].dst);
        }
    }

    /* GodMode9 scripts */
    if(S_isdir("sdmc:/gm9")){
        if(!S_isdir("sdmc:/gm9/scripts"))
            report_add(r,SEV_INFO,"SYSTEM",
                "/gm9/scripts/ missing - GM9Megascript unavailable",
                "Copy the scripts folder from the GodMode9 release archive.");
        else
            report_add(r,SEV_OK,"SYSTEM","GodMode9 + scripts folder present",NULL);
    }

    /* SD card free space */
    {
        u64 fr=0,tot=0; get_sd_space(&fr,&tot);
        if(tot>0){
            u32 fr_mb=(u32)(fr/1024/1024), tot_mb=(u32)(tot/1024/1024);
            u32 pct=(u32)((fr*100)/tot);
            char d[128]; snprintf(d,sizeof(d),"SD: %luMB free / %luMB total (%lu%%)",fr_mb,tot_mb,pct);
            if(pct<5)       report_add(r,SEV_ERROR,"SYSTEM",d,"SD card almost full! Free up space urgently.");
            else if(pct<15) report_add(r,SEV_WARN,"SYSTEM",d,"SD card getting full - delete unneeded files.");
            else            report_add(r,SEV_OK,"SYSTEM",d,NULL);
        }
    }
}
