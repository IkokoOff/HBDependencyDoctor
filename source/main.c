/**
 * main.c — HB Dependency Doctor v1.0
 * 3DS Homebrew Health Analyser with citro2d GUI
 *
 * Top screen  : scrollable issue list with severity badges
 * Bottom screen: selected issue detail + auto-fix prompt + touch buttons
 *
 * Controls (Report view):
 *   UP / DOWN    Navigate issues
 *   LEFT / RIGHT Page up/down
 *   A            Attempt auto-fix (with confirmation)
 *   B            Cancel / back
 *   X            Cycle severity filter (ALL -> WARN+ -> ERR+ -> CRIT)
 *   Y            Export full report to /HBDD_report.txt
 *   SELECT       Toggle fix hints
 *   L            Open hardware test
 *   START        Exit
 *
 * Controls (Hardware Test):
 *   All buttons testable (A/B/X/Y/D-Pad/L/R/START/SELECT/TOUCH)
 *   ZL/ZR shown only on New 3DS models
 *   L+R          Exit hardware test
 *   START        Blocked (so it can be tested)
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "checks.h"

/* ── Color palette (dark theme) ─────────────────────────────────── */
#define COL_BG_TOP    C2D_Color32(13,17,23,255)
#define COL_BG_BOT    C2D_Color32(10,14,20,255)
#define COL_BAR       C2D_Color32(22,27,34,255)
#define COL_SEL_ROW   C2D_Color32(31,58,95,255)
#define COL_WHITE     C2D_Color32(240,240,240,255)
#define COL_GRAY      C2D_Color32(139,148,158,255)
#define COL_DGRAY     C2D_Color32(72,79,88,255)
#define COL_BORDER    C2D_Color32(48,54,61,255)
#define COL_OK_BG     C2D_Color32(27,94,32,255)
#define COL_OK_TXT    C2D_Color32(76,175,80,255)
#define COL_INFO_BG   C2D_Color32(1,82,106,255)
#define COL_INFO_TXT  C2D_Color32(41,182,246,255)
#define COL_WARN_BG   C2D_Color32(78,52,2,255)
#define COL_WARN_TXT  C2D_Color32(255,179,0,255)
#define COL_ERR_BG    C2D_Color32(127,29,29,255)
#define COL_ERR_TXT   C2D_Color32(239,83,80,255)
#define COL_FATAL_BG  C2D_Color32(74,20,140,255)
#define COL_FATAL_TXT C2D_Color32(206,147,216,255)
#define COL_FIXAVAIL  C2D_Color32(255,179,0,255)
#define COL_FIXED_TXT C2D_Color32(129,199,132,255)
#define COL_BTN_BG    C2D_Color32(31,58,95,255)
#define COL_PROG_BG   C2D_Color32(22,27,34,255)
#define COL_PROG_FG   C2D_Color32(41,182,246,255)

/* ── Layout constants ───────────────────────────────────────────── */
#define TOP_W   400.0f
#define TOP_H   240.0f
#define BOT_W   320.0f
#define BOT_H   240.0f
#define M       8.0f
#define TXT_S   0.50f
#define HDR_S   0.60f
#define SM_S    0.42f
#define XS_S    0.38f

/* ── Application state ──────────────────────────────────────────── */
typedef enum { STATE_SCAN, STATE_REPORT, STATE_CONFIRM, STATE_FIXRES, STATE_EXPORT, STATE_HWTEST } AppState;
typedef enum { FILTER_ALL=0, FILTER_WARN, FILTER_ERR, FILTER_FATAL, FILTER_COUNT } FilterMode;
static const char* FLABEL[]={"ALL","WARN+","ERR+","CRIT"};

static Report           g_report;
static C3D_RenderTarget* g_top;
static C3D_RenderTarget* g_bot;
static C2D_TextBuf      g_tbuf;
static AppState         g_state    = STATE_SCAN;
static FilterMode       g_filter  = FILTER_ALL;
static int              g_sel      = 0;
static int              g_scroll   = 0;
static bool             g_hint     = true;
static char             g_expmsg[128];
static int              g_lastfix  = -1;
static int              g_fidx[MAX_ISSUES];
static int              g_fcount   = 0;
static int              g_scanned  = 0;
static const int        ROW_H      = 18;

/* Touch button definitions for the bottom screen */
typedef struct { float x,y,w,h; const char* label; u32 key; } TBtn;
static const TBtn BTNS[]={
    { 10,202,94,16,"[A] Fix",    KEY_A},
    {110,202,94,16,"[B] Back",   KEY_B},
    {210,202,100,16,"[X] Filter",KEY_X},
    { 10,220,94,16,"[Y] Export", KEY_Y},
    {110,220,94,16,"[SEL] Hint", KEY_SELECT},
    {210,220,100,16,"[L] HWTest",KEY_L},
};
#define NBTS 6

/* ── Filter & scroll helpers ────────────────────────────────────── */

/* Rebuild the filtered issue index based on current filter mode */
static void rebuild_filter(void){
    g_fcount=0;
    for(int i=0;i<g_report.count;i++){
        Severity s=g_report.issues[i].severity; bool ok=false;
        switch(g_filter){
            case FILTER_ALL:  ok=true; break;
            case FILTER_WARN: ok=(s>=SEV_WARN); break;
            case FILTER_ERR:  ok=(s>=SEV_ERROR); break;
            case FILTER_FATAL:ok=(s==SEV_FATAL); break;
            default: ok=true;
        }
        if(ok) g_fidx[g_fcount++]=i;
    }
    if(g_sel>=g_fcount) g_sel=(g_fcount>0)?g_fcount-1:0;
}

/* Number of visible rows in the issue list */
static int vis_rows(void){
    return (int)((TOP_H-44.0f-16.0f)/ROW_H);
}

/* Scroll the list so the selected item is visible */
static void ensure_vis(void){
    int vr=vis_rows();
    if(g_sel<g_scroll) g_scroll=g_sel;
    if(g_sel>=g_scroll+vr) g_scroll=g_sel-vr+1;
}

/* ── Severity helpers ────────────────────────────────────────────── */
static u32 svbg(Severity s){
    switch(s){ case SEV_OK:return COL_OK_BG; case SEV_INFO:return COL_INFO_BG;
        case SEV_WARN:return COL_WARN_BG; case SEV_ERROR:return COL_ERR_BG;
        case SEV_FATAL:return COL_FATAL_BG; default:return COL_DGRAY; }
}
static u32 svfg(Severity s){
    switch(s){ case SEV_OK:return COL_OK_TXT; case SEV_INFO:return COL_INFO_TXT;
        case SEV_WARN:return COL_WARN_TXT; case SEV_ERROR:return COL_ERR_TXT;
        case SEV_FATAL:return COL_FATAL_TXT; default:return COL_GRAY; }
}
static const char* svtag(Severity s){
    switch(s){ case SEV_OK:return "OK"; case SEV_INFO:return "INFO";
        case SEV_WARN:return "WARN"; case SEV_ERROR:return "ERR";
        case SEV_FATAL:return "FATAL"; default:return "?"; }
}

/* ── Drawing primitives ─────────────────────────────────────────── */

/* Measure text width in pixels */
static float tw(float s, const char* fmt, ...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextParse(&t,g_tbuf,buf); C2D_TextOptimize(&t);
    float w=0; C2D_TextGetDimensions(&t,s,s,&w,NULL); return w;
}

/* Measure text height in pixels */
static float th(float s, const char* fmt, ...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextParse(&t,g_tbuf,buf); C2D_TextOptimize(&t);
    float h=0; C2D_TextGetDimensions(&t,s,s,NULL,&h); return h;
}

/* Draw text at (x,y) */
static void dt(float x, float y, float s, u32 c, const char* fmt, ...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextParse(&t,g_tbuf,buf); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x,y,0,s,s,c);
}

/* Draw text centered inside a rectangle (rx,ry,rw,rh) */
static void dtc(float rx, float ry, float rw, float rh, float s, u32 c, const char* fmt, ...){
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    C2D_Text t; C2D_TextParse(&t,g_tbuf,buf); C2D_TextOptimize(&t);
    float w=0,h=0; C2D_TextGetDimensions(&t,s,s,&w,&h);
    C2D_DrawText(&t,C2D_WithColor,rx+(rw-w)/2,ry+(rh-h)/2,0,s,s,c);
}

/* Word-wrap text within max_w pixels, printing up to max_lines. Returns Y after last line. */
static float dtw(float x, float y, float max_w, float s, u32 c, const char* text, int max_lines){
    const char* p=text; int lines=0; float cy=y;
    while(*p&&lines<max_lines){
        int len=(int)strlen(p);
        int cut=len;
        if(tw(s,"%s",p)<=max_w){
            dt(x,cy,s,c,"%s",p); cy+=th(s,"W"); break;
        }
        /* Find the longest prefix that fits by searching backwards for a space */
        for(int i=len-1;i>0;i--){
            if(p[i]==' '&&tw(s,"%.*s",i,p)<=max_w){cut=i;break;}
        }
        if(cut==len) cut=len-1;
        dt(x,cy,s,c,"%.*s",cut,p);
        p+=cut; while(*p==' ')p++;
        cy+=th(s,"W"); lines++;
    }
    return cy;
}

/* Draw a rounded rectangle */
static void rrect(float x, float y, float w, float h, float r, u32 c){
    if(r<1||w<2*r||h<2*r){C2D_DrawRectSolid(x,y,0,w,h,c);return;}
    C2D_DrawRectSolid(x+r,y,0,w-2*r,h,c);
    C2D_DrawRectSolid(x,y+r,0,r,h-2*r,c);
    C2D_DrawRectSolid(x+w-r,y+r,0,r,h-2*r,c);
    C2D_DrawCircleSolid(x+r,y+r,0,r,c);
    C2D_DrawCircleSolid(x+w-r,y+r,0,r,c);
    C2D_DrawCircleSolid(x+r,y+h-r,0,r,c);
    C2D_DrawCircleSolid(x+w-r,y+h-r,0,r,c);
}

/* Draw a colored badge (rounded rect + centered label) */
static void badge(float x, float y, float w, float h, u32 bg, u32 fg, const char* lbl){
    rrect(x,y,w,h,3,bg);
    dtc(x,y,w,h,SM_S,fg,"%s",lbl);
}

/* ── Scan screen ────────────────────────────────────────────────── */
static void draw_scan(int cur, int total, const char* mod){
    dt(M,30,HDR_S,COL_WHITE,"HB Dependency Doctor v1.0");
    dt(M,48,SM_S,COL_GRAY,"3DS Homebrew Health Analyser  by Ikoko");
    float bw=TOP_W-2*M;
    dt(M,85,TXT_S,COL_WHITE,"Scanning: %s...",mod);
    C2D_DrawRectSolid(M,108,0,bw,10,COL_PROG_BG);
    C2D_DrawRectSolid(M,108,0,bw*(cur+1)/total,10,COL_PROG_FG);
    dt(M,124,XS_S,COL_GRAY,"Module %d / %d",cur+1,total);
    if(g_scanned>0)
        dt(M,145,SM_S,COL_WARN_TXT,"Issues so far: %d",g_scanned);
}

/* ── Top screen: issue list ─────────────────────────────────────── */
static void render_top(void){
    /* Title bar */
    C2D_DrawRectSolid(0,0,0,TOP_W,22,COL_BAR);
    dt(M,4,HDR_S,COL_WHITE,"HB Dependency Doctor");
    dt(TOP_W-M-tw(XS_S,"by Ikoko"),6,XS_S,COL_GRAY,"by Ikoko");

    /* Stats bar with severity badges + counts */
    float sy=22;
    C2D_DrawRectSolid(0,sy,0,TOP_W,20,COL_BAR);
    float bx=M;
    struct{const char*t;u32 bg,fg;int n;}si[]={
        {"OK",COL_OK_BG,COL_OK_TXT,g_report.ok_count},
        {"INF",COL_INFO_BG,COL_INFO_TXT,g_report.info_count},
        {"WRN",COL_WARN_BG,COL_WARN_TXT,g_report.warn_count},
        {"ERR",COL_ERR_BG,COL_ERR_TXT,g_report.error_count},
        {"CRT",COL_FATAL_BG,COL_FATAL_TXT,g_report.fatal_count},
    };
    for(int i=0;i<5;i++){
        badge(bx,sy+2,30,16,si[i].bg,si[i].fg,si[i].t); bx+=32;
        char nb[8]; snprintf(nb,sizeof(nb),"%d",si[i].n);
        float nw=tw(XS_S,"%s",nb), nh=th(XS_S,"%s",nb);
        dt(bx,sy+2+(16-nh)/2,XS_S,si[i].fg,"%s",nb); bx+=nw+6;
    }
    /* Page / filter info on the right */
    int vr=vis_rows();
    int pg=(g_fcount>0)?g_sel/vr:0;
    int tp=(g_fcount+vr-1)/vr; if(tp<1)tp=1;
    char pbuf[48]; snprintf(pbuf,sizeof(pbuf),"%s  Pg%d/%d",FLABEL[g_filter],pg+1,tp);
    float pw=tw(XS_S,"%s",pbuf);
    dt(TOP_W-M-pw,sy+2+(16-th(XS_S,"W"))/2,XS_S,COL_GRAY,"%s",pbuf);

    float list_y=44.0f;
    float list_h=TOP_H-list_y-16.0f;
    C2D_DrawRectSolid(0,list_y-1,0,TOP_W,1,COL_BORDER);

    if(g_fcount==0){
        if(g_filter==FILTER_ALL)
            dt(M,list_y+20,TXT_S,COL_OK_TXT,"No issues - console looks healthy!");
        else
            dt(M,list_y+20,TXT_S,COL_WARN_TXT,"No issues at this filter. Press X.");
        C2D_DrawRectSolid(0,TOP_H-16,0,TOP_W,16,COL_BAR);
        dt(M,TOP_H-14+1,XS_S,COL_GRAY,"^v:Nav  <>:Page  X:Filter  Y:Export  START:Exit");
        return;
    }

    /* Draw each visible issue row */
    for(int r=0;r<vr;r++){
        int vi=g_scroll+r;
        if(vi>=g_fcount) break;
        Issue* iss=&g_report.issues[g_fidx[vi]];
        float ry=list_y+r*ROW_H;
        bool sel=(vi==g_sel);
        if(sel) C2D_DrawRectSolid(0,ry,0,TOP_W,ROW_H,COL_SEL_ROW);
        u32 bg=svbg(iss->severity), fg=svfg(iss->severity);
        if(iss->fixed){bg=COL_OK_BG;fg=COL_FIXED_TXT;}
        badge(M,ry+2,36,14,bg,fg,svtag(iss->severity));
        float cx=M+40;
        float cat_h=th(SM_S,"W");
        dt(cx,ry+2+(14-cat_h)/2,SM_S,sel?COL_WHITE:COL_GRAY,"%s",iss->category);
        cx+=tw(SM_S,"%s",iss->category)+8;
        /* Truncate description to fit remaining width */
        float desc_w=TOP_W-cx-M-6;
        int maxch=(int)(desc_w/tw(TXT_S,"W"));
        if(maxch<1)maxch=1;
        char desc[128]; snprintf(desc,sizeof(desc),"%.*s",maxch,iss->description);
        float desc_h=th(TXT_S,"W");
        dt(cx,ry+2+(14-desc_h)/2,TXT_S,sel?COL_WHITE:fg,"%s",desc);
    }

    /* Scrollbar when items exceed visible area */
    if(g_fcount>vr){
        float sb_h=list_h-2;
        float thumb_h=sb_h*vr/g_fcount;
        if(thumb_h<6)thumb_h=6;
        float thumb_y=list_y+1+sb_h*g_scroll/g_fcount;
        C2D_DrawRectSolid(TOP_W-4,list_y+1,0,2,sb_h,COL_DGRAY);
        C2D_DrawRectSolid(TOP_W-4,thumb_y,0,2,thumb_h,COL_GRAY);
    }

    /* Navigation bar at bottom */
    C2D_DrawRectSolid(0,TOP_H-16,0,TOP_W,16,COL_BAR);
    dt(M,TOP_H-14+1,XS_S,COL_GRAY,"^v:Nav  <>:Page  X:Filter  Y:Export  START:Exit");
}

/* ── Bottom screen: issue detail ────────────────────────────────── */
static void render_btm(void){
    C2D_DrawRectSolid(0,0,0,BOT_W,22,COL_BAR);
    dt(M,4,HDR_S,COL_WHITE,"Issue Detail");

    if(g_fcount==0){
        dt(M,40,TXT_S,COL_GRAY,"Nothing to show.");
        return;
    }

    Issue* iss=&g_report.issues[g_fidx[g_sel]];
    u32 fg=svfg(iss->severity), bg=svbg(iss->severity);
    float y=28;

    /* Severity badge + category */
    badge(M,y,44,18,bg,fg,svtag(iss->severity));
    dt(M+50,y+2,SM_S,fg,"[%s]",iss->category);
    y+=24;

    /* Description (word-wrapped) */
    y=dtw(M,y,BOT_W-2*M,TXT_S,iss->fixed?COL_FIXED_TXT:fg,iss->description,3);
    y+=6;

    /* Fix hint (if enabled and present) */
    if(g_hint&&iss->fix_hint[0]){
        C2D_DrawRectSolid(M,y,BOT_W-2*M,1,0,COL_BORDER); y+=6;
        dt(M,y,XS_S,COL_INFO_TXT,"FIX:");
        y=dtw(M+26,y,BOT_W-M-26-M,SM_S,COL_GRAY,iss->fix_hint,3);
        y+=6;
    }

    /* Status box */
    float box_w=BOT_W-2*M;
    float lh=th(SM_S,"W");
    if(iss->fixed){
        float bh=2*lh+8;
        rrect(M,y,box_w,bh,4,COL_OK_BG);
        dtw(M+8,y+4,box_w-16,SM_S,COL_FIXED_TXT,iss->fix_result_msg,2);
    } else if(iss->fix_failed){
        float bh=2*lh+8;
        rrect(M,y,box_w,bh,4,COL_ERR_BG);
        dtw(M+8,y+4,box_w-16,SM_S,COL_ERR_TXT,iss->fix_result_msg,2);
    } else if(iss->autofix!=AUTOFIX_NONE){
        rrect(M,y,box_w,lh+8,4,COL_WARN_BG);
        dtc(M,y,box_w,lh+8,SM_S,COL_FIXAVAIL,"Auto-fix available [A]");
    } else if(iss->severity<=SEV_INFO){
        rrect(M,y,box_w,lh+8,4,COL_OK_BG);
        dtc(M,y,box_w,lh+8,SM_S,COL_OK_TXT,"No action needed");
    } else {
        rrect(M,y,box_w,lh+8,4,COL_BAR);
        dtc(M,y,box_w,lh+8,SM_S,COL_GRAY,"Manual fix required");
    }

    /* Touch button bar */
    C2D_DrawRectSolid(0,BOT_H-38,0,BOT_W,38,COL_BAR);
    for(int i=0;i<NBTS;i++){
        const TBtn* b=&BTNS[i];
        rrect(b->x,b->y,b->w,b->h,3,COL_BTN_BG);
        dtc(b->x,b->y,b->w,b->h,XS_S,COL_WHITE,"%s",b->label);
    }
}

/* ── Confirm auto-fix dialog ─────────────────────────────────────── */
static void render_confirm(Issue* iss){
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_BG_BOT);
    rrect(M,8,BOT_W-2*M,26,6,COL_WARN_BG);
    dtc(M,8,BOT_W-2*M,26,HDR_S,COL_WARN_TXT,"CONFIRM AUTO-FIX");

    float y=42;
    dt(M,y,SM_S,COL_GRAY,"Issue:");
    y=dtw(M,y+14,BOT_W-2*M,TXT_S,COL_WHITE,iss->description,3);
    y+=8;

    /* Action type */
    const char* op="APPLY FIX";
    switch(iss->autofix){
        case AUTOFIX_DELETE_FILE:op="DELETE FILE";break;
        case AUTOFIX_DELETE_DIR: op="DELETE DIR";break;
        case AUTOFIX_CREATE_DIR: op="CREATE DIR";break;
        case AUTOFIX_MOVE_FILE:  op="MOVE FILE";break;
        case AUTOFIX_INI_SET:    op="EDIT config.ini";break;
        case AUTOFIX_INI_REMOVE: op="DISABLE in config.ini";break;
        case AUTOFIX_DELETE_ORPHAN_TICKETS: op="DELETE ORPHAN TICKETS";break;
        default:break;
    }
    dt(M,y,SM_S,COL_ERR_TXT,"Action:");
    dt(M+50,y,SM_S,COL_WHITE,"%s",op);
    y+=16;
    dt(M,y,SM_S,COL_GRAY,"Target:");
    dtw(M+50,y,BOT_W-M-50-M,SM_S,COL_GRAY,iss->autofix_arg1,2);

    /* Confirm / cancel buttons */
    y=180;
    rrect(M,y,136,24,4,COL_OK_BG);
    dtc(M,y,136,24,TXT_S,COL_WHITE,"A = Confirm");
    rrect(M+144,y,136,24,4,COL_ERR_BG);
    dtc(M+144,y,136,24,TXT_S,COL_WHITE,"B = Cancel");
}

/* ── Fix result screen ───────────────────────────────────────────── */
static void render_fixres(Issue* iss){
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_BG_BOT);
    if(iss->fixed){
        rrect(M,40,BOT_W-2*M,28,6,COL_OK_BG);
        dtc(M,40,BOT_W-2*M,28,HDR_S,COL_FIXED_TXT,"Fix applied!");
    } else {
        rrect(M,40,BOT_W-2*M,28,6,COL_ERR_BG);
        dtc(M,40,BOT_W-2*M,28,HDR_S,COL_ERR_TXT,"Fix failed.");
    }
    dtw(M,80,BOT_W-2*M,TXT_S,COL_WHITE,iss->fix_result_msg,4);
    dt(M,200,TXT_S,COL_GRAY,"Press any button to continue.");
}

/* ── Interactive hardware test ───────────────────────────────────── */
static void render_hwtest_top(void){
    u32 held=hidKeysHeld();
    circlePosition cp;
    hidCircleRead(&cp);

    C2D_DrawRectSolid(0,0,0,TOP_W,TOP_H,COL_BG_TOP);
    C2D_DrawRectSolid(0,0,0,TOP_W,22,COL_BAR);
    dt(M,4,HDR_S,COL_WHITE,"Hardware Test");

    /* Row 1: L R [ZL ZR on New3DS] | START SEL */
    u8 sysmodel=0;
    CFGU_GetSystemModel(&sysmodel);
    bool isN3DS=(sysmodel==CFG_MODEL_N3DS||sysmodel==CFG_MODEL_N3DSXL||sysmodel==CFG_MODEL_N2DSXL);
    float bx=M;
    struct{u32 k;const char*n;float w;} r1[4];
    int nr1=0;
    r1[nr1].k=KEY_L; r1[nr1].n="L"; r1[nr1].w=24; nr1++;
    r1[nr1].k=KEY_R; r1[nr1].n="R"; r1[nr1].w=24; nr1++;
    if(isN3DS){
        r1[nr1].k=KEY_ZL; r1[nr1].n="ZL"; r1[nr1].w=28; nr1++;
        r1[nr1].k=KEY_ZR; r1[nr1].n="ZR"; r1[nr1].w=28; nr1++;
    }
    for(int i=0;i<nr1;i++){
        bool on=(held&r1[i].k)!=0;
        rrect(bx,28,r1[i].w,14,3,on?COL_OK_BG:COL_BAR);
        dtc(bx,28,r1[i].w,14,XS_S,on?COL_OK_TXT:COL_DGRAY,"%s",r1[i].n);
        bx+=r1[i].w+4;
    }
    struct{u32 k;const char*n;float x,w;} r1r[]={
        {KEY_START,"START",280,38},{KEY_SELECT,"SEL",322,26},
    };
    for(int i=0;i<2;i++){
        bool on=(held&r1r[i].k)!=0;
        rrect(r1r[i].x,28,r1r[i].w,14,3,on?COL_OK_BG:COL_BAR);
        dtc(r1r[i].x,28,r1r[i].w,14,XS_S,on?COL_OK_TXT:COL_DGRAY,"%s",r1r[i].n);
    }

    /* Row 2-4: D-Pad (left) */
    struct{u32 k;const char*n;float x,y;} dp[]={
        {KEY_DUP,   "Up", 22, 0},{KEY_DLEFT, "Lt",  0,16},
        {KEY_DRIGHT,"Rt", 44,16},{KEY_DDOWN, "Dn", 22,32},
    };
    for(int i=0;i<4;i++){
        bool on=(held&dp[i].k)!=0;
        rrect(M+dp[i].x,50+dp[i].y,32,12,3,on?COL_OK_BG:COL_BAR);
        dtc(M+dp[i].x,50+dp[i].y,32,12,XS_S,on?COL_OK_TXT:COL_DGRAY,"%s",dp[i].n);
    }

    /* Row 2-4: ABXY (center) */
    struct{u32 k;const char*n;float x,y;} ab[]={
        {KEY_X,"X",16, 0},{KEY_Y,"Y", 0,16},
        {KEY_A,"A",32,16},{KEY_B,"B",16,32},
    };
    for(int i=0;i<4;i++){
        bool on=(held&ab[i].k)!=0;
        rrect(140+ab[i].x,50+ab[i].y,26,12,3,on?COL_OK_BG:COL_BAR);
        dtc(140+ab[i].x,50+ab[i].y,26,12,XS_S,on?COL_OK_TXT:COL_DGRAY,"%s",ab[i].n);
    }

    /* TOUCH badge */
    {
        bool on=(held&KEY_TOUCH)!=0;
        rrect(240,50,48,12,3,on?COL_OK_BG:COL_BAR);
        dtc(240,50,48,12,XS_S,on?COL_OK_TXT:COL_DGRAY,"TOUCH");
    }

    /* Circle pad section */
    C2D_DrawRectSolid(M,96,0,TOP_W-2*M,1,COL_BORDER);
    dt(M,102,SM_S,COL_WHITE,"Circle Pad: dx=%d  dy=%d",cp.dx,cp.dy);

    /* Circle pad visualizer */
    float ccx=200,ccy=155,cr=50;
    C2D_DrawCircleSolid(ccx,ccy,0,cr,COL_BAR);
    C2D_DrawRectSolid(ccx-1,ccy-cr,0,2,2*cr,COL_BORDER);
    C2D_DrawRectSolid(ccx-cr,ccy-1,0,2*cr,2,COL_BORDER);
    {
        float nx=(float)cp.dx/156.0f, ny=(float)cp.dy/156.0f;
        if(nx>1.0f) nx=1.0f;
        if(nx<-1.0f) nx=-1.0f;
        if(ny>1.0f) ny=1.0f;
        if(ny<-1.0f) ny=-1.0f;
        float pad_r=cr-6.0f;
        C2D_DrawCircleSolid(ccx+nx*pad_r,ccy-ny*pad_r,0,5,COL_PROG_FG);
    }

    dt(M,TOP_H-16,XS_S,COL_GRAY,"L+R = Exit");
}

static void render_hwtest_bot(void){
    u32 held=hidKeysHeld();
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_BG_BOT);
    C2D_DrawRectSolid(0,0,0,BOT_W,22,COL_BAR);
    dtc(0,0,BOT_W,22,HDR_S,COL_WHITE,"Touch Screen");
    if(held&KEY_TOUCH){
        touchPosition tp;
        hidTouchRead(&tp);
        C2D_DrawCircleSolid((float)tp.px,(float)tp.py,0,8,COL_OK_TXT);
        dt(M,BOT_H-16,XS_S,COL_GRAY,"x=%d y=%d  |  L+R=Exit",tp.px,tp.py);
    } else {
        dt(M,BOT_H-16,XS_S,COL_GRAY,"L+R = Exit");
    }
}

/* ── Export result screen ───────────────────────────────────────── */
static void render_export(void){
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_BG_BOT);
    dt(M,20,HDR_S,COL_WHITE,"Export Result");
    dtw(M,50,BOT_W-2*M,TXT_S,COL_WHITE,g_expmsg,4);
    dt(M,200,TXT_S,COL_GRAY,"Press any button to continue.");
}

/* ── Scan phase ──────────────────────────────────────────────────── */
static void run_scan(void){
    const char* MODS[]={"Luma3DS config","3GX plugins","Tickets (AM)","Cheat files",
                         "Homebrew apps","System / SD card","LayeredFS mods","Sysmodules",
                         "Hardware health"};
    void (*CHK[])(Report*)={check_luma,check_plugins,check_tickets,check_cheats,
                            check_homebrews,check_system,check_layeredfs,check_modules,
                            check_hardware};
    int N=9;

    for(int i=0;i<N;i++){
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(g_top,COL_BG_TOP);
        C2D_SceneBegin(g_top);
        C2D_TextBufClear(g_tbuf);
        draw_scan(i,N,MODS[i]);
        C3D_FrameEnd(0);
        CHK[i](&g_report);
        g_scanned=g_report.count;
    }

    /* Wait for user input after scan completes */
    bool wait=true;
    while(wait&&aptMainLoop()){
        hidScanInput();
        if(hidKeysDown()) wait=false;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(g_top,COL_BG_TOP);
        C2D_SceneBegin(g_top);
        C2D_TextBufClear(g_tbuf);
        dt(M,40,HDR_S,COL_OK_TXT,"Scan complete!");
        dt(M,60,TXT_S,COL_WHITE,"OK:%d  INFO:%d  WARN:%d  ERR:%d  FATAL:%d",
            g_report.ok_count,g_report.info_count,g_report.warn_count,
            g_report.error_count,g_report.fatal_count);
        dt(M,80,TXT_S,COL_GRAY,"Total: %d issues",g_report.count);
        dt(M,130,TXT_S,COL_WHITE,"Press any button to continue.");
        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }
    rebuild_filter();
    g_state=STATE_REPORT;
}

/* ── Main entry point ───────────────────────────────────────────── */
int main(void){
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    g_bot=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    g_tbuf=C2D_TextBufNew(8192);

    amInit(); cfguInit(); ptmuInit(); acInit();
    memset(&g_report,0,sizeof(g_report));
    run_scan();

    /* Main event loop */
    while(aptMainLoop()){
        hidScanInput();
        u32 kd=hidKeysDown();
        if(kd&KEY_START && g_state!=STATE_HWTEST) break;

        /* Map touch input to button keys */
        if(kd&KEY_TOUCH){
            touchPosition tp; hidTouchRead(&tp);
            for(int i=0;i<NBTS;i++){
                const TBtn* b=&BTNS[i];
                if(tp.px>=b->x&&tp.px<b->x+b->w&&tp.py>=b->y&&tp.py<b->y+b->h){
                    kd|=b->key; break;
                }
            }
        }

        switch(g_state){
        case STATE_REPORT:{
            int vr=vis_rows();
            if((kd&KEY_DOWN)&&g_fcount>0){
                g_sel=(g_sel+1)%g_fcount; ensure_vis();
            }
            if((kd&KEY_UP)&&g_fcount>0){
                g_sel=g_sel==0?g_fcount-1:g_sel-1; ensure_vis();
            }
            if(kd&KEY_RIGHT){
                int np=g_sel+vr; if(np>=g_fcount)np=g_fcount-1;
                if(np>g_sel){g_sel=np; ensure_vis();}
            }
            if(kd&KEY_LEFT){
                int np=g_sel-vr; if(np<0)np=0;
                if(np<g_sel){g_sel=np; ensure_vis();}
            }
            if(kd&KEY_X){
                g_filter=(FilterMode)((g_filter+1)%FILTER_COUNT);
                rebuild_filter(); g_scroll=0; ensure_vis();
            }
            if(kd&KEY_SELECT) g_hint=!g_hint;
            if((kd&KEY_A)&&g_fcount>0){
                Issue* iss=&g_report.issues[g_fidx[g_sel]];
                if(!iss->fixed&&iss->autofix!=AUTOFIX_NONE)
                    g_state=STATE_CONFIRM;
            }
            if(kd&KEY_Y){
                const char* p="sdmc:/HBDD_report.txt";
                if(export_report(&g_report,p))
                    snprintf(g_expmsg,sizeof(g_expmsg),"Saved to %s",p);
                else
                    snprintf(g_expmsg,sizeof(g_expmsg),"Export FAILED - SD write error.");
                g_state=STATE_EXPORT;
            }
            if(kd&KEY_L) g_state=STATE_HWTEST;
            break;
        }
        case STATE_CONFIRM:{
            Issue* iss=&g_report.issues[g_fidx[g_sel]];
            if(kd&KEY_A){
                g_lastfix=g_fidx[g_sel];
                autofix_run(iss);
                rebuild_filter(); ensure_vis();
                g_state=STATE_FIXRES;
            }
            if(kd&KEY_B) g_state=STATE_REPORT;
            break;
        }
        case STATE_FIXRES:
            if(kd){g_lastfix=-1;g_state=STATE_REPORT;}
            break;
        case STATE_EXPORT:
            if(kd) g_state=STATE_REPORT;
            break;
        case STATE_HWTEST:{
            u32 hh=hidKeysHeld();
            if((hh&KEY_L)&&(hh&KEY_R)) g_state=STATE_REPORT;
            break;
        }
        default: break;
        }

        /* Render both screens */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_tbuf);

        C2D_TargetClear(g_top,COL_BG_TOP);
        C2D_SceneBegin(g_top);
        if(g_state==STATE_HWTEST) render_hwtest_top();
        else if(g_state!=STATE_SCAN) render_top();

        C2D_TargetClear(g_bot,COL_BG_BOT);
        C2D_SceneBegin(g_bot);
        if(g_state==STATE_HWTEST){
            render_hwtest_bot();
        } else {
            switch(g_state){
                case STATE_CONFIRM:{render_confirm(&g_report.issues[g_fidx[g_sel]]);break;}
                case STATE_FIXRES:{
                    int fi=g_lastfix>=0?g_lastfix:(g_fcount>0?g_fidx[g_sel]:0);
                    render_fixres(&g_report.issues[fi]); break;
                }
                case STATE_EXPORT: render_export(); break;
                default: render_btm(); break;
            }
        }
        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }

    /* Cleanup */
    C2D_Fini(); C3D_Fini(); gfxExit();
    cfguExit(); amExit(); ptmuExit(); acExit();
    return 0;
}
