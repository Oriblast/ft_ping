#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>  // Pour struct icmphdr
#include <netinet/ip.h>  // Pour struct iphdr
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <math.h>
struct icmp_packet {
    struct icmphdr hdr;
    char data[56]; // 56 octets pour que le total fasse 64 (classique ping)
};

typedef struct s_opts
{
    int h;
    int v;
    int w;
    int W;
    int n;
    int H;
    int c; // nbr of packet receved before exit
}   t_opts;

typedef struct s_ping
{
    char *ip;
    char *hostname;
    int ttl;
    int seq;            // compteur de sequence pour les paquets envoyés
    int sent;           // nombre de paquets envoyés
    int received;       // nombre de paquets reçus
    long min;           // min RTT en ms
    long max;           // max RTT en ms
    long sum;           // somme RTT pour avg
    long sum_sq;        // somme des RTT^2 pour mdev
    int avg;            // RTT moyen en ms (optionnel, peut calculer à la fin)
    int mdev;           // deviation type (optionnel, peut calculer à la fin)
    char *dns; 
    t_opts opts;
} t_ping;
t_ping ping = {0};

/*Explications :

sent et received servent à calculer le % de perte.
min/max/sum/sum_sq servent à calculer min/avg/max/mdev.
seq reste le numéro de séquence ICMP.
avg et mdev peuvent rester dans la structure mais seront calculés au moment du SIGINT.*/
void handle_sigint(int sig)
{
    (void)sig;
    double avg = (ping.received) ? (double)ping.sum / ping.received : 0;
    double mdev = (ping.received) ? sqrt((double)ping.sum_sq / ping.received - avg*avg) : 0;
    
    printf("\n--- %s ping statistics ---\n", ping.hostname);
    printf("%d packets transmitted, %d received, %.1f%% packet loss\n",
           ping.sent, ping.received, 
           (ping.sent > 0) ? 100.0 * (ping.sent - ping.received) / ping.sent : 0);
    printf("rtt min/avg/max/mdev = %ld/%.2f/%ld/%.2f ms\n",
           ping.min, avg, ping.max, mdev);
    
    free(ping.dns);
    _exit(0);
}

int check_addr(char *ip)
{
    char* token = strtok(ip, ".");
    int count = 0;

    while (token != NULL && count < 4)
    {
        token = strtok(NULL, ".");
        count++;
    }
    token = strtok(ip, ".");
    if (count == 4 )
    {
        /*while (token != NULL)    {
            int num = atoi(token);
            if (num < 0 || num > 255) {
                printf("Invalid IP address: %s\n", ip);
                exit(1);             
            }
            count++;
            token = strtok(NULL, ".");
        }*/
        return (4);
    }
    else if (count > 1 && count < 4)
    {
        //printf("probably hostname");
        return(3);
    }
    else
    {
        printf("Invalid IP address: %s\n", ip);
        exit(1);
    }
    return(1);

}
unsigned short checksum(void *b, int len)
{
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char*)buf;

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}
char *find_ip(char *hostname)
{
    struct addrinfo hints, *res, *p;
    static char ipstr[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // IPv4 + IPv6

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        perror("getaddrinfo");
        exit(1);
    }

    for(p = res; p != NULL; p = p->ai_next) {
        void *addr;

        if (p->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else {
            continue; // skip IPv6 si tu veux simple
        }

        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        break;
    }

    freeaddrinfo(res);
    return ipstr;
}

char *find_dns(char *ip)
{
    struct sockaddr_in sa;
    char host[1024];

    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        return NULL;
    }

    int ret = getnameinfo((struct sockaddr *)&sa, sizeof(sa),
                          host, sizeof(host),
                          NULL, 0,
                          0);
    if (ret != 0) {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(ret));
        return NULL;
    }

    return strdup(host); // caller must free
}

int main(int argc, char **argv)
{
    ping.seq = 0;
    ping.sent = 0;
    ping.received = 0;
    ping.min = LONG_MAX;
    ping.max = 0;
    ping.sum = 0;
    int timeOutW = 2;
    ping.sum_sq = 0;
    struct timeval start, end;
   // char packet[64] = {0};
    ping.ip = NULL;
    int global_time = 0;
    double sum_time = 0;
    signal(SIGINT, handle_sigint);
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            ping.opts.h = 1;
        else if (strcmp(argv[i], "-v") == 0)
            ping.opts.v = 1;
        else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc)
        {
            timeOutW = atoi(argv[++i]);
            if (timeOutW <= 0)
            {
                printf("Invalid timeout value: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-n") == 0) {
            ping.opts.n = 1;
            ping.opts.H = 0; // -n overrides -H
        }
        else if (strcmp(argv[i], "-H") == 0) {
            ping.opts.H = 1;
            ping.opts.n = 0; // -H overrides -n
        }
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
        {
            int w = atoi(argv[++i]);
            if (w <= 0)
            {
                printf("Invalid wait time value: %s\n", argv[i]);
                return 1;
            }
            global_time = 1;
            printf("Global time limit set to %d seconds.\n", w);
            ping.opts.w = w;
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            int c = atoi(argv[++i]);
            if (c <= 0)
            {
                printf("Invalid count value: %s\n", argv[i]);
                return 1;
            }
            ping.opts.c = c;
        }
        else
            if (argv[i][0] != '-')
                ping.hostname = argv[i];
    }
    if (!ping.hostname)
    {
        printf("Usage: ft_ping [-?] <destination>\n");
        return 1;
    }
    char *ip_copy = strdup(ping.hostname);
    int check = check_addr(ip_copy);
    free(ip_copy);
    
    ping.ip = find_ip(ping.hostname);
    if (ping.opts.H)
        check = 3; 
    if ((check != 4 && !ping.opts.n) || ping.opts.H) {
        ping.dns = find_dns(ping.ip);
        ping.opts.H = 1;
    }
    if (!ping.ip)
    {
        printf("Usage: ft_ping [-?] <destination>\n");
        return 1;
    }
    if (ping.opts.h)
    {
        printf("Usage: ft_ping [-?] <destination>\n");
        printf("Options:\n");
        printf("  -?    Show this help message\n");
        printf("  -v    Verbose output (debug info)\n");
        printf("  -W <seconds>  Set timeout for each reply (default: 2 seconds)\n");
        printf("  -n    Don't resolve hostnames (numeric output only)\n");
        printf("  -H    Show hostname in output (overrides -n)\n");
        printf("  -w <seconds>  Wait up to <seconds> seconds before exiting\n");
        printf("  -c <count>    Exit after receiving <count> replies\n");
        return 0;
    }
    if (ping.opts.v)
        printf("ping: sock4.fd: 3 (socktype: SOCK_DGRAM), sock6.fd: 4 (socktype: SOCK_DGRAM), hints.ai_family: AF_UNSPEC\n");
    // Préparez le paquet ICMP Echo Request
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);       
    struct icmp_packet packet;
    memset(&packet, 0, sizeof(packet));

    packet.hdr.type = ICMP_ECHO;    // Type 8 = Echo Request
    packet.hdr.code = 0;
    packet.hdr.un.echo.id = getpid() & 0xFFFF;
    packet.hdr.un.echo.sequence = 0;

    // Préparez la structure d'adresse pour envoyer les paquets ICMP
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_port = 0;
    dest.sin_family = AF_INET;
    inet_pton(AF_INET, ping.ip, &dest.sin_addr);

    // Préparez le socket pour recevoir les réponses
    struct sockaddr_in reply_addr;
    socklen_t addr_len = sizeof(reply_addr);
    char recvbuf[1024];

    
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }
    ping.seq = 0;
    
    printf("PING %s (%s) 56(84) bytes of data.\n", ping.hostname, ping.ip);
    // time out recvfrom
    struct timeval tv;
    tv.tv_sec = timeOutW;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ping.opts.v)
        printf("ping: ai->ai_family: AF_INET, ai->ai_canonname: %s\n", ping.hostname);
    double timeLimit = 2000;
    while (1)
    {
        packet.hdr.un.echo.sequence++;
        ping.seq++;
        packet.hdr.checksum = 0;
        packet.hdr.checksum = checksum(&packet, sizeof(packet));
        gettimeofday(&start, NULL);
        if (sendto(sockfd, &packet, sizeof(packet), 0,
           (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            perror("sendto");
            return 1;
        }
        ping.sent++;
        addr_len = sizeof(reply_addr);
        ssize_t n = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0,
                    (struct sockaddr *)&reply_addr, &addr_len);
        gettimeofday(&end, NULL);
        // printf("Debug: Preparing packet with sequence %d\n", ping.seq + 1);
        if (n < 0) {
            perror("recvfrom");
        } else {
            struct iphdr *ip = (struct iphdr *)recvbuf;
            struct icmphdr *icmp = (struct icmphdr *)(recvbuf + ip->ihl * 4);

            if (icmp->type == ICMP_ECHOREPLY && icmp->un.echo.id == (getpid() & 0xFFFF)) {
                char ipstr[INET_ADDRSTRLEN];
                ping.ttl = ip->ttl;
                inet_ntop(AF_INET, &reply_addr.sin_addr, ipstr, sizeof(ipstr));
                double rtt = (end.tv_sec - start.tv_sec) * 1000.0
                    + (end.tv_usec - start.tv_usec) / 1000.0;
                sum_time += rtt + 1000;
                if (timeLimit > rtt)
                    timeLimit = rtt;
                if (ping.opts.H)
                    printf("%zd bytes from %s (%s): icmp_seq=%d ttl=%d time=%.1f ms\n", 
                    n - ip->ihl*4, ping.dns, ipstr, icmp->un.echo.sequence, ping.ttl, rtt);
                else
                    printf("%zd bytes from %s: icmp_seq=%d ttl=%d time=%.1f ms\n", 
                        n - ip->ihl*4, ipstr, icmp->un.echo.sequence, ping.ttl, rtt);
                if (ping.opts.v && rtt > timeLimit * 1.5)
                    printf("Warning: RTT %.1f ms exceeds time limit\n", rtt);

                ping.received++;
                ping.sum += rtt;
                ping.sum_sq += rtt * rtt;
                if (rtt < ping.min) ping.min = rtt;
                if (rtt > ping.max) ping.max = rtt;
            }
            sleep(1);
        }
        if (global_time && sum_time >= ping.opts.w * 1000) {
            printf("Time limit of %d seconds reached.\n", ping.opts.w);
            raise(SIGINT);
        }
        if (ping.opts.c && ping.received >= ping.opts.c) {
            printf("Count limit of %d packets received reached.\n", ping.opts.c);
            raise(SIGINT);
        }
    }
    if (ping.dns != NULL)
        free(ping.dns);
    return 0;
}