#include <stdbool.h>

extern char *dt_buf;
extern size_t dt_size;

#define kPropNameLength 32

// Device tree entry as a linked list
typedef struct dt_entry {
    struct dt_entry *parent;
    struct dt_entry *prev;
    struct dt_entry *next;
    struct dt_property *first_property;
    struct dt_entry *first_child;
    char *name;
} dt_entry;

// Device tree property as a linked list
typedef struct dt_property {
    char name[kPropNameLength];
    uint32_t length;
    char *value;
    struct dt_entry *parent;
    struct dt_property *prev;
    struct dt_property *next;
    bool remove;
} dt_property;

// Apple device tree entry format
typedef struct DTEntry {
    uint32_t nProperties;
    uint32_t nChildren;
} DTEntry;

// Apple device tree property format
typedef struct DTProperty {
    char name[kPropNameLength];
    uint32_t length;
} DTProperty;
