/* tests — typed error list, thread-local diagnostic, and logger boundary API. */
#include "utest.h"
#include <ame/error.h>
#include <string.h>

int main(void) {
    printf("=== test_error ===\n");

    UT_CASE("X-macro table has stable success entry");
    UT_ASSERT(AME_ERR_OK == 0);
    UT_ASSERT(strcmp(ame_error_name(AME_ERR_OK), "AME_ERR_OK") == 0);
    UT_ASSERT(strcmp(ame_error_string(AME_ERR_OK), "success") == 0);

    UT_CASE("last error keeps code and detail separate");
    ame_clear_error();
    UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
    ame_set_errorf(AME_ERR_TEXTURE_DECODE, "card.png: bad signature");
    UT_ASSERT(ame_last_error_code() == AME_ERR_TEXTURE_DECODE);
    UT_ASSERT(strcmp(ame_last_error_detail(), "card.png: bad signature") == 0);
    UT_ASSERT(strstr(ame_last_error(), "could not decode texture") != NULL);
    UT_ASSERT(strstr(ame_last_error(), "bad signature") != NULL);

    UT_CASE("invalid code is safe to inspect");
    UT_ASSERT(strcmp(ame_error_name((ame_error_code)-1), "AME_ERR_UNKNOWN") == 0);
    UT_ASSERT(strcmp(ame_error_string((ame_error_code)-1),
                      "invalid error code") == 0);

    ame_clear_error();
    UT_OK();
    return ut_done("test_error");
}
