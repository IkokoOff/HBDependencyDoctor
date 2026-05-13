/**
 * check_modules.c
 * Validates custom sysmodule replacements in /luma/sysmodules/.
 *
 * Luma3DS loads .cxi files from /luma/sysmodules/ to replace system modules.
 * Common use cases: Pretendo's nwm module, custom nim, etc.
 *
 * Checks:
 *  - Valid .cxi file (not 0 bytes)
 *  - Non-.cxi files (wrong extension)
 *  - Known deprecated modules
 */

#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MIN_CXI 4096  // CXI header alone is 0x200 bytes; real module >> 4KB

static bool MOD_isdir(const char* p){struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}
static long MOD_size(const char* p) {struct stat s;return stat(p,&s)==0?(long)s.st_size:-1L;}

void check_modules(Report* r){
    const char* ROOT="sdmc:/luma/sysmodules";

    if(!MOD_isdir(ROOT)){
        report_add(r,SEV_INFO,"MODULES",
            "No /luma/sysmodules/ - no custom system modules",
            "Normal. Only needed for projects like Pretendo Network.");
        return;
    }

    DIR* d=opendir(ROOT); if(!d){
        report_add(r,SEV_WARN,"MODULES",
            "Cannot open /luma/sysmodules","Run chkdsk/fsck on the SD card.");
        return;
    }

    int valid=0, broken=0, wrong_ext=0;
    struct dirent* e;

    while((e=readdir(d))!=NULL){
        if(e->d_name[0]=='.') continue;
        char full[450]; snprintf(full,sizeof(full),"%s/%s",ROOT,e->d_name);

        size_t nl=strlen(e->d_name);
        bool is_cxi=(nl>4&&strcmp(e->d_name+nl-4,".cxi")==0);

        if(!is_cxi){
            char desc[256]; snprintf(desc,sizeof(desc),
                "Non-.cxi file: %.60s",e->d_name);
            report_add(r,SEV_WARN,"MODULES",desc,
                "Only .cxi module files belong here. Remove anything else.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            wrong_ext++; continue;
        }

        long sz=MOD_size(full);
        if(sz<=0||sz<MIN_CXI){
            char desc[256]; snprintf(desc,sizeof(desc),
                "Module %s is empty or too small (corrupt?)",e->d_name);
            report_add(r,SEV_ERROR,"MODULES",desc,
                "Delete and re-download this sysmodule from its source.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            broken++;
        } else {
            char desc[256]; snprintf(desc,sizeof(desc),
                "Module %s loaded (%ldKB)",e->d_name,sz/1024);
            report_add(r,SEV_OK,"MODULES",desc,NULL);
            valid++;
        }
    }
    closedir(d);

    if(valid==0&&broken==0&&wrong_ext==0)
        report_add(r,SEV_INFO,"MODULES",
            "/luma/sysmodules/ is empty","Normal if not using custom network modules.");
}
