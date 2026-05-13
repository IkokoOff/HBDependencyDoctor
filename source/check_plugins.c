/**
 * check_plugins.c — 3GX plugin validator.
 *
 * Scans /luma/plugins/ for corrupt/empty .3gx files, misplaced
 * non-plugin files, and incorrectly named TitleID subdirectories.
 * Also validates the global default.3gx plugin.
 */
#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MIN_3GX 256

static bool P_isdir(const char* p){struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}
static long P_size(const char* p) {struct stat s;return stat(p,&s)==0?(long)s.st_size:-1L;}
static bool P_isTID(const char* n){
    if(!n||strlen(n)!=16)return false;
    for(int i=0;i<16;i++){
        char c=n[i];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
    }
    return true;
}

void check_plugins(Report* r) {
    const char* ROOT="sdmc:/luma/plugins";
    if(!P_isdir(ROOT)){
        report_add(r,SEV_INFO,"PLUGIN","No /luma/plugins/ - no 3GX plugins installed",
            "Normal if you don't use game plugins. Create the folder if needed.");
        return;
    }

    /* default.3gx */
    char defp[350]; snprintf(defp,sizeof(defp),"%s/default.3gx",ROOT);
    struct stat dst; bool has_default=(stat(defp,&dst)==0);
    if(has_default){
        if(dst.st_size<MIN_3GX){
            report_add(r,SEV_ERROR,"PLUGIN","default.3gx is empty/corrupt (global plugin)",
                "Delete the corrupt file and re-download from the plugin's source.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,defp,NULL);
        } else {
            report_add(r,SEV_OK,"PLUGIN","default.3gx valid",NULL);
        }
    }

    DIR* d=opendir(ROOT); if(!d){
        report_add(r,SEV_WARN,"PLUGIN","Cannot open /luma/plugins - SD issue",
            "Run chkdsk/fsck on the SD card.");
        return;
    }

    int valid=0,broken=0; struct dirent* e;
    while((e=readdir(d))!=NULL){
        if(e->d_name[0]=='.') continue;
        char full[450]; snprintf(full,sizeof(full),"%s/%s",ROOT,e->d_name);

        /* File at root level */
        if(!P_isdir(full)){
            if(strcmp(e->d_name,"default.3gx")==0) continue;
            size_t nl=strlen(e->d_name);
            bool is3gx=(nl>4&&strcmp(e->d_name+nl-4,".3gx")==0);
            if(is3gx){
                long sz=P_size(full);
                if(sz<MIN_3GX){
                    char desc[256]; snprintf(desc,sizeof(desc),
                        "Root plugin %s is empty/corrupt",e->d_name);
                    report_add(r,SEV_ERROR,"PLUGIN",desc,
                        "Delete or re-download this .3gx file.");
                    report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
                    broken++;
                } else { valid++; }
            } else {
                char desc[256]; snprintf(desc,sizeof(desc),
                    "Non-.3gx file: %.60s",e->d_name);
                report_add(r,SEV_INFO,"PLUGIN",desc,
                    "Only .3gx files or TitleID folders belong here.");
                report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            }
            continue;
        }

        /* Subdirectory */
        if(!P_isTID(e->d_name)){
            char desc[256]; snprintf(desc,sizeof(desc),
                "Non-TitleID dir: %.60s",e->d_name);
            report_add(r,SEV_WARN,"PLUGIN",desc,
                "Subdirectories must be named with a 16-hex TitleID.");
            continue;
        }

        char plgf[500]; snprintf(plgf,sizeof(plgf),"%s/plugin.3gx",full);
        long sz=P_size(plgf);
        if(sz<0){
            char desc[256]; snprintf(desc,sizeof(desc),
                "Plugin folder %s has no plugin.3gx inside",e->d_name);
            report_add(r,SEV_WARN,"PLUGIN",desc,
                "Add plugin.3gx or remove the empty folder.");
            report_set_autofix(r,AUTOFIX_DELETE_DIR,full,NULL);
            broken++;
        } else if(sz<MIN_3GX){
            char desc[256]; snprintf(desc,sizeof(desc),
                "plugin.3gx for TID %s is empty",e->d_name);
            report_add(r,SEV_ERROR,"PLUGIN",desc,
                "Re-download the plugin from its official source.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,plgf,NULL);
            broken++;
        } else { valid++; }
    }
    closedir(d);

    if(valid>0){
        char desc[128]; snprintf(desc,sizeof(desc),"%d valid plugin(s) installed",valid);
        report_add(r,SEV_OK,"PLUGIN",desc,NULL);
    }
    if(broken==0&&valid==0&&!has_default)
        report_add(r,SEV_INFO,"PLUGIN","/luma/plugins/ is empty - no active plugins",
            "Add .3gx files or TitleID/plugin.3gx to this folder.");
}
