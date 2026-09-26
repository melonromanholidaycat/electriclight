#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "el_safety.h"
static int checks;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(void)
{
    char why[128];el_output_t o=EL_DEFAULT_OUTPUT;
    CHECK(el_output_validate(&o,why,sizeof why));
    const float invalid[]={0,1,59,-1,NAN,INFINITY};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        o=EL_DEFAULT_OUTPUT;o.ma_per_led=invalid[i];CHECK(!el_output_validate(&o,why,sizeof why));
    }
    o=EL_DEFAULT_OUTPUT;o.current_budget=20000;CHECK(!el_output_validate(&o,why,sizeof why));
    o=EL_DEFAULT_OUTPUT;o.idle_current=-1;CHECK(!el_output_validate(&o,why,sizeof why));
    o=EL_DEFAULT_OUTPUT;o.gamma=NAN;CHECK(!el_output_validate(&o,why,sizeof why));
    CHECK(!el_restore_allowed(EL_RADIO_SAFE));CHECK(el_restore_allowed(EL_RADIO_OFF));
    el_battery_config_t c=EL_BATTERY_DEFAULT;
    CHECK(el_battery_config_valid(&c));CHECK(el_battery_scale(&c,false,0,0)==1);
    c.enabled=true;
    CHECK(el_battery_scale(&c,true,7.2f,1)==1);
    CHECK(fabsf(el_battery_scale(&c,true,6.3f,1)-0.5f)<0.001f);
    CHECK(el_battery_scale(&c,true,5.9f,1)==0);
    CHECK(el_battery_scale(&c,false,7.2f,1)==0);
    CHECK(el_battery_scale(&c,true,NAN,1)==0);
    CHECK(el_battery_scale(&c,true,7.2f,0.2f)==0.2f); // rebound must not brighten
    c.divider_ratio=NAN;CHECK(!el_battery_config_valid(&c));
    c=EL_BATTERY_DEFAULT;c.cutoff_volts=5;CHECK(!el_battery_config_valid(&c));
    uint8_t rgb[156];
    for(int mode=1;mode<=5;mode++) {
        el_diagnostic_frame(rgb,26,mode,0);
        int sum=0;for(int i=0;i<156;i++){CHECK(rgb[i]<=16);sum+=rgb[i];}
        CHECK(sum<=52*3*16);
        if(mode==1)CHECK(sum==0);
        if(mode==2) {CHECK(rgb[0]==16);CHECK(rgb[78]==0);}
        if(mode==3) {CHECK(rgb[0]==0);CHECK(rgb[79]==16);}
        if(mode==4) {CHECK(rgb[2]==16 && rgb[80]==16);CHECK(rgb[5]==0);}
    }
    printf("%d safety checks passed\n",checks);
}
