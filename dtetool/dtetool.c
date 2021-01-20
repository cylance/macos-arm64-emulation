#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include "dtetool.h"
#include "dtefunc.h"

char *dt_buf = NULL;
size_t dt_size = 0;

int main(int argc, char *argv[]) {
    int c;
    bool pflag = false;
    char *fname_input = NULL;
    char *fname_output = NULL;
    char *fname_diff = NULL;
    extern char *optarg;
    extern int optind;
    fname_input = argv[1];
    while ((c = getopt(argc, argv, "o:d:p")) != -1) {
        switch (c) {
            case 'o':
                fname_output = optarg;
                break;
            case 'd':
                fname_diff = optarg;
                break;
            case 'p':
                pflag = true;
                break;
            case '?':
                print_usage();
                return 1;
            default:
                abort();
        }
    }
    if (!fname_input) {
        print_usage();
        return 1;
    }

    // Read the device tree
    dt_entry *root = new_dte();
    dt_buf = get_file_buf(fname_input, &dt_size);
    if (dt_buf  == NULL)
        return 1;
    if (!read_dt_entry((DTEntry *)dt_buf, root)) {
        printf("ERROR: device tree read failed\n");
        del_dte(root);
        exit(1);
    }

    if (fname_diff) {
        // Apply diff
        FILE *fp = fopen(fname_diff, "r");
        apply_dtb_diff(root, fp);
        fclose(fp);
    }

    if (fname_output) {
        // Rebuild the device tree
        uint32_t fsize = get_dt_size(root);
        int fd = open(fname_output, O_RDWR | O_CREAT | O_TRUNC, 0644);
        ftruncate(fd, fsize);
        char *dst = mmap(NULL, fsize, PROT_READ | PROT_WRITE, 
                MAP_SHARED, fd, 0);
        close(fd);
        build_dt_entry(dst, root);
    }

    // Dump the device tree
    if (pflag)
        print_dte(root, 0);

    del_dte(root);

    return 0;
}
