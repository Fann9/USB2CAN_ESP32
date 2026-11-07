#include "can_config.h"
#include "can_config_shell.h"
#include "can_bus.h"
#include "shell_input.h"
#include "flash_files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool can_bus_reload(const can_config_t *cfg) { (void)cfg; return true; }
static uint8_t last_length;
static uint32_t last_period;
bool can_bus_send(uint32_t id,const uint8_t *data,uint8_t length,uint32_t period_ms) { (void)id; (void)data; last_length=length; last_period=period_ms; return true; }
bool can_bus_stop_periodic(void) { last_period=0; return true; }
uint32_t can_bus_period_ms(void) { return last_period; }
uint32_t can_bus_period_id(void) { return 0x113; }
bool can_bus_receive(twai_message_t *frame) { (void)frame; return false; }
bool can_bus_running(void) { return true; }
uint32_t can_bus_dropped(void) { return 0; }
uint32_t can_bus_tx_failed(void) { return 0; }
bool can_bus_measure_hz(uint16_t id,uint32_t sample_ms,uint32_t *frames) { if (id!=0x114 || sample_ms!=1000) return false; *frames=526; return true; }
uint32_t can_bus_seen_count(uint16_t id) { return id==0x114 ? 526 : id==0x115 ? 500 : 0; }
static uint32_t echoed_id;
bool serial_shell_can_echo(uint32_t id) { echoed_id=id; return true; }
static ctshell_ctx_t ctx;
static shell_input_t input;
static char output[8192];
static size_t used;
bool flash_files_ready(void) { return true; } /* Host directory instead of SPI flash. */
static void capture(const char *s,uint16_t n,void *priv)
{
    (void)priv;
    assert(used+n<sizeof(output));
    memcpy(output+used,s,n); used+=n; output[used]=0;
}
static void command(const char *s)
{
    used=0; output[0]=0;
    for (;*s;++s) shell_input_feed(&input,(uint8_t)*s);
    shell_input_feed(&input,'\r');
}
int main(void)
{
    can_config_boot(true);
    ctshell_init(&ctx,(ctshell_io_t){.write=capture},NULL);
    input=(shell_input_t){.ctx=&ctx};
    flash_files_attach(&ctx);
    command("echo first > demo.txt");
    command("echo second >> demo.txt");
    command("touch demo.txt");
    command("cat demo.txt"); assert(strstr(output,"first\r\nsecond\r\n"));
    command("ls"); assert(strstr(output,"demo.txt"));
    command("mkdir sub");
    command("cd sub");
    command("pwd"); assert(strstr(output,"/sub"));
    command("echo child > child.txt");
    command("cd ..");
    command("rm sub"); assert(strstr(output,"cannot remove"));
    command("rm sub/child.txt");
    command("rm sub"); assert(!strstr(output,"cannot remove"));
    command("rm demo.txt");
    command("cat /outside.txt"); assert(strstr(output,"Cannot open file"));
    command("can config"); assert(strstr(output,"bitrate=1000000") && strstr(output,"reload needed: no"));
    command("can config set bitrate 500000"); assert(strstr(output,"Saved /flash/can.conf"));
    command("can config set tx_period_ms 30"); assert(strstr(output,"Saved /flash/can.conf"));
    can_config_t disk; char error[160];
    assert(!can_config_load(CAN_CONFIG_PATH,&disk,error,sizeof(error)));
    assert(disk.bitrate==500000 && disk.tx_period_ms==30);
    command("can config save"); assert(strstr(output,"Usage:"));
    command("can config set mode normal"); assert(strstr(output,"Config rejected"));
    command("can config set telemetry_id 0x114"); assert(strstr(output,"Config rejected"));
    command("can config set status_id 0x115"); assert(strstr(output,"Config rejected"));
    command("can send 0x113 00 00 00 00 00 00 00 00"); assert(strstr(output,"Queued ID 0x113 DLC 8"));
    assert(last_length==8 && last_period==0);
    command("can send 0x123"); assert(strstr(output,"DLC 0") && last_length==0);
    command("can send 0x113 00 0A 01 A5 5A 00 00 00 --20");
    assert(strstr(output,"every 20 ms") && last_length==8 && last_period==20);
    command("can status"); assert(strstr(output,"Periodic TX: ID 0x113 every 20 ms"));
    command("can send stop"); assert(strstr(output,"Periodic send stop queued") && last_period==0);
    command("can send 0x123 00 01 02 03 04 05 06 07 08");
    assert(strstr(output,"DLC must be 0..8"));
    command("can send 0x123 00 --0"); assert(strstr(output,"Period must be"));
    command("can send 0x123 00 --15"); assert(strstr(output,"Period must be"));
    command("can send 0x800 00"); assert(strstr(output,"Invalid 11-bit CAN ID"));
    command("can recv 20"); assert(strstr(output,"0 frame(s) read"));
    command("can status"); assert(strstr(output,"TWAI: running"));
    command("can topic hz /0x114"); assert(strstr(output,"526 Hz"));
    command("can topic list"); assert(strstr(output,"/0x114") && strstr(output,"/0x115"));
    command("can topic echo"); assert(echoed_id==UINT32_MAX);
    command("can topic echo /0x114"); assert(echoed_id==0x114);
    command("can topic echo /0x800"); assert(strstr(output,"Expected topic"));
    command("can topic hz /0x800"); assert(strstr(output,"Expected topic"));
    command("can config"); assert(strstr(output,"reload needed: yes"));
    command("can reload"); assert(strstr(output,"Configuration reloaded") && strstr(output,"TWAI restarted"));
    command("can config"); assert(strstr(output,"reload needed: no"));
    can_config_boot(true); /* reboot model: re-read persisted file */
    command("can config"); assert(strstr(output,"bitrate=500000") && !strstr(output,"bitrate=1000000"));
    FILE *f=fopen(CAN_CONFIG_PATH,"w"); assert(f); fputs("broken\n",f); assert(!fclose(f));
    command("can reload"); assert(strstr(output,"Reload rejected"));
    command("can config"); assert(strstr(output,"bitrate=500000") && strstr(output,"Saved: unavailable"));
    can_config_boot(true); /* corrupt main: recover previous saved bitrate */
    command("can config"); assert(strstr(output,"bitrate=500000"));
    command("can config set bitrate 1000000"); assert(strstr(output,"Saved /flash/can.conf"));
    command("can config set mode garbage"); assert(strstr(output,"Config rejected"));
    can_config_boot(false);
    command("can config set bitrate 500000"); assert(strstr(output,"Filesystem unavailable"));
    puts("PASS: file commands, append, non-destructive touch, directories, CAN commands, reboot and recovery");
}
