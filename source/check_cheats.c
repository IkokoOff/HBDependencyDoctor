/**
 * check_cheats.c — Rosalina cheat file validator.
 *
 * Scans /luma/cheats/ for malformed, empty, oversized, or badly
 * named cheat files. Validates the [Section] header and hex code
 * line format used by Rosalina's cheat engine.
 */
#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define CHEAT_MAX (512*1024)
#define CHEAT_PREVIEW 40

static bool C_ishex(char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}

/* Verify a string is exactly 16 hex digits (TitleID format) */
static bool C_is16hex(const char* s){
    if(!s||strlen(s)!=16)return false;
    for(int i=0;i<16;i++) if(!C_ishex(s[i]))return false;
    return true;
}

/* Returns: 0=ok, 1=no_section, 2=bad_code, -1=empty/unreadable */
static int C_validate(const char* path, long sz){
    if(sz==0) return -1;
    FILE* f=fopen(path,"r"); if(!f) return -1;
    char line[256]; int n=0; bool has_sec=false; int ret=0;
    while(fgets(line,sizeof(line),f)&&n<CHEAT_PREVIEW){
        n++;
        int len=(int)strlen(line);
        while(len>0&&(line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==' '))line[--len]='\0';
        if(len==0) continue;
        if(line[0]=='['){
            has_sec=true;
            if(!strchr(line,']')){ret=1;break;}
            continue;
        }
        /* Code line: DDDDDDDD DDDDDDDD */
        char* p=line; while(*p==' '||*p=='\t') p++;
        if(strlen(p)>=9&&p[8]==' '){
            bool ok=true;
            for(int i=0;i<8&&ok;i++) if(!C_ishex(p[i]))ok=false;
            if(!ok){ret=2;break;}
        }
    }
    fclose(f);
    if(!has_sec&&n>0) return 1;
    return ret;
}

void check_cheats(Report* r){
    const char* CHEAT_DIR="sdmc:/luma/cheats";
    struct stat rst;
    if(stat(CHEAT_DIR,&rst)!=0||!S_ISDIR(rst.st_mode)){
        report_add(r,SEV_INFO,"CHEAT","No /luma/cheats/ - cheats not used",
            "Normal. Add <TitleID16>.txt files to use Rosalina cheat codes.");
        return;
    }
    DIR* d=opendir(CHEAT_DIR); if(!d){
        report_add(r,SEV_WARN,"CHEAT","Cannot open /luma/cheats - SD issue",
            "Run chkdsk/fsck on the SD card.");
        return;
    }
    int total=0,valid=0,empty=0,bad=0,malformed=0;
    struct dirent* e;
    while((e=readdir(d))!=NULL){
        if(e->d_name[0]=='.') continue;
        size_t nl=strlen(e->d_name);
        char full[400]; snprintf(full,sizeof(full),"%s/%s",CHEAT_DIR,e->d_name);
        if(nl<5||strcmp(e->d_name+nl-4,".txt")!=0){
            char desc[256]; snprintf(desc,sizeof(desc),"Unexpected file: %.60s",e->d_name);
            report_add(r,SEV_INFO,"CHEAT",desc,"Only <TitleID16>.txt files belong here.");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            continue;
        }
        total++;
        char base[32]; snprintf(base,sizeof(base),"%.*s",(int)(nl-4),e->d_name);
        if(!C_is16hex(base)){
            char desc[256]; snprintf(desc,sizeof(desc),"Bad filename: %s",e->d_name);
            report_add(r,SEV_WARN,"CHEAT",desc,
                "Rename to a 16-digit hex TitleID (e.g. 0004000000030600.txt).");
            report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
            bad++;
        } else {
            struct stat fst; long sz=-1;
            if(stat(full,&fst)==0) sz=(long)fst.st_size;
            if(sz==0){
                char desc[256]; snprintf(desc,sizeof(desc),"%s is empty",e->d_name);
                report_add(r,SEV_WARN,"CHEAT",desc,"Delete the empty cheat file.");
                report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
                empty++;
            } else if(sz>CHEAT_MAX){
                char desc[256]; snprintf(desc,sizeof(desc),
                    "%s is suspiciously large (%ldKB)",e->d_name,sz/1024);
                report_add(r,SEV_WARN,"CHEAT",desc,"Probably a binary file placed here by mistake.");
                report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
                malformed++;
            } else {
                int v=C_validate(full,sz);
                if(v!=0){
                    char desc[256];
                    if(v==1) snprintf(desc,sizeof(desc),"%s: missing [Game Name] header",e->d_name);
                    else snprintf(desc,sizeof(desc),"%s: malformed code line",e->d_name);
                    report_add(r,SEV_WARN,"CHEAT",desc,
                        "Cheat format: [Game Name]\\nCheatName\\nDDDDDDDD DDDDDDDD ...");
                    report_set_autofix(r,AUTOFIX_DELETE_FILE,full,NULL);
                    malformed++;
                } else { valid++; }
            }
        }
    }
    closedir(d);
    if(total==0){
        report_add(r,SEV_INFO,"CHEAT","No cheat files in /luma/cheats/",
            "Add <TitleID>.txt files to use Rosalina cheats.");
    } else {
        char desc[128]; snprintf(desc,sizeof(desc),
            "%d valid / %d total cheat files",valid,total);
        Severity sv=(empty>0||bad>0||malformed>0)?SEV_WARN:SEV_OK;
        report_add(r,sv,"CHEAT",desc,NULL);
    }
}
