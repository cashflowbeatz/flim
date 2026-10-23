/* Exercise the actual parser against complete records and corrupted copies. */
#include <assert.h>
#include "../../engine/flim_engine.c"

static char image[FLIM_PROFILE_MAX + 1], scratch[FLIM_PROFILE_MAX + 1];
static unsigned checks;
static int parse(const char *text, size_t n, FlBuild *row) {
    wchar_t build[32]; char sha[65];
    assert(n <= FLIM_PROFILE_MAX);
    memcpy(scratch, text, n);
    checks++;
    return flim_profile_parse(scratch, n, L"25.2.3.5171", row, build, sha);
}
int main(void) {
    FlBuild row = {0}, original; size_t n, i;
    n = sprintf_s(image, sizeof(image), "schema=1\nbuild=25.2.3.5171\nsha256=%064d\nwire=" FLIM_WIRE "\nmsgbox=1\n", 0);
    for (i = 0; i < FLIM_PROFILE_FIELDS; i++) {
        const FlProfileField *f = &flim_profile_fields[i];
        uint64_t v = f->width == 8 ? *(const uint64_t *)((const char *)&k_flBuilds[0] + f->offset) :
                                   *(const uint32_t *)((const char *)&k_flBuilds[0] + f->offset);
        n += sprintf_s(image + n, sizeof(image) - n, "%s=0x%llx\n", f->name, (unsigned long long)v);
    }
    assert(parse(image, n, &row));
    assert(row.REC == k_flBuilds[0].REC && row.PATOP_SPLIT_SLOT == k_flBuilds[0].PATOP_SPLIT_SLOT);
    original = row; original.build = NULL;
    for (i = 0; i < n; i++) {
        /* A complete prefix that omits the last field must never inherit a previous record. */
        if (image[i] == '\n' && i + 1 < n) {
            row = original; assert(!parse(image, i + 1, &row)); assert(!memcmp(&row, &original, sizeof(row)));
        }
    }
    strcat_s(image, sizeof(image), "REC=0x401000\n");
    assert(!parse(image, strlen(image), &row)); image[n] = 0;
    strcat_s(image, sizeof(image), "UNKNOWN=0x1\n");
    assert(!parse(image, strlen(image), &row)); image[n] = 0;
    image[0] = 0; assert(!parse(image, n, &row)); image[0] = 's';
    { char *v = strstr(image, "msgbox=1"); v[7] = '3'; assert(!parse(image, n, &row)); v[7] = '1'; }
    { char *v = strstr(image, "25.2.3.5171"); v[0] = '3'; assert(!parse(image, n, &row)); v[0] = '2'; }
    { char *v = strstr(image, "FLPROJ_LOAD_SLOT=0x38"); assert(v); v[20] = '9'; assert(!parse(image, n, &row)); v[20] = '8'; }
    puts("Compatibility profile parsing: OK (all fields required; duplicates, unknowns, corruption and partial records refused)");
    printf("%u parser checks\n", checks);
    return 0;
}
