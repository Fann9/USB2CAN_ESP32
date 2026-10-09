#include "can_config.h"
#include "can_config_shell.h"
#include "can_bus.h"
#include "ctshell.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "sdkconfig.h"

/* Runs on the shell task and returns when Ctrl+C is received. */
bool serial_shell_can_echo(uint16_t id);

/* Shell-task owned until the TWAI worker is introduced. Loaded is a config
 * snapshot, not a claim that a CAN controller is running. */
static can_config_t loaded;
static bool ready;

#if CONFIG_ESP_CONSOLE_UART_CUSTOM
#define CONSOLE_TX CONFIG_ESP_CONSOLE_UART_TX_GPIO
#define CONSOLE_RX CONFIG_ESP_CONSOLE_UART_RX_GPIO
#else
#define CONSOLE_TX 1
#define CONSOLE_RX 3
#endif

static int validate_board(const can_config_t *c,char *error,size_t size)
{
    if (can_config_validate(c,error,size)) return -1;
    if (c->tx_gpio==CONSOLE_TX || c->rx_gpio==CONSOLE_TX ||
        c->tx_gpio==CONSOLE_RX || c->rx_gpio==CONSOLE_RX) {
        snprintf(error,size,"CAN pins conflict with console UART"); return -1;
    }
    return 0;
}

static int read_config(can_config_t *c,char *error,size_t size)
{
    if (!ready) { snprintf(error,size,"filesystem unavailable"); return -1; }
    return can_config_load(CAN_CONFIG_PATH,c,error,size) || validate_board(c,error,size) ? -1 : 0;
}

void can_config_boot(bool storage_ready)
{
    ready=storage_ready;
    loaded=can_config_defaults();
    char error[160];
    if (ready) {
        can_config_t disk;
        if (!read_config(&disk,error,sizeof(error))) {
            loaded=disk;
            printf("CAN config: loaded %s\n",CAN_CONFIG_PATH);
        } else {
            printf("CAN config: %s; checking backup\n",error);
            if (!can_config_load(CAN_CONFIG_PATH ".bak",&disk,error,sizeof(error)) &&
                !validate_board(&disk,error,sizeof(error))) {
                loaded=disk;
                printf("CAN config: recovered backup (main file left unchanged)\n");
            } else {
                struct stat st;
                bool missing=stat(CAN_CONFIG_PATH,&st)<0 && errno==ENOENT;
                bool backup_missing=stat(CAN_CONFIG_PATH ".bak",&st)<0 && errno==ENOENT;
                if (missing && backup_missing) {
                    if (can_config_save(CAN_CONFIG_PATH,&loaded,error,sizeof(error)))
                        printf("CAN config: default save failed: %s\n",error);
                    else printf("CAN config: default file created\n");
                } else printf("CAN config: invalid files retained; using built-in defaults\n");
            }
        }
    }
    printf("CAN: configuration loaded\n");
}

static void show(const char *label,const can_config_t *c)
{
    char text[CAN_CONFIG_TEXT_SIZE];
    can_config_format(c,text,sizeof(text));
    ctshell_printf("%s:\r\n",label);
    char *line=text;
    while (*line) {
        char *end=strchr(line,'\n');
        if (!end) break;
        *end=0; ctshell_printf("  %s\r\n",line); line=end+1;
    }
}
static int command_config(int argc,char **argv)
{
    char error[160];
    if (argc==1) {
        show("Loaded",&loaded);
        can_config_t disk;
        if (read_config(&disk,error,sizeof(error))) ctshell_printf("Saved: unavailable (%s)\r\n",error);
        else {
            show("Saved /flash/can.conf",&disk);
            ctshell_printf("reload needed: %s\r\n",can_config_equal(&loaded,&disk)?"no":"yes");
        }
        return 0;
    }
    if (argc==4 && !strcmp(argv[1],"set")) {
        if (!ready) { ctshell_printf("Filesystem unavailable\r\n"); return 1; }
        can_config_t next;
        /* Chain edits from the saved file, not the older loaded snapshot.
         * A damaged/missing file can be repaired from the loaded fallback. */
        if (read_config(&next,error,sizeof(error))) next=loaded;
        if (can_config_set(&next,argv[2],argv[3],error,sizeof(error)) || validate_board(&next,error,sizeof(error))) {
            ctshell_printf("Config rejected: %s\r\n",error); return 1;
        }
        if (can_config_save(CAN_CONFIG_PATH,&next,error,sizeof(error))) {
            ctshell_printf("Save failed: %s\r\n",error); return 1;
        }
        ctshell_printf("Saved /flash/can.conf; use can reload or reboot to load\r\n"); return 0;
    }
    ctshell_printf("Usage: can config [set <key> <value>]\r\n"); return 1;
}
static int command_reload(int argc,char **argv)
{
    (void)argv;
    if (argc!=1) { ctshell_printf("Usage: can reload\r\n"); return 1; }
    can_config_t next;
    char error[160];
    if (read_config(&next,error,sizeof(error))) {
        ctshell_printf("Reload rejected: %s; loaded unchanged\r\n",error); return 1;
    }
    if (!can_bus_reload(&next)) {
        ctshell_printf("Reload rejected: TWAI could not start; loaded unchanged\r\n"); return 1;
    }
    loaded=next;
    ctshell_printf("Configuration reloaded; TWAI restarted.\r\n");
    return 0;
}
static int command_send(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"stop")) {
        if (!can_bus_stop_periodic()) {
            ctshell_printf("Cannot queue periodic stop\r\n"); return 1;
        }
        ctshell_printf("Periodic send stop queued\r\n"); return 0;
    }
    if (argc<2 || argc>11) {
        ctshell_printf("Usage: can send <id> [00..FF x0..8] [--period_ms] | can send stop\r\n"); return 1;
    }
    unsigned id;
    char extra;
    if (sscanf(argv[1],"%x%c",&id,&extra)!=1 || id>0x7ff) {
        ctshell_printf("Invalid 11-bit CAN ID\r\n"); return 1;
    }
    uint32_t period_ms=0;
    int data_end=argc;
    if (argc>2 && argv[argc-1][0]=='-' && argv[argc-1][1]=='-') {
        const char *digits=argv[argc-1]+2;
        unsigned value;
        if (!*digits || sscanf(digits,"%u%c",&value,&extra)!=1 ||
            value<portTICK_PERIOD_MS || value>60000 || value%portTICK_PERIOD_MS) {
            ctshell_printf("Period must be %d..60000 ms in %d ms steps\r\n",
                portTICK_PERIOD_MS,portTICK_PERIOD_MS); return 1;
        }
        period_ms=value;
        --data_end;
    }
    if (data_end-2>8) {
        ctshell_printf("Classic CAN DLC must be 0..8 bytes\r\n"); return 1;
    }
    uint8_t bytes[8]={0};
    for (int i=2;i<data_end;++i) {
        unsigned value;
        if (sscanf(argv[i],"%x%c",&value,&extra)!=1 || value>255) {
            ctshell_printf("Invalid hex byte: %s\r\n",argv[i]); return 1;
        }
        bytes[i-2]=value;
    }
    if (!can_bus_send(id,bytes,data_end-2,period_ms)) {
        ctshell_printf("CAN TX queue unavailable/full\r\n"); return 1;
    }
    if (period_ms) ctshell_printf("Queued ID 0x%03X DLC %d every %lu ms\r\n",
        id,data_end-2,(unsigned long)period_ms);
    else ctshell_printf("Queued ID 0x%03X DLC %d\r\n",id,data_end-2);
    return 0;
}

static int command_recv(int argc,char **argv)
{
    unsigned limit=20;
    char extra;
    if (argc==2 && (sscanf(argv[1],"%u%c",&limit,&extra)!=1 || limit<1 || limit>100)) {
        ctshell_printf("Usage: can recv [1..100]\r\n"); return 1;
    }
    if (argc>2) { ctshell_printf("Usage: can recv [1..100]\r\n"); return 1; }
    twai_message_t frame;
    unsigned count=0;
    while (count<limit && can_bus_receive(&frame)) {
        ctshell_printf("%s ID 0x%08lX DLC %u",frame.extd?"EXT":"STD",
            (unsigned long)frame.identifier,frame.data_length_code);
        if (frame.rtr) ctshell_printf(" RTR");
        else for (unsigned i=0;i<frame.data_length_code && i<8;++i)
            ctshell_printf(" %02X",frame.data[i]);
        ctshell_printf("\r\n"); ++count;
    }
    ctshell_printf("%u frame(s) read; dropped %lu\r\n",count,(unsigned long)can_bus_dropped());
    return 0;
}

static int command_status(int argc,char **argv)
{
    (void)argv;
    if (argc!=1) { ctshell_printf("Usage: can status\r\n"); return 1; }
    ctshell_printf("TWAI: %s; software RX dropped: %lu; TX enqueue failed: %lu\r\n",
        can_bus_running()?"running":"stopped",(unsigned long)can_bus_dropped(),
        (unsigned long)can_bus_tx_failed());
    if (can_bus_period_ms()) ctshell_printf("Periodic TX: ID 0x%03lX every %lu ms\r\n",
        (unsigned long)can_bus_period_id(),(unsigned long)can_bus_period_ms());
    else ctshell_printf("Periodic TX: off\r\n");
    return 0;
}

static int command_topic(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"list")) {
        unsigned topics=0;
        for (unsigned id=0;id<=0x7ff;++id) {
            uint32_t count=can_bus_seen_count(id);
            if (count) {
                ctshell_printf("/0x%03X  %lu frame(s)\r\n",id,(unsigned long)count);
                ++topics;
            }
        }
        ctshell_printf("%u standard CAN topic(s) seen since boot\r\n",topics);
        return 0;
    }
    if (argc!=3 || (strcmp(argv[1],"hz") && strcmp(argv[1],"echo")) || argv[2][0]!='/') {
        ctshell_printf("Usage: can topic list | can topic hz /0x114 | can topic echo /0x114\r\n"); return 1;
    }
    unsigned id;
    char extra;
    const char *name=argv[2]+1;
    if (!*name || sscanf(name,"%x%c",&id,&extra)!=1 || id>0x7ff) {
        ctshell_printf("Expected topic /0x000 through /0x7FF\r\n"); return 1;
    }
    if (!strcmp(argv[1],"echo")) {
        if (!serial_shell_can_echo(id)) {
            ctshell_printf("TWAI is not running; cannot echo topic\r\n"); return 1;
        }
        return 0;
    }
    uint32_t frames;
    if (!can_bus_measure_hz(id,1000,&frames)) {
        ctshell_printf("TWAI is not running; cannot measure frequency\r\n"); return 1;
    }
    ctshell_printf("/0x%03X: %lu frames in 1.0 s = %lu Hz\r\n",
        id,(unsigned long)frames,(unsigned long)frames);
    return 0;
}

can_config_t can_config_active(void) { return loaded; }
CTSHELL_EXPORT_CMD(can,NULL,"CAN configuration",CTSHELL_ATTR_MENU);
CTSHELL_EXPORT_SUBCMD(can,config,command_config,"Show or set and persist CAN configuration");
CTSHELL_EXPORT_SUBCMD(can,reload,command_reload,"Load saved CAN configuration");
CTSHELL_EXPORT_SUBCMD(can,send,command_send,"Queue a standard CAN data frame");
CTSHELL_EXPORT_SUBCMD(can,recv,command_recv,"Read received CAN frames");
CTSHELL_EXPORT_SUBCMD(can,status,command_status,"Show CAN driver status");
CTSHELL_EXPORT_SUBCMD(can,topic,command_topic,"List, measure or echo CAN IDs");
