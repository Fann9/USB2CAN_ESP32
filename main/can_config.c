#include "can_config.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *keys[] = {"version", "bitrate", "tx_gpio", "rx_gpio",
    "tx_period_ms", "rx_timeout_ms", "mode", "command_id", "telemetry_id", "status_id"};

static int fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return -1;
}

can_config_t can_config_defaults(void)
{
    return (can_config_t){.version=2, .bitrate=1000000,
        .tx_gpio=25, .rx_gpio=26, .tx_period_ms=20, .rx_timeout_ms=500};
}

static bool gpio_allowed(uint32_t pin)
{
    /* Exclude UART0, flash, strapping and input-only pins for this board profile. */
    const uint8_t pins[] = {4,13,14,16,17,18,19,21,22,23,25,26,27,32,33};
    for (size_t i=0; i<sizeof(pins); ++i) if (pin==pins[i]) return true;
    return false;
}

int can_config_validate(const can_config_t *c, char *error, size_t size)
{
    if (c->version!=2) return fail(error,size,"version must be 2 (file schema)");
    const uint32_t rates[] = {25000,50000,100000,125000,250000,500000,800000,1000000};
    bool rate_ok=false;
    for (size_t i=0; i<sizeof(rates)/sizeof(rates[0]); ++i) rate_ok |= c->bitrate==rates[i];
    if (!rate_ok) return fail(error,size,"unsupported bitrate (25000..1000000, standard rates)");
    if (!gpio_allowed(c->tx_gpio) || !gpio_allowed(c->rx_gpio) || c->tx_gpio==c->rx_gpio)
        return fail(error,size,"GPIO must be distinct and in 4,13,14,16-19,21-23,25-27,32,33");
    if (c->tx_period_ms<1 || c->tx_period_ms>60000)
        return fail(error,size,"tx_period_ms must be 1..60000");
    if (c->rx_timeout_ms<1 || c->rx_timeout_ms>60000)
        return fail(error,size,"rx_timeout_ms must be 1..60000");
    return 0;
}

static int key_index(const char *key)
{
    for (size_t i=0; i<sizeof(keys)/sizeof(keys[0]); ++i) if (!strcmp(key,keys[i])) return (int)i;
    return -1;
}

static int assign(can_config_t *c, int index, const char *v, char *error, size_t size)
{
    if (index<0) return fail(error,size,"unknown configuration key");
    if (!isdigit((unsigned char)v[0])) return fail(error,size,"expected unsigned decimal or 0x hexadecimal integer");
    char *end;
    errno=0;
    unsigned long n=strtoul(v,&end, (v[0]=='0' && (v[1]=='x'||v[1]=='X')) ? 16 : 10);
    if (errno || *end || end==v || n>UINT32_MAX) return fail(error,size,"invalid or overflowing integer");
    uint32_t *fields[]={&c->version,&c->bitrate,&c->tx_gpio,&c->rx_gpio,
        &c->tx_period_ms,&c->rx_timeout_ms};
    *fields[index]=(uint32_t)n;
    return 0;
}

int can_config_set(can_config_t *c, const char *key, const char *value, char *error, size_t size)
{
    if (key_index(key)>=6) return fail(error,size,"removed configuration key");
    can_config_t next=*c;
    if (assign(&next,key_index(key),value,error,size) || can_config_validate(&next,error,size)) return -1;
    *c=next;
    return 0;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    char *end=s+strlen(s);
    while (end>s && isspace((unsigned char)end[-1])) --end;
    *end=0;
    return s;
}

int can_config_parse(const char *text, can_config_t *c, char *error, size_t size)
{
    can_config_t next=can_config_defaults();
    unsigned seen=0, line=0;
    while (*text) {
        const char *end=strchr(text,'\n');
        size_t n=end ? (size_t)(end-text) : strlen(text);
        char buffer[128];
        ++line;
        if (n>=sizeof(buffer)) return fail(error,size,"configuration line too long");
        memcpy(buffer,text,n); buffer[n]=0;
        text += n+(end!=NULL);
        char *s=trim(buffer);
        if (!*s || *s=='#' || *s==';') continue;
        char *eq=strchr(s,'=');
        if (!eq) { snprintf(error,size,"line %u: expected key=value",line); return -1; }
        *eq=0;
        int index=key_index(trim(s));
        if (index<0 || (seen & (1u<<index))) {
            snprintf(error,size,"line %u: unknown or duplicate key",line); return -1;
        }
        /* Legacy fields are accepted only in complete schema-1 files below.
         * They never control the new generic debugger. */
        if (index<6 && assign(&next,index,trim(eq+1),error,size)) return -1;
        seen |= 1u<<index;
    }
    if (next.version==1 && seen==0x3ff) next.version=2;
    else if (next.version!=2 || seen!=0x3f)
        return fail(error,size,"invalid schema or missing/unexpected configuration fields");
    if (can_config_validate(&next,error,size)) return -1;
    *c=next;
    return 0;
}

int can_config_format(const can_config_t *c, char *text, size_t size)
{
    int n=snprintf(text,size,"version=%"PRIu32"\nbitrate=%"PRIu32"\n"
        "tx_gpio=%"PRIu32"\nrx_gpio=%"PRIu32"\ntx_period_ms=%"PRIu32"\nrx_timeout_ms=%"PRIu32"\n",
        c->version,c->bitrate,c->tx_gpio,c->rx_gpio,c->tx_period_ms,c->rx_timeout_ms);
    return n<0 || (size_t)n>=size ? -1 : n;
}

bool can_config_equal(const can_config_t *a, const can_config_t *b)
{
    char x[CAN_CONFIG_TEXT_SIZE], y[CAN_CONFIG_TEXT_SIZE];
    can_config_format(a,x,sizeof(x)); can_config_format(b,y,sizeof(y));
    return !strcmp(x,y);
}

int can_config_load(const char *path, can_config_t *c, char *error, size_t size)
{
    FILE *f=fopen(path,"rb");
    if (!f) { snprintf(error,size,"open: %s",strerror(errno)); return -1; }
    char text[2049];
    size_t n=fread(text,1,sizeof(text)-1,f);
    bool bad=ferror(f) || n==sizeof(text)-1 || memchr(text,0,n)!=NULL;
    if (fclose(f)) bad=true;
    if (bad) return fail(error,size,"read failed, file too large or embedded NUL");
    text[n]=0;
    return can_config_parse(text,c,error,size);
}

int can_config_save(const char *path, const can_config_t *c, char *error, size_t size)
{
    if (can_config_validate(c,error,size)) return -1;
    char temp[256], backup[256], text[CAN_CONFIG_TEXT_SIZE];
    if (snprintf(temp,sizeof(temp),"%s.new",path)>=(int)sizeof(temp) ||
        snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup))
        return fail(error,size,"configuration path too long");
    int n=can_config_format(c,text,sizeof(text));
    FILE *f=fopen(temp,"wb");
    if (!f) return fail(error,size,"cannot open temporary configuration");
    bool bad=n<0 || fwrite(text,1,(size_t)n,f)!=(size_t)n;
    if (fflush(f)) bad=true;
    if (fsync(fileno(f))) bad=true;
    if (fclose(f)) bad=true;
    if (bad) return fail(error,size,"configuration write/sync failed; original retained");
    can_config_t check;
    if (can_config_load(temp,&check,error,size) || !can_config_equal(&check,c))
        return fail(error,size,"temporary configuration verification failed");
    /* FAT rename cannot overwrite a destination, and is not a power-loss transaction.
     * Preserve the last valid file for boot recovery; never promote an uncommitted .new. */
    bool old_valid=can_config_load(path,&check,error,size)==0;
    if (old_valid) {
        if (unlink(backup) && errno!=ENOENT) return fail(error,size,"cannot rotate backup");
        if (rename(path,backup)) return fail(error,size,"cannot back up configuration");
    } else if (unlink(path) && errno!=ENOENT) {
        return fail(error,size,"cannot replace invalid configuration");
    }
    if (rename(temp,path)) {
        if (old_valid) (void)rename(backup,path);
        return fail(error,size,"cannot install configuration; check file/backup");
    }
    if (can_config_load(path,&check,error,size) || !can_config_equal(&check,c))
        return fail(error,size,"saved configuration verification failed");
    return 0;
}
