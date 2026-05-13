/**
 * check_hardware.c — Hardware health diagnostics.
 *
 * Checks battery level/charging, circle pad drift, SD card
 * read speed, WiFi connectivity, system clock sanity, and
 * touch screen ghost touches.
 */
#include "checks.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <3ds.h>

void check_hardware(Report* r){

    /* Battery level (0-5 scale via PTMU) */
    {
        u8 batt=0;
        if(R_SUCCEEDED(PTMU_GetBatteryLevel(&batt))){
            if(batt==0){
                report_add(r,SEV_ERROR,"HARDWARE",
                    "Battery critically low or not detected",
                    "Charge your 3DS immediately or replace the battery if it won't hold charge.");
            } else if(batt<=1){
                report_add(r,SEV_WARN,"HARDWARE",
                    "Battery level is very low",
                    "Connect the charger soon to avoid data loss.");
            } else {
                char d[64]; snprintf(d,sizeof(d),"Battery level: %d/5",batt);
                report_add(r,SEV_OK,"HARDWARE",d,NULL);
            }
        }
    }

    /* Battery charging state */
    {
        u8 chg=0;
        if(R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg))&&chg){
            report_add(r,SEV_INFO,"HARDWARE","Console is currently charging",NULL);
        }
    }

    /* AC adapter connected */
    {
        bool ac=false;
        if(R_SUCCEEDED(PTMU_GetAdapterState(&ac))&&ac){
            report_add(r,SEV_INFO,"HARDWARE","AC adapter is connected",NULL);
        }
    }

    /* Circle pad drift detection (should be centered at rest) */
    {
        circlePosition cp;
        hidCircleRead(&cp);
        if(abs(cp.dx)>40||abs(cp.dy)>40){
            char d[128]; snprintf(d,sizeof(d),
                "Circle pad drift detected (dx=%d, dy=%d) - not centered at rest",
                cp.dx,cp.dy);
            report_add(r,SEV_WARN,"HARDWARE",d,
                "Clean around the circle pad or replace it. Hardware repair may be needed.");
        } else {
            report_add(r,SEV_OK,"HARDWARE","Circle pad centered (no drift)",NULL);
        }
    }

    /* SD card read speed test */
    {
        FILE* f=fopen("sdmc:/boot.firm","rb");
        if(f){
            char buf[8192];
            u64 t0=osGetTime();
            size_t total=0;
            for(int i=0;i<128;i++){
                size_t rd=fread(buf,1,sizeof(buf),f);
                total+=rd;
                if(rd<sizeof(buf)) break;
            }
            u64 elapsed=osGetTime()-t0;
            fclose(f);
            if(total>0&&elapsed>0){
                double mb=(double)total/(1024.0*1024.0);
                double sec=(double)elapsed/1000.0;
                double speed=mb/sec;
                char d[128]; snprintf(d,sizeof(d),"SD read speed: %.1f MB/s",speed);
                if(speed<2.0){
                    report_add(r,SEV_WARN,"HARDWARE",d,
                        "SD card is very slow. Replace with a Class 10 / UHS-I card.");
                } else if(speed<5.0){
                    report_add(r,SEV_INFO,"HARDWARE",d,
                        "SD card speed is below average. A faster card may improve load times.");
                } else {
                    report_add(r,SEV_OK,"HARDWARE",d,NULL);
                }
            }
        }
    }

    /* WiFi connectivity */
    {
        u32 wifi=0;
        if(R_SUCCEEDED(ACU_GetWifiStatus(&wifi))){
            if(wifi==0){
                report_add(r,SEV_INFO,"HARDWARE",
                    "WiFi is not connected",
                    "Connect to WiFi for online features, updates, and Pretendo.");
            } else {
                report_add(r,SEV_OK,"HARDWARE","WiFi is connected",NULL);
            }
        }
    }

    /* System clock sanity check */
    {
        time_t t=time(NULL);
        struct tm* tm=localtime(&t);
        if(tm){
            int year=tm->tm_year+1900;
            if(year<2017){
                char d[128]; snprintf(d,sizeof(d),
                    "System clock is set to year %d - likely incorrect",year);
                report_add(r,SEV_WARN,"HARDWARE",d,
                    "Set the correct date/time in System Settings. Wrong clock causes SSL/online errors.");
            } else if(year>2030){
                char d[128]; snprintf(d,sizeof(d),
                    "System clock is set to year %d - likely incorrect",year);
                report_add(r,SEV_WARN,"HARDWARE",d,
                    "Set the correct date/time in System Settings. Wrong clock causes SSL/online errors.");
            } else {
                report_add(r,SEV_OK,"HARDWARE","System clock is within expected range",NULL);
            }
        }
    }

    /* Touch screen ghost touch detection */
    if(hidKeysHeld()&KEY_TOUCH){
        report_add(r,SEV_INFO,"HARDWARE",
            "Touch screen active during scan",
            "Make sure nothing is pressing on the touch screen. Could indicate a hardware fault if persistent.");
    }

    /* NAND free space */
    {
        FS_ArchiveResource res={0};
        if(R_SUCCEEDED(FSUSER_GetArchiveResource(&res,SYSTEM_MEDIATYPE_CTR_NAND))){
            u64 cluster=(u64)res.clusterSize;
            u64 free=(u64)res.freeClusters*cluster;
            u64 tot=(u64)res.totalClusters*cluster;
            if(tot>0){
                u32 pct=(u32)((free*100)/tot);
                char d[128]; snprintf(d,sizeof(d),
                    "CTRNAND: %luMB free / %luMB total (%lu%%)",
                    (u32)(free/1024/1024),(u32)(tot/1024/1024),pct);
                if(pct<5)
                    report_add(r,SEV_ERROR,"HARDWARE",d,"CTRNAND almost full! May cause boot issues.");
                else if(pct<15)
                    report_add(r,SEV_WARN,"HARDWARE",d,"CTRNAND getting full - remove unused system data.");
                else
                    report_add(r,SEV_OK,"HARDWARE",d,NULL);
            }
        }
    }
}
