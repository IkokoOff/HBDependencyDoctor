/**
 * check_homebrews.c — Homebrew application health checks.
 *
 * Detects deprecated apps (FreeShop, TWLoader, etc.), verifies
 * essential tools are installed (FBI, Checkpoint, GodMode9, etc.),
 * and checks for leftover install files, empty directories, and
 * missing databases.
 */
#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static bool HB_exists(const char* p){struct stat s;return stat(p,&s)==0;}
static bool HB_isdir(const char* p){struct stat s;return stat(p,&s)==0&&S_ISDIR(s.st_mode);}
static long HB_size(const char* p) {struct stat s;return stat(p,&s)==0?(long)s.st_size:-1L;}

typedef struct{
    const char* path; const char* name;
    const char* issue; const char* fix;
    Severity sev; AutoFixType fixtype;
} BadApp;

static const BadApp BAD[]={
    {"sdmc:/3ds/freeshop","FreeShop",
     "Servers shut down permanently years ago",
     "Remove /3ds/freeshop/ - use hShop instead (Universal-Updater).",
     SEV_WARN,AUTOFIX_DELETE_DIR},
    {"sdmc:/3ds/FreeShop","FreeShop",
     "Servers shut down permanently years ago",
     "Remove /3ds/FreeShop/ - use hShop instead.",
     SEV_WARN,AUTOFIX_DELETE_DIR},
    {"sdmc:/3ds/TWLoader","TWLoader (DS)",
     "Abandoned - replaced by TWiLight Menu++",
     "Uninstall TWLoader; install TWiLight Menu++ from Universal-Updater.",
     SEV_INFO,AUTOFIX_DELETE_DIR},
    {"sdmc:/3ds/hb_appstore","Legacy hb_appstore",
     "Old brand name of Universal-Updater; likely very outdated build",
     "Replace with current Universal-Updater from Universal-Team GitHub.",
     SEV_INFO,AUTOFIX_DELETE_DIR},
    {"sdmc:/nds-bootstrap.nds","nds-bootstrap at SD root",
     "Should not be at root - TWiLight Menu++ manages it internally",
     "Delete nds-bootstrap.nds from root; TWiLight ships its own copy.",
     SEV_WARN,AUTOFIX_DELETE_FILE},
};

typedef struct{ const char* paths[3]; const char* name; const char* why; } Essential;

static const Essential ESSENTIAL[]={
    {{"sdmc:/3ds/FBI.3dsx","sdmc:/3ds/FBI",NULL},
     "FBI","Essential for installing CIA files and managing tickets."},
    {{"sdmc:/3ds/Checkpoint.3dsx","sdmc:/3ds/Checkpoint",NULL},
     "Checkpoint","Backs up and restores 3DS/DS game saves."},
    {{"sdmc:/luma/payloads/GodMode9.firm",NULL,NULL},
     "GodMode9 payload","Emergency rescue tool - hold START on boot."},
    {{"sdmc:/3ds/Universal-Updater.3dsx","sdmc:/3ds/Universal-Updater",NULL},
     "Universal-Updater","Keeps homebrew up to date without a computer."},
    {{"sdmc:/3ds/Anemone3DS.3dsx","sdmc:/3ds/Anemone3DS",NULL},
     "Anemone3DS","Recommended theme manager."},
};

void check_homebrews(Report* r){

    /* Deprecated apps */
    for(size_t i=0;i<sizeof(BAD)/sizeof(BAD[0]);i++){
        const BadApp* a=&BAD[i];
        if(HB_exists(a->path)){
            char d[300]; snprintf(d,sizeof(d),"[%s] %s",a->name,a->issue);
            report_add(r,a->sev,"HOMEBREW",d,a->fix);
            if(a->fixtype != AUTOFIX_NONE)
                report_set_autofix(r,a->fixtype,a->path,NULL);
        }
    }

    /* Essential apps */
    int missing=0;
    for(size_t i=0;i<sizeof(ESSENTIAL)/sizeof(ESSENTIAL[0]);i++){
        const Essential* a=&ESSENTIAL[i];
        bool found=false;
        for(int j=0;j<3&&a->paths[j];j++) if(HB_exists(a->paths[j])){found=true;break;}
        if(!found){
            char d[256]; snprintf(d,sizeof(d),"%s not found on SD",a->name);
            report_add(r,SEV_INFO,"HOMEBREW",d,a->why);
            missing++;
        }
    }
    if(missing==0)
        report_add(r,SEV_OK,"HOMEBREW","All essential homebrew tools detected",NULL);

    /* /3ds/ directory */
    if(!HB_isdir("sdmc:/3ds")){
        report_add(r,SEV_WARN,"HOMEBREW","/3ds/ directory missing from SD root",
            "Create /3ds/ and place .3dsx homebrew files inside.");
        report_set_autofix(r,AUTOFIX_CREATE_DIR,"sdmc:/3ds",NULL);
    }

    /* Leftover CIA install folder */
    if(HB_isdir("sdmc:/cias")){
        report_add(r,SEV_INFO,"HOMEBREW",
            "/cias/ folder found - leftover install files wasting space",
            "After installing CIA files, delete /cias/ to free SD space.");
        report_set_autofix(r,AUTOFIX_DELETE_DIR,"sdmc:/cias",NULL);
    }

    /* Checkpoint missing romfs assets */
    if(HB_exists("sdmc:/3ds/Checkpoint.3dsx")&&!HB_isdir("sdmc:/3ds/Checkpoint")){
        report_add(r,SEV_INFO,"HOMEBREW",
            "Checkpoint.3dsx present but /3ds/Checkpoint/ assets folder missing",
            "Re-install Checkpoint to get full game compatibility database.");
    }

    /* Empty Themes folder */
    if(HB_isdir("sdmc:/Themes")){
        DIR* d=opendir("sdmc:/Themes"); int cnt=0;
        if(d){struct dirent* e;
            while((e=readdir(d))!=NULL) if(e->d_name[0]!='.') cnt++;
            closedir(d);}
        if(cnt==0){
            report_add(r,SEV_INFO,"HOMEBREW","/Themes/ folder is empty",
                "Add theme folders inside /Themes/ or delete it if not used.");
            report_set_autofix(r,AUTOFIX_DELETE_DIR,"sdmc:/Themes",NULL);
        }
    }

    /* Universal-Updater database cache */
    if(HB_isdir("sdmc:/3ds/Universal-Updater")){
        if(!HB_exists("sdmc:/3ds/Universal-Updater/Universal-DB.unistore")){
            report_add(r,SEV_INFO,"HOMEBREW","Universal-Updater has no cached app database",
                "Open Universal-Updater with internet access to download the DB.");
        } else {
            report_add(r,SEV_OK,"HOMEBREW","Universal-Updater with database cache OK",NULL);
        }
    }

    /* Very small Checkpoint binary (old build heuristic) */
    long ck=HB_size("sdmc:/3ds/Checkpoint.3dsx");
    if(ck>0&&ck<400*1024)
        report_add(r,SEV_INFO,"HOMEBREW",
            "Checkpoint.3dsx seems old (small file size)",
            "Update via Universal-Updater for latest save compatibility.");

    /* Pretendo / Nimbus - just inform if missing */
    if(!HB_isdir("sdmc:/3ds/nimbus")&&!HB_exists("sdmc:/3ds/nimbus.3dsx"))
        report_add(r,SEV_INFO,"HOMEBREW",
            "Pretendo (Nimbus) not found - Nintendo Network replacement",
            "Optional: install Nimbus from GitHub/Universal-Updater for online play.");
}
