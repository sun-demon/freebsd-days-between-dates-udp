#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define BUF_SIZE 65507 /* max UDP payload safe size for IPv4 (practical) */
#define SERVER_HOST "freebsd.ddns.net"
#define SERVER_PORT 9000

/* check if leap year */
int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* get days number in month */
int days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

/* check data correct */
int is_valid_date(int year, int month, int day) {
    /* check year - from 1 to 9999 */
    if (year < 1 || year > 9999) {
        return 0;
    }
    
    /* check months */
    if (month < 1 || month > 12) {
        return 0;
    }
    
    /* check days */
    if (day < 1 || day > days_in_month(year, month)) {
        return 0;
    }
    
    return 1;
}

/* parse date in YYYY-MM-DD, return 0 on success */
int parse_date(const char *s, int *y, int *m, int *d) {
    if (!s) return -1;
    
    /* check string length */
    if (strlen(s) < 10) return -1;
    
    /* check format (deffises must be contains in right positions) */
    if (s[4] != '-' || s[7] != '-') return -1;
    
    /* check, that all characters without "-" are digits */
    for (int i = 0; i < 10; i++) {
        if (i != 4 && i != 7) {
            if (s[i] < '0' || s[i] > '9') return -1;
        }
    }
    
    int yy = 0, mm = 0, dd = 0;
    if (sscanf(s, "%d-%d-%d", &yy, &mm, &dd) != 3) return -1;
    
    /* check data correction */
    if (!is_valid_date(yy, mm, dd)) return -1;
    
    *y = yy; *m = mm; *d = dd;
    return 0;
}

/* Fliegel & Van Flandern algorithm (integer) for Gregorian calendar -> JDN */
int64_t date_to_jdn(int y, int m, int d) {
    int64_t a = (14 - m) / 12;
    int64_t y_ = (int64_t)y + 4800 - a;
    int64_t m_ = (int64_t)m + 12*a - 3;
    int64_t jdn = d + (153*m_ + 2)/5 + 365*y_ + y_/4 - y_/100 + y_/400 - 32045;
    return jdn;
}

void print_menu() {
    puts("\n=== Days Between Dates (Client) ===");
    puts("1) Enter two dates from keyboard (YYYY-MM-DD)");
    puts("2) Read two dates from a file (first two non-empty lines)");
    puts("3) Quit");
    printf("Choose: ");
}

int read_two_dates_from_file(const char *filename, char *d1, size_t l1, char *d2, size_t l2) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof line, f) && found < 2) {
        /* trim */
        char *p = line;
        while (*p && (*p==' '||*p=='\t' || *p=='\r' || *p=='\n')) p++;
        if (*p==0) continue;
        
        /* remove trailing whitespace */
        char *end = p + strlen(p) - 1;
        while (end >= p && (*end==' '||*end=='\t' || *end=='\r' || *end=='\n')) { 
            *end = '\0'; 
            end--; 
        }
        
        /* check string length (must be lower 10 symbols for YYYY-MM-DD) */
        if (strlen(p) < 10) continue;
        
        if (found == 0) {
            strncpy(d1, p, l1-1);
            d1[l1-1] = '\0';
        } else {
            strncpy(d2, p, l2-1);
            d2[l2-1] = '\0';
        }
        found++;
    }
    fclose(f);
    return (found == 2) ? 0 : -2;
}

int send_file_over_udp(const char *local_filename) {
    FILE *f = fopen(local_filename, "rb");
    if (!f) { perror("fopen"); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); fprintf(stderr, "File empty or error\n"); return -2; }
    if (sz > BUF_SIZE) { fclose(f); fprintf(stderr, "File too large for single UDP datagram (%ld bytes)\n", sz); return -3; }
    char *buf = malloc(sz);
    if (!buf) { fclose(f); fprintf(stderr, "OOM\n"); return -4; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); fprintf(stderr, "fread error\n"); return -5; }
    fclose(f);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); free(buf); return -6; }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // AF_UNSPEC for IPv6
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[16];
    snprintf(port_str, sizeof port_str, "%d", SERVER_PORT);

    int err = getaddrinfo(SERVER_HOST, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        close(sock); free(buf);
        return -7;
    }

    ssize_t sent = sendto(sock, buf, sz, 0, res->ai_addr, res->ai_addrlen);
    if (sent < 0) perror("sendto");
    else printf("Sent %zd bytes to %s:%d\n", sent, SERVER_HOST, SERVER_PORT);

    freeaddrinfo(res);
    close(sock);
    free(buf);
    return (sent == sz) ? 0 : -8;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return 1;
    }

    while (1) {
        print_menu();
        int choice = 0;
        if (scanf("%d%*c", &choice) != 1) {
            /* Clear buffer output on exception */
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        
        if (choice == 3) break;
        
        char d1[64] = {0}, d2[64] = {0};
        if (choice == 1) {
            printf("Enter date 1 (YYYY-MM-DD): "); 
            if (!fgets(d1, sizeof d1, stdin)) break; 
            d1[strcspn(d1, "\r\n")] = '\0';
            
            printf("Enter date 2 (YYYY-MM-DD): "); 
            if (!fgets(d2, sizeof d2, stdin)) break; 
            d2[strcspn(d2, "\r\n")] = '\0';
        } else if (choice == 2) {
            char fname[256];
            printf("Enter input filename: "); 
            if (!fgets(fname, sizeof fname, stdin)) break; 
            fname[strcspn(fname, "\r\n")] = '\0';
            
            if (read_two_dates_from_file(fname, d1, sizeof d1, d2, sizeof d2) != 0) { 
                fprintf(stderr, "Failed to read two valid dates from file\n"); 
                continue; 
            }
            printf("Read dates: %s and %s\n", d1, d2);
        } else {
            printf("Unknown choice. Please enter 1, 2 or 3.\n"); 
            continue;
        }

        int y1, m1, day1, y2, m2, day2;
        if (parse_date(d1, &y1, &m1, &day1) != 0) {
            fprintf(stderr, "Invalid date 1: %s. Use YYYY-MM-DD format with valid date\n", d1);
            continue;
        }
        if (parse_date(d2, &y2, &m2, &day2) != 0) {
            fprintf(stderr, "Invalid date 2: %s. Use YYYY-MM-DD format with valid date\n", d2);
            continue;
        }
        
        int64_t j1 = date_to_jdn(y1, m1, day1);
        int64_t j2 = date_to_jdn(y2, m2, day2);
        int64_t diff = j2 - j1;
        if (diff < 0) diff = -diff;

        /* prepare output file */
        char outname[256];
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        if (!tm) snprintf(outname, sizeof outname, "output.txt");
        else strftime(outname, sizeof outname, "output_%Y%m%d_%H%M%S.txt", tm);

        FILE *out = fopen(outname, "w");
        if (!out) { perror("fopen output"); continue; }
        fprintf(out, "Input date 1: %s\n", d1);
        fprintf(out, "Input date 2: %s\n", d2);
        fprintf(out, "JDN date1: %" PRId64 "\n", j1);
        fprintf(out, "JDN date2: %" PRId64 "\n", j2);
        fprintf(out, "Days between (absolute): %" PRId64 "\n", diff);
        fclose(out);
        printf("Saved result to %s\n", outname);

        /* send file via UDP to server */
        int sendres = send_file_over_udp(outname);
        if (sendres == 0) printf("File sent successfully.\n");
        else printf("File send error (code %d).\n", sendres);
    }

    puts("Client exiting.");
    return 0;
}