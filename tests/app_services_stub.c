/* test_editor_geom only compiles the game TU without a platform bootstrap. */
#include <ame/app.h>

void ame_request_quit(void) {}
void ame_cursor_visible(bool on) { (void)on; }
double ame_time_seconds(void) { return 0.0; }
void ame_sleep_seconds(double seconds) { (void)seconds; }
