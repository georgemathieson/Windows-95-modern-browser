#include "tc_tags.h"

#include <string.h>

static const char *const names[TC_TAG_COUNT] = {
    "",
#define TC_TAG_NAME(id, name) name,
    TC_TAG_LIST(TC_TAG_NAME)
#undef TC_TAG_NAME
};

tc_tag tc_tag_lookup(const char *name, size_t len)
{
    size_t lo = 1, hi = TC_TAG_COUNT;

    if (len == 0 || len > 15)
        return TC_TAG_UNKNOWN;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strncmp(name, names[mid], len);
        if (cmp == 0 && names[mid][len] != '\0')
            cmp = -1; /* name is a proper prefix, so it sorts first */
        if (cmp == 0)
            return (tc_tag)mid;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return TC_TAG_UNKNOWN;
}

const char *tc_tag_name(tc_tag tag)
{
    return tag > 0 && tag < TC_TAG_COUNT ? names[tag] : "";
}
