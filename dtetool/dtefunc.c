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
#include "usage.h"

void print_usage() {
    printf(usage_text);
}

// Returns a copy of a string as a new buffer
char *copy_str(char *str, uint32_t *length, bool escape) {
    uint32_t len = strnlen(str, PATH_MAX) + 1;
    // Interpret backslash escapes
    if (escape) {
        char hex[3];
        char *ptr = str;
        char *ptr2 = str;
        while (*ptr) {
            if (*ptr == '\\') {
                ptr++;
                if (*ptr == 'x') {
                    // Interpret hex value
                    ptr++;
                    hex[0] = *ptr++;
                    hex[1] = *ptr++;
                    hex[2] = 0;
                    len -= 3; // Ignore extra characters
                    *ptr2++ = (char)strtoul(hex, NULL, 16);
                }
                else if (*ptr == '0') {
                    // Only \0 is allowed
                    ptr++;
                    len--;
                    *ptr2++ = 0;
                }
                else {
                    printf("ERROR: invalid escape value in string: %s\n", str);
                }
            }
            else {
                *ptr2++ = *ptr++;
            }
        }
        *ptr2 = 0;
    }
    if (length)
        *length = len;
    char *ret = malloc(len);
    memset(ret, 0, len);
    memcpy(ret, str, len);
    return ret;
}

dt_property *new_dtp() {
    dt_property *dtp = malloc(sizeof(dt_property));
    memset(dtp, 0, sizeof(dt_property));
    return dtp;
}

dt_entry *new_dte() {
    dt_entry *dte = malloc(sizeof(dt_entry));
    memset(dte, 0, sizeof(dt_entry));
    return dte;
}

char *new_data(int length) {
    char *data = malloc(length);
    memset(data, 0, length);
    return data;
}

// Calculate total number of children for entry
uint32_t get_num_children(dt_entry *dte) {
    uint32_t num = 0;
    dt_entry *child = dte->first_child;
    while (child) {
        child = child->next;
        num++;
    }
    return num;
}

// Calculate total number of properties for entry
uint32_t get_num_properties(dt_entry *dte) {
    uint32_t num = 0;
    dt_property *property = dte->first_property;
    while (property) {
        property = property->next;
        num++;
    }
    return num;
}

// Calculate total size of entry
uint32_t get_dt_size(dt_entry *dte) {
    uint32_t size = 0;
    size += sizeof(uint32_t); // nProperties
    size += sizeof(uint32_t); // nChildren
    // Scan properties
    dt_property *property = dte->first_property;
    while (property) {
        size += sizeof(property->name);
        size += sizeof(property->length);
        // Align length
        int length = property->length & 0x7fffffff;
        if (length % 4)
            length = length + (4 - (length % 4));
        size += length;
        property = property->next;
    }
    // Scan children
    dt_entry *child = dte->first_child;
    while (child) {
        size += get_dt_size(child);
        child = child->next;
    }
    return size;
}

// Builds an Apple device tree from a linked list root entry
char *build_dt_entry(char *dst, dt_entry *dte) {
    uint32_t num_properties = get_num_properties(dte);
    uint32_t num_children = get_num_children(dte);
    memcpy(dst, &num_properties, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    memcpy(dst, &num_children, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    // Build properties
    dt_property *property = dte->first_property;
    while (property) {
        memcpy(dst, &property->name, kPropNameLength);
        dst += kPropNameLength;
        memcpy(dst, &property->length, sizeof(property->length));
        dst += sizeof(property->length);
        // Align length
        int length = property->length & 0x7fffffff;
        if (length % 4)
            length = length + (4 - (length % 4));
        memcpy(dst, property->value, length);
        dst += length;
        property = property->next;
    }
    // Build children
    dt_entry *child = dte->first_child;
    while (child) {
        dst = build_dt_entry(dst, child);
        child = child->next;
    }
    return dst;
}

// Builds a linked list root entry from an Apple device tree
DTEntry *read_dt_entry(DTEntry *parent, dt_entry *this_parent) {
    // Scan properties
    DTProperty *dtp = (DTProperty *)((char *)parent + sizeof(DTEntry));
    dt_property *prev_dtp = NULL;
    for (int i = 0; i < parent->nProperties; i++) {
        // Align length
        int length = dtp->length & 0x7fffffff;
        if (length >= dt_size) {
            printf("ERROR: Length %d at offset %#lx exceeds file size %ld\n",
                    length, (size_t)&dtp->length - (size_t)dt_buf, dt_size);
            return NULL;
        }
        if (length % 4)
            length = length + (4 - (length % 4));
        // Copy DTProperty to dt_property
        dt_property *this_dtp = new_dtp();
        strncpy(this_dtp->name, dtp->name, kPropNameLength);
        this_dtp->length = dtp->length;
        this_dtp->value = new_data(length);
        memcpy(this_dtp->value, (char *)dtp + sizeof(DTProperty), length);
        this_dtp->parent = this_parent;
        this_dtp->prev = prev_dtp;
        if (!this_parent->first_property)
            this_parent->first_property = this_dtp;
        // Update pointer to previous property
        if (prev_dtp != NULL)
            prev_dtp->next = this_dtp;
        prev_dtp = this_dtp;
        // Reference name in parent entry
        if (!strncmp(this_dtp->name, "name", kPropNameLength))
            this_parent->name = this_dtp->value;
        // Move to next property
        dtp = (DTProperty *)((char *)dtp + sizeof(DTProperty) + length);
    }
    // Scan children
    DTEntry *dte = (DTEntry *)dtp;
    dt_entry *prev_dte = NULL;
    for (int i = 0; i < parent->nChildren; i++) {
        // Copy DTEntry to dt_entry
        dt_entry *this_dte = new_dte();
        this_dte->parent = this_parent;
        this_dte->prev = prev_dte;
        if (!this_parent->first_child)
            this_parent->first_child = this_dte;
        // Update pointer to previous child
        if (prev_dte != NULL)
            prev_dte->next = this_dte;
        prev_dte = this_dte;
        // Move to next child
        dte = read_dt_entry(dte, this_dte);
        if (dte == NULL) {
            printf("ERROR: failed to read entry at offset %#lx\n",
                    (size_t)&dte - (size_t)dt_buf);
            return NULL;
        }
    }
    return dte;
}

// Search for device tree entry in node
dt_entry *find_dte(dt_entry *dte, char *query, bool recursive) {
    dt_entry *ret = NULL;
    dt_entry *child = dte->first_child;
    int len = strnlen(query, kPropNameLength);
    while (child) {
        dt_property *property = child->first_property;
        while (property) {
            if (!strncmp(property->name, "name", kPropNameLength) &&
                    !strncmp(property->value, query, len)) {
                return child;
            }
            property = property->next;
        }
        if (recursive) {
            if ((ret = find_dte(child, query, true)))
                return ret;
        }
        child = child->next;
    }
    return NULL;
}

// Returns a reference to an entry's name property
char *get_dte_name(dt_entry *dte) {
    dt_property *property = dte->first_property;
    while (property) {
        if (!strncmp(property->name, "name", kPropNameLength))
            return property->value;
        property = property->next;
    }
    return NULL;
}

// Search for device tree entry path
dt_entry *find_dte_path(dt_entry *dte, char *path) {
    path = copy_str(path, NULL, false);
    char *token = strtok(path, "/");
    char *name = get_dte_name(dte);
    // Validate root node name
    if (strncmp(name, token, kPropNameLength)) {
        printf("ERROR: path root node '%s' does not match root entry '%s'\n",
                token, name);
        free(path);
        return NULL;
    }
    // Validate path
    token = strtok(NULL, "/");
    while (token) {
        dt_entry *child = dte->first_child;
        dte = NULL;
        while (child) {
            name = get_dte_name(child);
            // Entry name matches path node
            if (!strncmp(name, token, kPropNameLength)) {
                dte = child;
                break;
            }
            child = child->next;
        }
        token = strtok(NULL, "/");
    }
    free(path);
    return dte;
}

// Search for device tree property in node
dt_property *find_dtp(dt_entry *dte, char *query, bool recursive) {
    dt_property *ret = NULL;
    while (dte) {
        dt_property *property = dte->first_property;
        while (property) {
            if (!strncmp(property->name, query, kPropNameLength))
                return property;
            property = property->next;
        }
        if (recursive) {
            if ((ret = find_dtp(dte->first_child, query, true)))
                return ret;
        }
        dte = dte->next;
    }
    return NULL;
}

// Search for device tree property path
dt_property *find_dtp_path(dt_entry *dte, char *path) {
    path = copy_str(path, NULL, false);
    char *token = strtok(path, "/");
    char *name = get_dte_name(dte);
    // Validate root node name
    if (strncmp(name, token, kPropNameLength)) {
        printf("ERROR: path root node '%s' does not match root entry '%s'\n",
                token, name);
        free(path);
        return NULL;
    }
    // Validate path
    token = strtok(NULL, "/");
    while (token) {
        char *query = token;
        token = strtok(NULL, "/");
        if (token) {
            // Next child in path
            dt_entry *child = dte->first_child;
            dte = NULL;
            while (child) {
                name = get_dte_name(child);
                // Entry name matches path node
                if (!strncmp(name, query, kPropNameLength)) {
                    dte = child;
                    break;
                }
                child = child->next;
            }
        }
        else if (dte) {
            // End of path, search for property
            dt_property *property = dte->first_property;
            while (property) {
                if (!strncmp(property->name, query, kPropNameLength)) {
                    free(path);
                    return property;
                }
                property = property->next;
            }
        }
    }
    free(path);
    return NULL;
}

// Update links after new property is inserted
void update_dtp_links(dt_entry *dte, dt_property *dtp) {
    if (dte->first_property) {
        dtp->next = dte->first_property;
        dte->first_property->prev = dtp;
        dte->first_property = dtp;
    }
    else {
        dte->first_property = dtp;
    }
    dtp->parent = dte;
}

// Add a new property to the beginning of an entry
dt_property *add_dtp_uint8(dt_entry *dte, char *name, uint8_t value) {
    dt_property *dtp = new_dtp();
    strncpy(dtp->name, name, kPropNameLength);
    dtp->length = sizeof(uint8_t);
    dtp->value = new_data(sizeof(uint8_t));
    memcpy(dtp->value, &value, sizeof(uint8_t));
    update_dtp_links(dte, dtp);
    return dtp;
}

dt_property *add_dtp_uint16(dt_entry *dte, char *name, uint16_t value) {
    dt_property *dtp = new_dtp();
    strncpy(dtp->name, name, kPropNameLength);
    dtp->length = sizeof(uint16_t);
    dtp->value = new_data(sizeof(uint16_t));
    memcpy(dtp->value, &value, sizeof(uint16_t));
    update_dtp_links(dte, dtp);
    return dtp;
}

dt_property *add_dtp_uint32(dt_entry *dte, char *name, uint32_t value) {
    dt_property *dtp = new_dtp();
    strncpy(dtp->name, name, kPropNameLength);
    dtp->length = sizeof(uint32_t);
    dtp->value = new_data(sizeof(uint32_t));
    memcpy(dtp->value, &value, sizeof(uint32_t));
    update_dtp_links(dte, dtp);
    return dtp;
}

dt_property *add_dtp_uint64(dt_entry *dte, char *name, uint64_t value) {
    dt_property *dtp = new_dtp();
    strncpy(dtp->name, name, kPropNameLength);
    dtp->length = sizeof(uint64_t);
    dtp->value = new_data(sizeof(uint64_t));
    memcpy(dtp->value, &value, sizeof(uint64_t));
    update_dtp_links(dte, dtp);
    return dtp;
}

dt_property *add_dtp_data(dt_entry *dte, char *name, char *data, uint32_t length) {
    dt_property *dtp = new_dtp();
    strncpy(dtp->name, name, kPropNameLength);
    dtp->length = length;
    dtp->value = new_data(length);
    memcpy(dtp->value, data, length);
    update_dtp_links(dte, dtp);
    return dtp;
}

// Adds a new child entry to the specified entry
dt_entry *add_dte(dt_entry *dte, char *name) {
    dt_entry *child = new_dte();
    child->parent = dte;
    
    // Initialize the name and handle properties
    int length = strnlen(name, kPropNameLength);
    add_dtp_data(child, "name", name, length + 1);
    add_dtp_uint32(child, "AAPL,phandle", 0);
    
    // Update the entry links
    if (dte->first_child) {
        child->next = dte->first_child;
        dte->first_child->prev = child;
    }

    // Add new child to beginning of list
    dte->first_child = child;

    return child;
}

dt_entry *add_dte_path(dt_entry *dte, char *path) {
    printf("Adding entry '%s'...\n", path);
    path = copy_str(path, NULL, false);
    char *token = strtok(path, "/");
    // Validate root node name
    char *name = get_dte_name(dte);
    if (strncmp(name, token, kPropNameLength)) {
        printf("ERROR: path root node '%s' does not match root entry '%s'\n",
                token, name);
        free(path);
        return NULL;
    }
    // Validate path
    token = strtok(NULL, "/");
    while (token) {
        dt_entry *child = find_dte(dte, token, false);
        // Add entry if not present
        if (!child)
            child = add_dte(dte, token);
        dte = child;
        token = strtok(NULL, "/");
    }
    return dte;
}

dt_entry *add_dtp_path(dt_entry *dte, char *path, char *data, uint32_t length) {
    printf("Adding property '%s'...\n", path);
    path = copy_str(path, NULL, false);
    char *token = strtok(path, "/");
    // Validate root node name
    char *name = get_dte_name(dte);
    if (strncmp(name, token, kPropNameLength)) {
        printf("ERROR: path root node '%s' does not match root entry '%s'\n",
                token, name);
        free(path);
        return NULL;
    }
    // Validate path
    token = strtok(NULL, "/");
    while (token) {
        char *query = token;
        token = strtok(NULL, "/");
        if (token) {
            dt_entry *child = find_dte(dte, query, false);
            // Add entry if not present
            if (!child)
                child = add_dte(dte, query);
            dte = child;
        }
        else {
            // Add property
            add_dtp_data(dte, query, data, length);
        }
    }
    free(path);
    return dte;
}

bool del_dtp(dt_property *dtp) {
    if (!dtp)
        return false;
    // Between two properties
    if (dtp->prev && dtp->next) {
        dtp->prev->next = dtp->next;
        dtp->next->prev = dtp->prev;
    }
    // Last property
    else if (dtp->prev && !dtp->next) {
        dtp->prev->next = NULL;
    }
    // First property
    else if (!dtp->prev && dtp->next) {
        dtp->next->prev = NULL;
        dtp->parent->first_property = dtp->next;
    }
    // Only property
    else if (!dtp->prev && !dtp->next) {
        dtp->parent->first_property = NULL;
    }
    free(dtp->value);
    free(dtp);
    return true;
}

bool del_dtp_path(dt_entry *dte, char *path) {
    dt_property *dtp = find_dtp_path(dte, path);
    return del_dtp(dtp);
}

void free_dte(dt_entry *dte) {
    // Free properties
    dt_property *next_property = NULL;
    dt_property *property = dte->first_property;
    while (property) {
        next_property = property->next;
        free(property->value);
        free(property);
        property = next_property;
    }
    // Free children
    dt_entry *next_child = NULL;
    dt_entry *child = dte->first_child;
    while (child) {
        next_child = child->next;
        free_dte(child);
        child = next_child;
    }
    free(dte);
}

bool del_dte(dt_entry *dte) {
    if (!dte)
        return false;
    // Between two entries
    if (dte->prev && dte->next) {
        dte->prev->next = dte->next;
        dte->next->prev = dte->prev;
    }
    // Last entry
    else if (dte->prev && !dte->next) {
        dte->prev->next = NULL;
    }
    // First entry
    else if (!dte->prev && dte->next) {
        dte->next->prev = NULL;
        if (dte->parent)
            dte->parent->first_child = dte->next;
    }
    // Only entry
    else if (!dte->prev && !dte->next) {
        if (dte->parent)
            dte->parent->first_child = NULL;
    }
    free_dte(dte);
    return true;
}

// Return true on success, false on failure
bool del_dte_path(dt_entry *dte, char *path) {
    dte = find_dte_path(dte, path);
    return del_dte(dte);
}

void set_dtp_value(dt_property *dtp, uint64_t value, uint32_t length) {
    //free(dtp->value);
    if (length > 0) {
        //dtp->value = malloc(length);
        //memset(dtp->value, 0, length);
        memcpy(dtp->value, &value, length);
    }
}

void set_dtp_data(dt_property *dtp, char *data, uint32_t length) {
    //free(dtp->value);
    if (length > 0) {
        //dtp->value = malloc(length);
        //memset(dtp->value, 0, length);
        memcpy(dtp->value, data, length);
    }
}

char *get_file_buf(char *path, size_t *size) {
    struct stat stbuf;
    if (stat(path, &stbuf)) {
        printf("ERROR: cannot load '%s': No such file or directory.\n", path);
        return NULL;
    }
    if (!S_ISREG(stbuf.st_mode)) {
        printf("ERROR: '%s' is not a regular file.\n", path);
        return NULL;
    }
    size_t fsize = stbuf.st_size;
    int fd = open(path, O_RDONLY);
    char *ret = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
    if (size != NULL)
        *size = fsize;
    close(fd);
    return ret;
}

void apply_diff_mask(dt_entry *dte, dt_property *mask_properties) {
    dt_property *mask_property = NULL;
    while (dte) {
        mask_property = mask_properties;
        // Get compatible property and check
        dt_property *dtp = find_dtp(dte, "compatible", false);
        if (dtp) {
            int length = dtp->length & 0x7fffffff;
            bool remove = true;
            while (mask_property) {
                if (!memcmp(mask_property->value, dtp->value, length)) {
                    // If match, remove if specified, else do not remove
                    remove = mask_property->remove;
                    break;
                }
                mask_property = mask_property->next;
            }
            if (remove) {
                printf("Removing %s/%s %d ", dte->name, dtp->name, length);
                char *ptr = dtp->value;
                for (int i = 0; i < length - 1; i++) {
                    if (*ptr == 0)
                        printf("\\0");
                    else
                        printf("%c", *ptr);
                    ptr++;
                }
                printf("...\n");
                dt_entry *temp = dte->next;
                del_dte(dte);
                dte = temp;
                continue;
            }
        }
        apply_diff_mask(dte->first_child, mask_properties);
        dte = dte->next;
    }
}

// Parse diff array data with specified element size
char *parse_diff_array(char *data, int length, int size) {
    char *dst = malloc(length);
    char *element = strtok(data, ",");
    char *ret = dst;
    int base = 0;
    while (element) {
        if (element[0] == '0' && element[1] == 'x')
            base = 16;
        else
            base = 10;
        uint64_t value = strtoul(element, NULL, base);
        memcpy(ret, &value, size);
        ret += size;
    }
    return ret;
}

void apply_dtb_diff(dt_entry *dte, FILE *fp) {
    char *name = NULL;
    char *path = NULL;
    char *line = NULL;
    char *line_n = NULL;
    char *data = NULL;
    char *type = NULL;
    char *token = NULL;
    char *fbuf = NULL;
    char answer[PATH_MAX];
    uint32_t length;
    uint64_t value;
    size_t len = 0;
    size_t size = 0;
    dt_property *mask_property = NULL;
    dt_property *mask_properties = NULL;
    // Read file line by line
    while (getline(&line_n, &len, fp) != -1) {
        line = strtok(line_n, "\n");
        // Comment
        if (line[0] == '#') {
            free(line_n);
            continue;
        }
        else if (line[0] == '-') {
            if (!del_dte_path(dte, ++line))
                del_dtp_path(dte, line);
        }
        else if (line[0] == '&' || line[0] == '~') {
            name = strtok(++line, " ");
            token = strtok(NULL, " ");
            if (token) {
                // Get property length and data
                length = atoi(token);
                data = strtok(NULL, " ");
                type = strtok(NULL, " ");
                if (data && type) {
                    // Start at first if not initialized
                    if (!mask_properties) {
                        mask_properties = malloc(sizeof(dt_property));
                        mask_property = mask_properties;
                    }
                    else {
                        // Reference next if not at first
                        mask_property->next = malloc(sizeof(dt_property));
                        mask_property = mask_property->next;
                    }
                    mask_property->remove = line[0] == '~';
                    strncpy(mask_property->name, name, kPropNameLength);
                    if (!strncmp(type, "d", 1)) {
                        value = strtoul(data, NULL, 10);
                        mask_property->value = malloc(length);
                        mask_property->length = length;
                        memcpy(mask_property->value, (char *)&value, length);
                    }
                    else if (!strncmp(type, "h", 1)) {
                        value = strtoul(data, NULL, 16);
                        mask_property->value = malloc(length);
                        mask_property->length = length;
                        memcpy(mask_property->value, (char *)&value, length);
                    }
                    else if (!strncmp(type, "s", 1)) {
                        data = copy_str(data, &length, true);
                        mask_property->value = malloc(length);
                        mask_property->length = length;
                        memcpy(mask_property->value, data, length);
                        free(data);
                    }
                }
            }
        }
        else {
            // Get property or entry object
            path = strtok(line, " ");
            token = strtok(NULL, " ");
            if (token) {
                // Get property length and data
                length = atoi(token);
                data = strtok(NULL, " ");
                type = strtok(NULL, " ");
                dt_property *dtp = find_dtp_path(dte, path);
                // Set property value or add new property if not present
                if (type && !strncmp(type, "d", 1)) {
                    value = strtoul(data, NULL, 10);
                    if (!dtp)
                        add_dtp_path(dte, path, (char *)&value, length);
                    else
                        set_dtp_value(dtp, value, length);
                }
                else if (type && !strncmp(type, "h", 1)) {
                    value = strtoul(data, NULL, 16);
                    if (!dtp)
                        add_dtp_path(dte, path, (char *)&value, length);
                    else
                        set_dtp_value(dtp, value, length);
                }
                else if (type && !strncmp(type, "b", 1)) {
                    size = 0;
                    fbuf = NULL;
                    if (!(fbuf = get_file_buf(data, &size))) {
                        printf("WARNING: failed to load file '%s'. Continue? (y/N) ", data);
                        fgets(answer, PATH_MAX, stdin);
                        if (!strncmp("y", answer, 1) || !strncmp("Y", answer, 1)) {
                            printf("Ignoring diff '%s'...\n", data);
                            free(line_n);
                            continue;
                        }
                        else
                            goto cancel;
                    }
                    if (size != length) {
                        printf("WARNING: size of file '%s' (%ld) does not"
                                "match specified length (%d). Continue? (y/N) ",
                                data, size, length);
                        fgets(answer, PATH_MAX, stdin);
                        if (!strncmp("y", answer, 1) || !strncmp("Y", answer, 1)) {
                            printf("Ignoring diff '%s'...\n", data);
                            free(line_n);
                            continue;
                        }
                        else
                            goto cancel;
                    }
                    if (!dtp)
                        add_dtp_path(dte, path, fbuf, length);
                    else
                        set_dtp_data(dtp, fbuf, length);
                }
                else {
                    if (!dtp)
                        add_dtp_path(dte, path, data, length);
                    else
                        set_dtp_data(dtp, data, length);
                }
            }
            else if (!find_dte_path(dte, path)) {
                // Add new entry if not present
                add_dte_path(dte, path);
            }
        }
    }
    // Compare entries to mask entries
    if (mask_properties) {
        apply_diff_mask(dte, mask_properties);
        // Free mask properties
        mask_property = mask_properties;
        dt_property *next_mask_property = NULL;
        while (mask_property) {
            next_mask_property = mask_property->next;
            free(mask_property->value);
            free(mask_property);
            mask_property = next_mask_property;
        }
    }
    free(line_n);
    return;
cancel:
    printf("Diff operation cancelled.\n");
    free(line_n);
}

// Returns a string representing the value of the specified property
char *get_dtp_value(dt_property *dtp) {
    char *ret = malloc(96);
    memset(ret, 0, 96);

    uint32_t length = dtp->length & 0x7fffffff;
    if (length == 0)
        return ret;

    int i = 0;
    char nc = 0;
    char *src = dtp->value;
    bool contains_string = 0;
    for (i = 0; i < length; i++) {
        // Two or more consecutive numbers or letters of same case
        if (i < length - 1) {
            nc = src[i + 1]; 
            // Uppercase
            if (src[i] >= 0x41 && src[i] <= 0x5a &&
                    nc >= 0x41 && nc <= 0x5a) {
                contains_string = true;
            }
            // Lowercase
            if (src[i] >= 0x61 && src[i] <= 0x7a &&
                    nc >= 0x61 && nc <= 0x7a) {
                contains_string = true;
            }
            // Number
            if (src[i] >= 0x30 && src[i] <= 0x39 &&
                    nc >= 0x30 && nc <= 0x39) {
                contains_string = true;
            }
        }
    }

    // Print string
    if (contains_string && length <= 64) {
        for (i = 0; i < length; i++) {
            if (src[i] >= 0x20 && src[i] <= 0x7e)
                sprintf(ret + i, "%c", src[i]);
            else if (i + 1 < length) {
                sprintf(ret + i, ".");
            }
        }
    }
    else {
        // Print LE integer
        if (length == sizeof(uint8_t))
            sprintf(ret, "%d (%#x)", *(uint8_t *)src, *(uint8_t *)src);
        else if (length == sizeof(uint16_t))
            sprintf(ret, "%d (%#x)", *(uint16_t *)src, *(uint16_t *)src);
        else if (length == sizeof(uint32_t))
            sprintf(ret, "%d (%#x)", *(uint32_t *)src, *(uint32_t *)src);
        else if (length == sizeof(uint64_t))
            sprintf(ret, "%ld (%#lx)", *(uint64_t *)src, *(uint64_t *)src);
        else {
            for (i = 0; i < length && i < 16; i++) {
                sprintf(ret + i, "%02x ", (uint8_t)src[i]);
            }
            if (i < length)
                sprintf(ret + i, "...");
        }
    }
    return ret;
}

// Navigates to root then moves back forward to current
void print_branches(dt_entry *current, dt_entry *child) {
    if (current)
        print_branches(current->parent, current);
    // Print branch then navigate forward
    if (child) {
        if (child->next)
            printf("|  ");
        else if (child->parent)
            printf("   ");
    }
}

// Prints the specified device tree entry to stdout
void print_dte(dt_entry *dte, int index) {
    int num_properties = get_num_properties(dte);
    int num_children = get_num_children(dte);
    int index_property = 0;
    int index_child = 0;
    char *dtp_value = NULL;

    // Print node info
    if (dte->parent) {
        if (dte->next)
            printf("+- ");
        else
            printf("`- ");
    }
    printf("{%d} %s (%d properties, %d children):\n", 
            index, dte->name, num_properties, num_children);
    dt_property *property = dte->first_property;

    // Print properties
    while (property) {
        print_branches(dte, NULL);
        if (property->next || dte->first_child)
            printf("+- ");
        else
            printf("`- ");
        printf("[%d] %-30s %5d ", index_property++, property->name, 
                property->length & 0x7fffffff);
        dtp_value = get_dtp_value(property);
        printf("%s\n", dtp_value);
        property = property->next;
        free(dtp_value);
    }

    // Print children
    dt_entry *child = dte->first_child;
    while (child) {
        print_branches(dte, NULL);
        print_dte(child, index_child++);
        child = child->next;
    }
}

