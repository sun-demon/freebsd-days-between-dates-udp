#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define SERVER_BUF 65536
#define SERVER_PORT 9000

int ensure_dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        else return -2;
    }
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return 1;
    }
    int port = SERVER_PORT;
    const char *outdir = "received_files";
    if (port <= 0 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 2; }
    if (ensure_dir_exists(outdir) != 0) { perror("ensure_dir_exists ./received_files/"); return 3; }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 4; }
    struct sockaddr_in srv, cli;
    memset(&srv,0,sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons((uint16_t)port);
    if (bind(sock, (struct sockaddr*)&srv, sizeof srv) != 0) { perror("bind"); close(sock); return 5; }
    printf("Server listening on UDP port %d\n", port);
    while (1) {
        socklen_t clilen = sizeof cli;
        char buf[SERVER_BUF];
        ssize_t r = recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr*)&cli, &clilen);
        if (r < 0) { perror("recvfrom"); continue; }
        char cliaddr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, cliaddr, sizeof cliaddr);
        printf("Received %zd bytes from %s:%d\n", r, cliaddr, ntohs(cli.sin_port));

        /* save to timestamped file */
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char fname[512];
        if (tm) strftime(fname, sizeof fname, "%Y%m%d_%H%M%S", tm);
        else snprintf(fname, sizeof fname, "received");
        char outpath[1024];
        snprintf(outpath, sizeof outpath, "%s/%s_%s_%d.txt", outdir, fname, cliaddr, ntohs(cli.sin_port));
        FILE *f = fopen(outpath, "wb");
        if (!f) { perror("fopen out"); continue; }
        if (fwrite(buf, 1, r, f) != (size_t)r) { perror("fwrite"); }
        fclose(f);
        printf("Saved to %s\n", outpath);
    }
    close(sock);
    return 0;
}
