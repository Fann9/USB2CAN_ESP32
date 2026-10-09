#include "can_config.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fail_sync, fail_install;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
    if (fail_sync) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
int __real_rename(const char *a,const char *b);
int __wrap_rename(const char *a,const char *b)
{
    if (fail_install && strstr(a,".new")) { errno=EIO; return -1; }
    return __real_rename(a,b);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    char path[256], backup[256], error[160], text[CAN_CONFIG_TEXT_SIZE], bad[1024];
    assert(snprintf(path,sizeof(path),"%s/can.conf",argv[1])<(int)sizeof(path));
    assert(snprintf(backup,sizeof(backup),"%s.bak",path)<(int)sizeof(backup));
    can_config_t def=can_config_defaults(), parsed=def, before=def, next=def;
    assert(!can_config_validate(&def,error,sizeof(error)));
    assert(can_config_format(&def,text,sizeof(text))>0);
    assert(!can_config_parse(text,&parsed,error,sizeof(error)));
    assert(can_config_equal(&parsed,&def));
    const char *keys[]={"bitrate","bitrate","tx_gpio","rx_gpio","mode","tx_period_ms","rx_timeout_ms","command_id","version","bogus"};
    const char *values[]={"999999","-1","12","25","selftest","0","4294967296","0x800","3","1"};
    for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
        assert(can_config_set(&parsed,keys[i],values[i],error,sizeof(error))<0);
        assert(can_config_equal(&parsed,&before));
    }
    assert(can_config_set(&parsed,"command_id","0x114",error,sizeof(error))<0);
    assert(can_config_set(&parsed,"bitrate","500000junk",error,sizeof(error))<0);
    snprintf(bad,sizeof(bad),"%sbitrate=500000\n",text);
    assert(can_config_parse(bad,&parsed,error,sizeof(error))<0);
    assert(can_config_equal(&parsed,&before));
    assert(can_config_parse("bitrate=1000000\n",&parsed,error,sizeof(error))<0);
    snprintf(bad,sizeof(bad),"# comment\r\n; comment\n\n%s",text);
    assert(!can_config_parse(bad,&parsed,error,sizeof(error)));
    const char *legacy="version=1\nbitrate=1000000\nmode=listen\ntx_gpio=25\nrx_gpio=26\ntx_period_ms=20\nrx_timeout_ms=500\ncommand_id=0x113\ntelemetry_id=0x114\nstatus_id=0x115\n";
    assert(!can_config_parse(legacy,&parsed,error,sizeof(error)));
    assert(can_config_equal(&parsed,&def));
    snprintf(bad,sizeof(bad),"%smode=normal\n",text);
    assert(can_config_parse(bad,&parsed,error,sizeof(error))<0);
    assert(!can_config_save(path,&def,error,sizeof(error)));
    assert(!can_config_set(&next,"bitrate","500000",error,sizeof(error)));
    fail_sync=1;
    assert(can_config_save(path,&next,error,sizeof(error))<0);
    fail_sync=0;
    assert(!can_config_load(path,&parsed,error,sizeof(error)) && can_config_equal(&parsed,&def));
    fail_install=1;
    assert(can_config_save(path,&next,error,sizeof(error))<0);
    fail_install=0;
    assert(!can_config_load(path,&parsed,error,sizeof(error)) && can_config_equal(&parsed,&def));
    assert(!can_config_save(path,&next,error,sizeof(error)));
    assert(!can_config_load(path,&parsed,error,sizeof(error)) && can_config_equal(&parsed,&next));
    assert(!can_config_load(backup,&parsed,error,sizeof(error)) && can_config_equal(&parsed,&def));
    assert(!unlink(path)); /* Interrupted promotion: boot can recover the backup. */
    assert(can_config_load(path,&parsed,error,sizeof(error))<0);
    assert(!can_config_load(backup,&parsed,error,sizeof(error)));
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite("version=1\0junk",1,14,f)==14); assert(!fclose(f));
    before=parsed;
    assert(can_config_load(path,&parsed,error,sizeof(error))<0 && can_config_equal(&parsed,&before));
    puts("PASS: CAN config parsing, validation, round trip, backup, write/sync failure and rename rollback");
    return 0;
}
