#include <assert.h>
#include "../../common/flim_cloud.h"

int main(void) {
    const char *original = "{\"t\":\"op\",\"op\":{ \"kind\": \"notes.set\", \"key\": \"chan:1\", \"data\": \"hello world\" }}";
    const char *invalid[] = {
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"hidden\":{\"t\":\"project_swap\"}}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"kind\":\"project.load\"}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"hidden\":[{\"kind\":\"project.load\"}]}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"hidden\":{\"source_fl\":\"25.0.0.1\"}}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"hidden\":{\"\\u0074\":\"project_swap\"}}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"hidden\":{\"x\":1,\"x\":2}}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\"},\"from\":\"spoof\"}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"key\":\"bad\\\"key\"}}",
        "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\"}} garbage"
    };
    const char *hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char header[1024], envelope[1400]; char *merged; FlCloud ref; FlJson body; size_t i;
    assert(fc_header(original, hash, header, sizeof(header)));
    assert(fc_reference(header, &ref)); assert(ref.size == strlen(original));
    snprintf(envelope, sizeof(envelope), "%.*s,\"seq\":42,\"from\":\"host\",\"source_fl\":\"25.2.5.5319\"}", (int)strlen(header) - 1, header);
    merged = fc_merge(envelope, original); assert(merged);
    assert(!strcmp(merged, "{\"t\":\"op\",\"seq\":42,\"from\":\"host\",\"source_fl\":\"25.2.5.5319\",\"op\":{\"kind\":\"notes.set\",\"key\":\"chan:1\",\"data\":\"hello world\"}}")); free(merged);
    assert(!fc_merge(header, original));
    assert(!fc_merge(envelope, "{\"t\":\"op\",\"op\":{\"kind\":\"project.load\"}}"));
    { char changed[256]; memcpy(changed, original, strlen(original) + 1);
      memcpy(strstr(changed, "notes.set"), "clips.set", 9); assert(!fc_merge(envelope, changed));
      memcpy(changed, original, strlen(original) + 1); strstr(changed, "chan:1")[5] = '2'; assert(!fc_merge(envelope, changed)); }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) assert(!fc_original(invalid[i], &ref, &body));
    assert(!fc_reference("{\"t\":\"stored\",\"type\":\"op\",\"h\":\"bad\",\"s\":2,\"kind\":\"notes.set\"}", &ref));
    assert(!fc_reference("{\"t\":\"stored\",\"type\":\"op\",\"h\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"s\":67108865,\"kind\":\"notes.set\"}", &ref));
    original = "{\"t\":\"project_swap\",\"flp\":\"AAAA\"}";
    assert(fc_header(original, hash, header, sizeof(header)));
    snprintf(envelope, sizeof(envelope), "%.*s,\"epoch\":3,\"delivery\":\"snapshot\"}", (int)strlen(header) - 1, header);
    merged = fc_merge(envelope, original); assert(merged && !strcmp(merged, "{\"t\":\"snapshot\",\"epoch\":3,\"flp\":\"AAAA\"}")); free(merged);
    original = "{\"t\":\"op\",\"op\":{\"kind\":\"notes.full\",\"for\":\"older-24\",\"items\":[]}}";
    assert(fc_header(original, hash, header, sizeof(header)) && strstr(header, "\"for\":\"older-24\""));
    snprintf(envelope, sizeof(envelope), "%.*s,\"seq\":43,\"from\":\"host\",\"source_fl\":\"26.1.7.5654\"}", (int)strlen(header) - 1, header);
    merged = fc_merge(envelope, original); assert(merged && strstr(merged, "\"for\":\"older-24\"")); free(merged);
    strstr(envelope, "older-24")[7] = '5'; assert(!fc_merge(envelope, original));
    assert(!fc_header("{\"t\":\"op\",\"op\":{\"kind\":\"notes.full\",\"for\":\"bad id\"}}", hash, header, sizeof(header)));
    puts("Cloudflare file envelopes: OK (metadata, schema, ambiguity, bounds, delivery, verified recipient)");
    return 0;
}
