/**
 * check_layeredfs.c
 * Validates /luma/titles/ (LayeredFS / romfs replacement mods).
 *
 * Expected structure:
 *   /luma/titles/<TitleID16>/romfs/  OR
 *   /luma/titles/<TitleID16>/code.bin  OR  code.ips
 *   /luma/titles/<TitleID16>/exheader.bin
 *
 * Common problems:
 *  - TitleID folder exists but is completely empty
 *  - romfs/ subfolder is empty (mod files missing)
 *  - Non-TitleID folder names
 *  - Game patching disabled in Luma config while titles/ has content
 */

#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static bool LFS_isdir(const char* p){struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}
static bool LFS_exists(const char* p){struct stat s;return stat(p,&s)==0;}

static bool LFS_isTID(const char* n){
    if(!n||strlen(n)!=16)return false;
    for(int i=0;i<16;i++){
        char c=n[i];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false;
    }
    return true;
}

/* Count entries (non-dot) in a directory */
static int LFS_count_entries(const char* path){
    DIR* d=opendir(path); if(!d)return 0;
    int n=0; struct dirent* e;
    while((e=readdir(d))!=NULL) if(e->d_name[0]!='.') n++;
    closedir(d); return n;
}

void check_layeredfs(Report* r){
    const char* ROOT="sdmc:/luma/titles";

    if(!LFS_isdir(ROOT)){
        report_add(r,SEV_INFO,"LAYEREDFS",
            "No /luma/titles/ - no game mods (LayeredFS) installed",
            "Normal. Create /luma/titles/<TitleID>/ to add game mods.");
        return;
    }

    DIR* d=opendir(ROOT); if(!d){
        report_add(r,SEV_WARN,"LAYEREDFS",
            "Cannot open /luma/titles - SD issue","Run chkdsk/fsck on the SD card.");
        return;
    }

    int valid=0, empty_dir=0, no_tid=0, romfs_empty=0;
    struct dirent* e;

    while((e=readdir(d))!=NULL){
        if(e->d_name[0]=='.') continue;
        char full[400]; snprintf(full,sizeof(full),"%s/%s",ROOT,e->d_name);

        if(!LFS_isdir(full)){
            char desc[256]; snprintf(desc,sizeof(desc),"Stray file in /luma/titles/: %s",e->d_name);
            report_add(r,SEV_INFO,"LAYEREDFS",desc,"Only TitleID folders belong here.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            continue;
        }

        if(!LFS_isTID(e->d_name)){
            char desc[256]; snprintf(desc,sizeof(desc),"Non-TitleID folder: %.60s",e->d_name);
            report_add(r,SEV_WARN,"LAYEREDFS",desc,
                "Rename to the 16-digit hex TitleID of the game you want to mod.");
            no_tid++; continue;
        }

        /* Valid TitleID folder - check its contents */
        int cnt=LFS_count_entries(full);
        if(cnt==0){
            char desc[256]; snprintf(desc,sizeof(desc),"Mod folder %s is empty",e->d_name);
            report_add(r,SEV_WARN,"LAYEREDFS",desc,
                "Add mod files or delete the empty folder.");
            report_set_autofix(r,AUTOFIX_DELETE_DIR,full,NULL);
            empty_dir++; continue;
        }

        /* Check romfs/ */
        char romfs[450]; snprintf(romfs,sizeof(romfs),"%s/romfs",full);
        if(LFS_isdir(romfs)&&LFS_count_entries(romfs)==0){
            char desc[256]; snprintf(desc,sizeof(desc),"romfs/ for %s is empty",e->d_name);
            report_add(r,SEV_WARN,"LAYEREDFS",desc,
                "Add mod files inside the romfs/ folder or remove it.");
            report_set_autofix(r,AUTOFIX_DELETE_DIR,romfs,NULL);
            romfs_empty++;
        }

        /* At least one of the expected mod files should exist */
        char code[450],ips[450],exh[450],romfs_ok[450];
        snprintf(code,sizeof(code),   "%s/code.bin",full);
        snprintf(ips,sizeof(ips),     "%s/code.ips",full);
        snprintf(exh,sizeof(exh),     "%s/exheader.bin",full);
        snprintf(romfs_ok,sizeof(romfs_ok),"%s/romfs",full);

        bool has_code=LFS_exists(code)||LFS_exists(ips);
        bool has_romfs=LFS_isdir(romfs_ok)&&LFS_count_entries(romfs_ok)>0;
        bool has_exh=LFS_exists(exh);

        if(!has_code&&!has_romfs&&!has_exh){
            char desc[256]; snprintf(desc,sizeof(desc),
                "Mod folder %s has no code/romfs/exheader",e->d_name);
            report_add(r,SEV_WARN,"LAYEREDFS",desc,
                "Mod appears incomplete. Add code.ips, romfs/, or exheader.bin.");
        } else { valid++; }
    }
    closedir(d);

    if(valid>0){
        char desc[128]; snprintf(desc,sizeof(desc),"%d valid game mod(s) installed",valid);
        report_add(r,SEV_OK,"LAYEREDFS",desc,NULL);
    }
    if(empty_dir+no_tid+romfs_empty==0&&valid==0)
        report_add(r,SEV_INFO,"LAYEREDFS","/luma/titles/ exists but has no valid mods",
            "Add TitleID folders with romfs/ or code.ips to apply game mods.");
}
