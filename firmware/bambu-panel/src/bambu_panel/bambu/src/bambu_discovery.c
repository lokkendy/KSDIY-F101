#include "bambu_discovery.h"

#include "esp_log.h"
#include "esp_netif.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "bambu_disc";

#define MCAST_ADDR      "239.255.255.250"
#define SSDP_PORT_A     1990
#define SSDP_PORT_B     2021
#define RECV_BUF        2048

static const char *MSEARCH =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST:239.255.255.250:2021\r\n"
    "MAN:\"ssdp:discover\"\r\n"
    "MX:2\r\n"
    "ST:urn:bambulab-com:device:3dprinter:1\r\n"
    "USER-AGENT: KSDIY-BambuPanel/1.0\r\n\r\n";

static void trim_line(char *s)
{
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) {
        s[--n] = 0;
    }
    char *p = s;
    while (*p == ' ') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static void normalize_serial(char *serial, size_t cap)
{
    if (!serial || !cap) return;
    char *cut = strstr(serial, "::");
    if (cut) *cut = 0;
    if (strncasecmp(serial,"uuid:",5)==0) memmove(serial,serial+5,strlen(serial+5)+1);
    trim_line(serial);
    size_t n=strlen(serial);
    if(n<8 || n>=cap){serial[0]=0;return;}
    for(size_t i=0;i<n;++i){
        if(!isalnum((unsigned char)serial[i])){serial[0]=0;return;}
        serial[i]=(char)toupper((unsigned char)serial[i]);
    }
}

static void set_field(const char *key, const char *value, char *ip, size_t ip_cap, char *serial,
                      size_t sn_cap, char *name, size_t name_cap, char *model, size_t model_cap)
{
    if (!key || !value || !value[0]) {
        return;
    }
    if (strcasecmp(key, "location") == 0) {
        strncpy(ip, value, ip_cap - 1);
        ip[ip_cap - 1] = 0;
    } else if (strcasecmp(key, "usn") == 0) {
        strncpy(serial, value, sn_cap - 1);
        serial[sn_cap - 1] = 0;
        normalize_serial(serial, sn_cap);
    } else if (strcasecmp(key, "devname.bambu.com") == 0) {
        strncpy(name, value, name_cap - 1);
        name[name_cap - 1] = 0;
    } else if (strcasecmp(key, "devmodel.bambu.com") == 0) {
        strncpy(model, value, model_cap - 1);
        model[model_cap - 1] = 0;
    }
}

static int find_device(bambu_discovered_t *list, size_t count, const char *serial)
{
    if (!serial[0]) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list[i].serial, serial) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void upsert_device(bambu_discovered_t *list, size_t *count, size_t max, const char *ip,
                          const char *serial, const char *name, const char *dev_model)
{
    if (!serial[0]) {
        return;
    }
    if (!ip[0]) {
        return;
    }
    int idx = find_device(list, *count, serial);
    bambu_discovered_t *d;
    if (idx >= 0) {
        d = &list[idx];
    } else {
        if (*count >= max) {
            return;
        }
        d = &list[(*count)++];
        memset(d, 0, sizeof(*d));
        strncpy(d->serial, serial, sizeof(d->serial) - 1);
    }
    strncpy(d->ip, ip, sizeof(d->ip) - 1);
    if (name[0]) {
        strncpy(d->name, name, sizeof(d->name) - 1);
    }
    if (dev_model[0]) {
        strncpy(d->dev_model, dev_model, sizeof(d->dev_model) - 1);
        d->model = bambu_model_from_devmodel(dev_model);
    } else if (d->model == BAMBU_MODEL_UNKNOWN) {
        d->model = BAMBU_MODEL_OTHER;
    }
}

static void parse_ssdp_payload(bambu_discovered_t *list, size_t *count, size_t max,
                             const char *payload, size_t len, const char *from_ip)
{
    char ip[16] = {0};
    char serial[32] = {0};
    char name[48] = {0};
    char model[32] = {0};
    bool is_bambu=false;

    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, payload, len);
    copy[len] = 0;

    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        trim_line(line);
        if (line[0] == 0) {
            continue;
        }
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = 0;
        const char *key = line;
        const char *val = colon + 1;
        while (*val == ' ') {
            val++;
        }
        if(strcasecmp(key,"devmodel.bambu.com")==0 || strcasecmp(key,"devname.bambu.com")==0 ||
           ((strcasecmp(key,"st")==0 || strcasecmp(key,"nt")==0) &&
            strcasecmp(val,"urn:bambulab-com:device:3dprinter:1")==0)) is_bambu=true;
        set_field(key, val, ip, sizeof(ip), serial, sizeof(serial), name, sizeof(name), model,
                  sizeof(model));
    }
    free(copy);

    struct in_addr source;
    if(!is_bambu || !from_ip || inet_pton(AF_INET,from_ip,&source)!=1 || !source.s_addr) return;
    snprintf(ip,sizeof(ip),"%s",from_ip);

    if (ip[0] && serial[0]) {
        upsert_device(list, count, max, ip, serial, name, model);
    }
}

static bool sta_ip(struct in_addr *out)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    out->s_addr = info.ip.addr;
    return true;
}

static int make_listen_socket(uint16_t port, const struct in_addr *iface)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return -1;
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = port==0 && iface ? iface->s_addr : htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGW(TAG, "bind UDP %u failed: %d", port, errno);
        close(s);
        return -1;
    }

    if (iface && iface->s_addr) {
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, iface, sizeof(*iface));
    }

    struct ip_mreq mreq = {0};
    mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
    mreq.imr_interface.s_addr = iface ? iface->s_addr : htonl(INADDR_ANY);
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        ESP_LOGW(TAG, "multicast join on %u: %d", port, errno);
    }

    /* This BSP comments out the POSIX fcntl -> lwIP mapping. Use the socket
     * API explicitly and fail closed if nonblocking setup is rejected. */
    unsigned long nonblocking=1;
    if(ioctlsocket(s,FIONBIO,&nonblocking)!=0){
        ESP_LOGE(TAG,"UDP nonblocking setup failed: %d",errno);
        close(s);return -1;
    }
    return s;
}

static void send_msearch(int s, const struct in_addr *iface)
{
    if(s<0)return;
    if(iface && iface->s_addr)setsockopt(s,IPPROTO_IP,IP_MULTICAST_IF,iface,sizeof(*iface));
    struct sockaddr_in dest={0};dest.sin_family=AF_INET;dest.sin_addr.s_addr=inet_addr(MCAST_ADDR);
    const uint16_t ports[]={SSDP_PORT_B,SSDP_PORT_A};
    for(unsigned i=0;i<2;++i){
        dest.sin_port=htons(ports[i]);
        sendto(s,MSEARCH,strlen(MSEARCH),0,(struct sockaddr*)&dest,sizeof(dest));
    }
}

static void drain_socket(int s, bambu_discovered_t *out, size_t *found, size_t max)
{
    if (s < 0) {
        return;
    }
    char buf[RECV_BUF];
    for (unsigned packet=0;packet<32;++packet) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(s, buf, sizeof(buf) - 1, MSG_DONTWAIT, (struct sockaddr *)&from, &flen);
        if (n <= 0) {
            break;
        }
        buf[n] = 0;
        char from_ip[16] = {0};
        inet_ntoa_r(from.sin_addr, from_ip, sizeof(from_ip));
        parse_ssdp_payload(out, found, max, buf, (size_t)n, from_ip);
    }
}

esp_err_t bambu_discovery_scan(uint32_t listen_ms, bambu_discovered_t *out, size_t *in_out_count)
{
    if (!out || !in_out_count || *in_out_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct in_addr iface = {0};
    if (!sta_ip(&iface)) {
        ESP_LOGW(TAG, "WiFi STA has no IP yet");
        *in_out_count = 0;
        return ESP_ERR_INVALID_STATE;
    }

    size_t max = *in_out_count;
    size_t found = 0;
    memset(out, 0, max * sizeof(out[0]));

    int sock_b = make_listen_socket(SSDP_PORT_B, &iface);
    int sock_a = make_listen_socket(SSDP_PORT_A, &iface);
    int search = make_listen_socket(0, &iface);

    if (sock_b < 0 && sock_a < 0 && search < 0) {
        ESP_LOGE(TAG, "cannot bind SSDP ports");
        *in_out_count = 0;
        return ESP_FAIL;
    }

    /* Keep the ephemeral source socket open: unicast SSDP replies target it. */
    if(listen_ms<500)listen_ms=500;
    if(listen_ms>30000)listen_ms=30000;
    send_msearch(search, &iface);
    TickType_t last_search=xTaskGetTickCount();

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(listen_ms);

    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        drain_socket(sock_b, out, &found, max);
        drain_socket(sock_a, out, &found, max);
        drain_socket(search, out, &found, max);
        if((TickType_t)(xTaskGetTickCount()-last_search)>=pdMS_TO_TICKS(2000)){
            send_msearch(search,&iface);last_search=xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if(search>=0)close(search);
    if (sock_a >= 0) {
        close(sock_a);
    }
    if (sock_b >= 0) {
        close(sock_b);
    }

    *in_out_count = found;
    ESP_LOGI(TAG, "scan done: %u printer(s)", (unsigned)found);
    return ESP_OK;
}
