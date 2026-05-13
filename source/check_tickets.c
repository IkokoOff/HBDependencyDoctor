/**
 * check_tickets.c — Ticket database analyser.
 *
 * Uses the AM service to enumerate installed tickets and cross-references
 * them with installed titles (SD + NAND). Reports orphaned tickets whose
 * matching title is no longer installed, and zero-ID tickets that suggest
 * a corrupt ticket.db.
 */
#include "checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TKT  3000
#define MAX_TTL  3000

static bool is_user_title(u64 tid){
    u32 hi=(u32)(tid>>32);
    return hi==0x00040000||hi==0x0004008C||hi==0x0004000E;
}

void check_tickets(Report* r){
    u32 count=0;
    Result res=AM_GetTicketCount(&count);
    if(R_FAILED(res)){
        char d[128]; snprintf(d,sizeof(d),"AM_GetTicketCount error 0x%08lX",(unsigned long)res);
        report_add(r,SEV_WARN,"TICKET",d,"Reboot and try again. SD may have issues.");
        return;
    }
    if(count==0){
        report_add(r,SEV_INFO,"TICKET","No tickets installed","Normal for cartridge-only consoles.");
        return;
    }

    char inf[128]; snprintf(inf,sizeof(inf),"%lu ticket(s) installed",count);
    report_add(r,SEV_INFO,"TICKET",inf,NULL);

    u32 rc=(count<MAX_TKT)?count:MAX_TKT;
    u64* tids=(u64*)malloc(rc*sizeof(u64)); if(!tids) return;
    u32 tr=0;
    res=AM_GetTicketList(&tr,rc,0,tids);
    if(R_FAILED(res)||tr==0){ free(tids); return; }

    /* SD titles */
    u32 sc=0; AM_GetTitleCount(MEDIATYPE_SD,&sc); if(sc>MAX_TTL)sc=MAX_TTL;
    u64* stids=NULL; u32 sr=0;
    if(sc>0){ stids=(u64*)malloc(sc*sizeof(u64));
        if(stids) AM_GetTitleList(&sr,MEDIATYPE_SD,sc,stids); }

    /* NAND titles */
    u32 nc=0; AM_GetTitleCount(MEDIATYPE_NAND,&nc); if(nc>MAX_TTL)nc=MAX_TTL;
    u64* ntids=NULL; u32 nr=0;
    if(nc>0){ ntids=(u64*)malloc(nc*sizeof(u64));
        if(ntids) AM_GetTitleList(&nr,MEDIATYPE_NAND,nc,ntids); }

    int orphans=0, invalid=0;
    for(u32 i=0;i<tr;i++){
        u64 tid=tids[i];
        if(tid==0){ invalid++; continue; }
        if(!is_user_title(tid)) continue;
        bool found=false;
        for(u32 j=0;j<sr&&stids;j++) if(stids[j]==tid){found=true;break;}
        if(!found) for(u32 j=0;j<nr&&ntids;j++) if(ntids[j]==tid){found=true;break;}
        if(!found) orphans++;
    }

    if(orphans>0){
        char d[256]; snprintf(d,sizeof(d),
            "%d orphaned ticket(s) - title not installed",orphans);
        report_add(r,SEV_WARN,"TICKET",d,
            "Auto-fix will delete all orphaned tickets via AM service.");
        report_set_autofix(r,AUTOFIX_DELETE_ORPHAN_TICKETS,NULL,NULL);
    } else {
        report_add(r,SEV_OK,"TICKET","All tickets match an installed title",NULL);
    }
    if(invalid>0){
        char d[256]; snprintf(d,sizeof(d),
            "%d zero-ID ticket(s) - ticket.db may be corrupt",invalid);
        report_add(r,SEV_ERROR,"TICKET",d,
            "GodMode9: SYSNAND CTRNAND > ticket.db > Fix/Delete Corrupt Tickets.");
    }
    if(count>MAX_TKT){
        char d[128]; snprintf(d,sizeof(d),"Only first %d/%lu tickets analysed (RAM cap)",MAX_TKT,count);
        report_add(r,SEV_INFO,"TICKET",d,NULL);
    }
    free(tids); if(stids)free(stids); if(ntids)free(ntids);
}
